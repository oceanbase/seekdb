#!/usr/bin/env python3
"""Check origin layouts through real rewrite, three generations and recovery.

Requires sstable_layout_injection.py and baseline_progress_injection.py.
"""
import argparse
import os
from pathlib import Path
import re
import resource
import tempfile

from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id
from physical_merge_layout_probe import definition, wait_for


class Trace:
    def __init__(self, exp):
        self.exp, self.positions, self.rows = exp, {}, []
        self.rewrites = set()

    def read(self, prefix):
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
                    if b'fork rewrite: successfully created sstable' in line:
                        match = re.search(rb'param_->dest_tablet_id_=\{id:(\d+)\}', line)
                        if match:
                            self.rewrites.add(int(match.group(1)))
                    if b'FORK_INPUT_LAYOUT ' in line or b'SSTABLE_LAYOUT_AUDIT' in line:
                        text = line.decode(errors='replace')
                        self.rows.append((text, {k: int(v) for k, v in
                            re.findall(r'(\w+)=(-?\d+)', text)}))
                self.positions[inode] = stream.tell()
        return [row for text, row in self.rows if prefix in text]


def run(binary, compact_mixed=False):
    with tempfile.TemporaryDirectory(prefix='sstable-layout-') as directory:
        pause = Path(directory) / 'baseline'
        os.environ['SEEKDB_BASELINE_PAUSE'] = str(pause)
        os.environ['SEEKDB_BASELINE_PROGRESS_PROBE'] = '1'
        os.environ['SEEKDB_SSTABLE_LAYOUT_PROBE'] = '1'
        exp = BootstrapExperiment(binary, 'sstable_layout' + ('_mixed' if compact_mixed else ''), prototype=6)
        exp.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
        child = grandchild = None
        try:
            exp.start()
            trace = Trace(exp)
            # Template initialization must finish before pausing background work.
            pause.write_text('0\n')
            exp.sql('CREATE DATABASE merge_layout')
            exp.sql('CREATE TABLE merge_layout.t(id INT PRIMARY KEY,v INT)')
            exp.sql('INSERT INTO merge_layout.t VALUES(1,10),(3,30)')
            logical, initial_v = definition(exp)
            parent_tablet = physical_id(1, logical)
            exp.sql('FORK NAMESPACE layout_child FROM ns1')
            child = connect(exp, 'root@layout_child')
            child_tablet = physical_id(namespace_id(exp, 'layout_child'), logical)
            exp.sql('UPDATE merge_layout.t SET v=20 WHERE id=1', child)
            exp.sql('ALTER TABLE merge_layout.t ADD COLUMN parent_only INT DEFAULT 11')
            if compact_mixed:
                exp.sql('ALTER TABLE merge_layout.t ADD COLUMN parent_extra INT DEFAULT 17')
            parent_v = definition(exp)[1]
            exp.sql('UPDATE merge_layout.t SET parent_only=99 WHERE id=1')

            def dump():
                bound = int(exp.sql('SELECT current_scn()', log=False)[0][0])
                wait_for(lambda: int(exp.sql('SELECT weak_read_scn FROM oceanbase.__all_virtual_ls_info',
                    log=False)[0][0]) >= bound, 'weak horizon did not reach committed writes')
                exp.sql('ALTER SYSTEM MINOR FREEZE')

            def complete(tablet):
                wait_for(lambda: exp.sql('SELECT ref_tablet_id FROM oceanbase.__all_virtual_tablet_info '
                    f'WHERE tablet_id={tablet}', log=False) == ((0,),), 'takeover did not complete', 180)

            dump()
            # Do not release until an SSTable contains both sides of the fork.
            wait_for(lambda: exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
                f"WHERE tablet_id={parent_tablet} AND table_type IN ('MINI','MINOR')", log=False),
                'parent mini not installed')
            pause.unlink()
            complete(child_tablet)
            inputs = wait_for(lambda: [row for row in trace.read('FORK_INPUT_LAYOUT ')
                if row['tablet'] == parent_tablet and row['V'] == parent_v],
                'parent input did not use its own post-fork definition')
            assert all(row['V'] == row['selected'] and row['full'] == 1 for row in inputs), inputs
            assert child_tablet in trace.rewrites, trace.rewrites
            assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', child) == ((1,20),(3,30))
            parent_g = inputs[0]['G']

            pause.write_text('0\n')
            exp.sql('FORK NAMESPACE layout_grandchild FROM layout_child')
            grandchild = connect(exp, 'root@layout_grandchild')
            grandchild_tablet = physical_id(namespace_id(exp, 'layout_grandchild'), logical)
            exp.sql('UPDATE merge_layout.t SET v=40 WHERE id=3', grandchild)
            exp.sql('ALTER TABLE merge_layout.t ADD COLUMN child_only INT DEFAULT 13', child)
            child_v = definition(exp, child)[1]
            child_bound = int(exp.sql('SELECT current_scn()', log=False)[0][0])
            exp.sql('UPDATE merge_layout.t SET child_only=88 WHERE id=1', child)
            dump()
            wait_for(lambda: exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
                f"WHERE tablet_id={child_tablet} AND table_type IN ('MINI','MINOR') "
                f"AND upper_trans_version>{child_bound}", log=False), 'child mini not installed')
            pause.unlink()
            complete(grandchild_tablet)
            second = wait_for(lambda: [row for row in trace.read('FORK_INPUT_LAYOUT ')
                if row['tablet'] == child_tablet], 'grandchild did not load source layouts')
            assert any(row['G'] != parent_g and row['V'] == child_v for row in second), second
            assert grandchild_tablet in trace.rewrites, trace.rewrites
            assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', grandchild) == ((1,20),(3,40))
            assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', child) == ((1,20,88),(3,30,13))
            exp.sql('CREATE INDEX layout_idx ON merge_layout.t(v)', child)
            exp.sql('DROP TABLE merge_layout.t')
            for conn in (child, grandchild, exp.connection):
                conn.close()
            child = grandchild = exp.connection = None
            exp.proc.kill()
            exp.proc.wait(timeout=15)
            exp.record('crash_for_recovery', pid=exp.proc.pid)
            exp.start()
            audit = trace.read('SSTABLE_LAYOUT_AUDIT_END ')[-1]
            assert audit['ret'] == 0 and audit['count'] > 0 and audit['inherited'] > 0, audit
            persisted = [row for row in trace.read('SSTABLE_LAYOUT_AUDIT ')
                         if row['tablet'] in (child_tablet, grandchild_tablet)]
            assert any(row['origin'] == parent_g and row['owned'] != parent_g for row in persisted), persisted
            child = connect(exp, 'root@layout_child')
            grandchild = connect(exp, 'root@layout_grandchild')
            assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', grandchild) == ((1,20),(3,40))
            assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', child) == ((1,20,88),(3,30,13))
            if compact_mixed:
                # Collapse inherited wider inputs and local rows into one native
                # minor. Its persisted G/V must describe the actual output too.
                exp.sql('ALTER SYSTEM SET minor_compact_trigger=0')
                bound = int(exp.sql('SELECT current_scn()', log=False)[0][0])
                exp.sql('UPDATE merge_layout.t SET v=v+1 WHERE id=1', child)
                dump()
                wait_for(lambda: exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
                    f"WHERE tablet_id={child_tablet} AND table_type='MINOR' "
                    f"AND upper_trans_version>{bound}", log=False), 'mixed native minor did not install', 180)
                for conn in (child, grandchild, exp.connection):
                    conn.close()
                child = grandchild = exp.connection = None
                exp.proc.kill()
                exp.proc.wait(timeout=15)
                exp.record('crash_after_mixed_minor', pid=exp.proc.pid)
                exp.start()
                audit = trace.read('SSTABLE_LAYOUT_AUDIT_END ')[-1]
                assert audit['ret'] == 0 and audit['count'] > 0, audit
                child = connect(exp, 'root@layout_child')
                grandchild = connect(exp, 'root@layout_grandchild')
                assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', child) == ((1,21,88),(3,30,13))
                assert exp.sql('SELECT * FROM merge_layout.t ORDER BY id', grandchild) == ((1,20),(3,40))
            exp.record('PASS', case='sstable_layout', compact_mixed=compact_mixed,
                       parent_g=parent_g, initial_v=initial_v,
                       parent_v=parent_v, child_v=child_v, audit=audit, persisted=persisted,
                       source_drop=True, ddl_index=True, actual_rewrites=True, crash_recovery=True)
        finally:
            for conn in (child, grandchild):
                if conn:
                    conn.close()
            exp.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True)
    p.add_argument('--compact-mixed', action='store_true', help='merge wider inherited and local minor inputs')
    a = p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(a.binary, a.compact_mixed)
