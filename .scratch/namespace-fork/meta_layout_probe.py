#!/usr/bin/env python3
"""Real meta merge must load exact G/V after mini, despite a newer DDL head."""
import argparse
import os
from pathlib import Path
import re
import resource
import tempfile
import time

import pymysql

from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id
from physical_merge_layout_probe import definition, wait_for
from local_schema_install_probe import Trace as MiniTrace


class Trace:
    def __init__(self, exp):
        self.exp, self.positions, self.rows = exp, {}, []

    def read(self, tablet):
        for path in (self.exp.base / 'log').glob('seekdb.log*'):
            if path.suffix == '.wf':
                continue
            inode = path.stat().st_ino
            with path.open('rb') as stream:
                stream.seek(self.positions.get(inode, 0))
                while True:
                    pos = stream.tell()
                    line = stream.readline()
                    if not line or not line.endswith(b'\n'):
                        stream.seek(pos)
                        break
                    if b'META_LAYOUT_SELECTED ' in line:
                        self.rows.append({k: int(v) for k, v in
                                          re.findall(r'(\w+)=(-?\d+)', line.decode(errors='replace'))})
                self.positions[inode] = stream.tell()
        return [row for row in self.rows if row.get('tablet') == tablet]


def run(binary, owner, cold_parent=False):
    with tempfile.TemporaryDirectory(prefix='meta-layout-') as directory:
        trigger = Path(directory) / 'tablet'
        os.environ['SEEKDB_META_LAYOUT_TARGET'] = str(trigger)
        os.environ['SEEKDB_LOCAL_SCHEMA_INSTALL_PROBE'] = '1'
        exp = BootstrapExperiment(binary, 'meta_layout_' + owner + ('_cold' if cold_parent else ''), prototype=6)
        exp.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
        child = None
        try:
            exp.start()
            exp.sql('CREATE DATABASE merge_layout')
            exp.sql('CREATE TABLE merge_layout.t(id INT PRIMARY KEY,v INT)')
            exp.sql('INSERT INTO merge_layout.t VALUES(1,10)')
            def readable():
                bound = int(exp.sql('SELECT current_scn()', log=False)[0][0])
                wait_for(lambda: int(exp.sql('SELECT weak_read_scn FROM oceanbase.__all_virtual_ls_info',
                                             log=False)[0][0]) >= bound, 'weak horizon did not reach committed writes')

            def baseline(tablet):
                readable()
                exp.sql('ALTER SYSTEM MINOR FREEZE')
                requested = 0
                def installed():
                    nonlocal requested
                    rows = exp.sql("SELECT end_log_scn FROM oceanbase.V$OB_SSTABLES "
                                   f"WHERE tablet_id={tablet} AND table_type='MAJOR'", log=False)
                    if not rows:  # first native major is still being installed
                        return None
                    if rows and max(int(row[0]) for row in rows) > 1:
                        return max(int(row[0]) for row in rows)
                    if time.monotonic() - requested > 2:
                        try:
                            exp.sql(f'ALTER SYSTEM MAJOR FREEZE TABLET_ID={tablet}')
                        except pymysql.MySQLError as error:
                            if error.args[0] != 4213:  # prior request still in flight
                                raise
                        requested = time.monotonic()
                    return None
                return wait_for(installed, 'initial major not installed', 180)

            logical, initial = definition(exp)
            tablet = physical_id(1, logical)
            base = 0 if cold_parent else baseline(tablet)
            if owner == 'child':
                # Meta rewrites an existing baseline. Give the child a real
                # inherited major before testing independent layout evolution.
                exp.sql('FORK NAMESPACE meta_child FROM ns1')
                ns = namespace_id(exp, 'meta_child')
                child = connect(exp, 'root@meta_child')
                exp.sql('UPDATE merge_layout.t SET v=20', child)
                tablet = physical_id(ns, logical)
                readable()
                exp.sql('ALTER SYSTEM MINOR FREEZE')
                wait_for(lambda: exp.sql('SELECT ref_tablet_id FROM oceanbase.__all_virtual_tablet_info '
                    f'WHERE tablet_id={tablet}', log=False) == ((0,),),
                    'child baseline takeover not complete', 180)
                base = baseline(tablet)
            trace, mini = Trace(exp), MiniTrace(exp)
            exp.sql('ALTER TABLE merge_layout.t ADD COLUMN extra INT DEFAULT 7', child)
            _, version = definition(exp, child)
            exp.sql('INSERT INTO merge_layout.t VALUES(2,30,8)', child)
            readable()
            exp.sql('ALTER SYSTEM MINOR FREEZE')
            rows = wait_for(lambda: [row for row in mini.read('LOCAL_SCHEMA_MINI ', tablet)
                                    if row['observed'] == version], 'mini did not capture new definition')
            assert all(row['simplified'] == 1 and row['result'] == version for row in rows), rows
            wait_for(lambda: exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
                f"WHERE tablet_id={tablet} AND table_type IN ('MINI','MINOR')", log=False),
                'post-DDL mini not installed')
            # No subsequent data write: local V belongs to the dumped data;
            # the publication head now selects a distinct newer definition.
            exp.sql('ALTER TABLE merge_layout.t ADD COLUMN late_col INT DEFAULT 11', child)
            _, head = definition(exp, child)
            assert head > version > initial
            if child:
                exp.sql('ALTER TABLE merge_layout.t ADD COLUMN parent_only INT DEFAULT 99')
            readable()
            trigger.write_text(str(tablet) + '\n')
            selected = wait_for(lambda: trace.read(tablet), 'meta did not select a layout', 180)[0]
            assert selected['ret'] == 0 and selected['local'] == version, selected
            assert selected['selected'] == version and selected['head'] == head and selected['full'] == 1, selected
            wait_for(lambda: exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
                f"WHERE tablet_id={tablet} AND table_type='META' "
                f"AND end_log_scn={selected['snapshot']}", log=False), 'selected meta result not installed')
            assert selected['snapshot'] > base, (selected, base)
            trigger.unlink()
            expected = ((1, 20 if child else 10, 7, 11), (2, 30, 8, 11))
            assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', child) == expected
            if child:
                assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id') == ((1, 10, 99),)
                child.close()
                child = None
            exp.connection.close()
            exp.connection = None
            exp.proc.kill()
            exp.proc.wait(timeout=15)
            exp.record('crash_for_recovery', pid=exp.proc.pid)
            exp.start()
            if owner == 'child':
                child = connect(exp, 'root@meta_child')
            assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', child) == expected
            exp.record('PASS', case='meta_layout', owner=owner, cold_parent=cold_parent, selected=selected,
                       actual_meta_major=True, latest_head_not_substituted=True, crash_recovery=True)
        finally:
            if child:
                child.close()
            exp.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True)
    p.add_argument('--owner', choices=('initial', 'child'), default='initial')
    p.add_argument('--cold-parent', action='store_true', help='regress missing first major after a minors-only fork')
    a = p.parse_args()
    if a.cold_parent and a.owner != 'child':
        p.error('--cold-parent requires --owner child')
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(a.binary, a.owner, a.cold_parent)
