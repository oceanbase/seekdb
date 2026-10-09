#!/usr/bin/env python3
"""Assert actual G@F selection after independent and post-freeze parent/child DDL.

Requires physical_merge_layout_injection.py and local_schema_install_injection.py
in the disposable test binary.
This gate checks physical results; the separate production major gate checks
global progress. Neither substitutes for the new logical checksum validation.
"""
import argparse
import os
import re
import resource
import time

from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id, physical_id


class Trace:
    def __init__(self, exp):
        self.exp = exp
        self.positions = {}
        self.layouts = []
        self.members = []
        self.mediums = []
        self.minis = []

    def read(self):
        for path in (self.exp.base / 'log').glob('seekdb.log*'):
            if path.suffix == '.wf':
                continue
            inode = path.stat().st_ino
            with path.open('rb') as stream:
                position = self.positions.get(inode, 0)
                stream.seek(position if position <= path.stat().st_size else 0)
                while True:
                    position = stream.tell()
                    line = stream.readline()
                    if not line or not line.endswith(b'\n'):
                        stream.seek(position)
                        break
                    if b'PHYSICAL_MERGE_' not in line and b'LOCAL_SCHEMA_MINI ' not in line:
                        continue
                    text = line.decode(errors='replace')
                    pairs = dict((key, int(value)) for key, value in
                                 re.findall(r'(\w+)=(-?\d+)', text))
                    if 'PHYSICAL_MERGE_LAYOUT ' in text:
                        self.layouts.append(pairs)
                    elif 'PHYSICAL_MERGE_MEMBER ' in text:
                        self.members.append(pairs)
                    elif 'PHYSICAL_MERGE_MEDIUM ' in text:
                        self.mediums.append(pairs)
                    elif 'LOCAL_SCHEMA_MINI ' in text:
                        self.minis.append(pairs)
                self.positions[inode] = stream.tell()


def wait_for(check, message, timeout=90):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = check()
        if value:
            return value
        time.sleep(.3)
    raise AssertionError(message)


def definition(exp, connection=None, table='t'):
    rows = exp.sql("SELECT tablet_id,schema_version FROM oceanbase.__all_table "
                   "WHERE database_id=(SELECT database_id FROM oceanbase.__all_database "
                   "WHERE database_name='merge_layout') AND table_name='" + table + "'",
                   connection, log=False)
    assert len(rows) == 1, rows
    return tuple(map(int, rows[0]))


def run(binary):
    os.environ['SEEKDB_PHYSICAL_MERGE_LAYOUT_PROBE'] = '1'
    os.environ['SEEKDB_LOCAL_SCHEMA_INSTALL_PROBE'] = '1'
    exp = BootstrapExperiment(binary, 'physical_merge_layout', prototype=6)
    try:
        exp.start()
        # Freeze now waits for all inherited system/user baselines. Give this
        # correctness case a bounded preparation budget; timeout behavior is
        # covered independently by freeze_preparation_probe.py.
        exp.connection._read_timeout = 330
        exp.sql('SET ob_query_timeout=300000000')
        exp.sql("ALTER SYSTEM SET ob_compaction_schedule_interval='3s'")
        exp.sql('CREATE DATABASE merge_layout')
        exp.sql('CREATE TABLE merge_layout.t(id INT PRIMARY KEY,v INT)')
        exp.sql('INSERT INTO merge_layout.t VALUES(1,10)')
        exp.sql('FORK NAMESPACE merge_layout_child FROM ns1')
        child_id = namespace_id(exp, 'merge_layout_child')
        with connect(exp, 'root@merge_layout_child') as child:
            exp.sql('ALTER TABLE merge_layout.t ADD COLUMN child_col INT DEFAULT 7', child)
            exp.sql('UPDATE merge_layout.t SET v=20', child)
            exp.sql('ALTER TABLE merge_layout.t ADD COLUMN parent_col INT DEFAULT 9')
            logical, parent_version = definition(exp)
            child_logical, child_version = definition(exp, child)
            assert logical == child_logical
            parent_tablet, child_tablet = physical_id(1, logical), physical_id(child_id, logical)
            trace = Trace(exp)
            exp.sql('ALTER SYSTEM SUSPEND MERGE')
            exp.sql('ALTER SYSTEM MINOR FREEZE')
            previous = int(exp.sql('SELECT frozen_scn FROM oceanbase.DBA_OB_MAJOR_COMPACTION',
                                   log=False)[0][0])
            exp.sql('ALTER SYSTEM MAJOR FREEZE')
            def published():
                current = int(exp.sql('SELECT frozen_scn FROM oceanbase.DBA_OB_MAJOR_COMPACTION',
                                      log=False)[0][0])
                return current if current > previous else None
            frozen = wait_for(published, 'new freeze has not reached the progress view', 30)
            exp.record('freeze_selected', frozen=frozen, parent_tablet=parent_tablet,
                       child_tablet=child_tablet, parent_version=parent_version, child_version=child_version)
            exp.sql('ALTER TABLE merge_layout.t ADD COLUMN after_f INT DEFAULT 11')
            exp.sql('ALTER TABLE merge_layout.t ADD COLUMN after_f INT DEFAULT 13', child)
            assert definition(exp)[1] > parent_version
            assert definition(exp, child)[1] > child_version
            after_parent_version, after_child_version = definition(exp)[1], definition(exp, child)[1]
            exp.sql('UPDATE merge_layout.t SET after_f=21')
            exp.sql('UPDATE merge_layout.t SET after_f=23', child)
            exp.sql('ALTER SYSTEM MINOR FREEZE')
            def newer_minis():
                trace.read()
                prepared = all(any(row['tablet'] == tablet and row['result'] == version
                                   and row['simplified'] == 1 for row in trace.minis)
                               for tablet, version in ((parent_tablet, after_parent_version),
                                                       (child_tablet, after_child_version)))
                installed = exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
                    f"WHERE table_type IN ('MINI','MINOR') AND upper_trans_version>{frozen} "
                    f"AND tablet_id IN ({parent_tablet},{child_tablet})", log=False)
                return prepared and {int(row[0]) for row in installed} == {parent_tablet, child_tablet}
            wait_for(newer_minis, 'post-freeze mini did not advance the local descriptor')
            # A new physical object after F must not be made a member by its
            # initial empty major snapshot or by a logical schema enumeration.
            exp.sql('CREATE TABLE merge_layout.late(id INT PRIMARY KEY,v INT)')
            exp.sql('INSERT INTO merge_layout.late VALUES(1,30)')
            late_tablet = physical_id(1, definition(exp, table='late')[0])
            exp.sql('ALTER SYSTEM RESUME MERGE')

            def target_complete():
                trace.read()
                rows = exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
                    f"WHERE table_type='MAJOR' AND end_log_scn={frozen} "
                    f"AND tablet_id IN ({parent_tablet},{child_tablet})", log=False)
                seen = {row['tablet'] for row in trace.layouts if row['F'] == frozen and row['ret'] == 0}
                return ({int(row[0]) for row in rows} == {parent_tablet, child_tablet}
                        and {parent_tablet, child_tablet}.issubset(seen))

            wait_for(target_complete, ('physical major incomplete', frozen, parent_tablet, child_tablet))
            trace.read()
            selected = {}
            for tablet, version in ((parent_tablet, parent_version), (child_tablet, child_version)):
                matches = [row for row in trace.layouts if row['tablet'] == tablet
                           and row['F'] == frozen and row['ret'] == 0]
                assert matches and all(row['V'] == version and row['C'] <= frozen
                                       for row in matches), (tablet, version, matches)
                selected[tablet] = matches[-1]
            assert selected[parent_tablet]['G'] != selected[child_tablet]['G'], selected

            def excluded():
                trace.read()
                return [row for row in trace.members if row['tablet'] == late_tablet
                        and row['F'] == frozen and row['ret'] == 0]

            late = wait_for(excluded, ('late object was not enumerated', late_tablet, frozen))
            assert all(row['C'] > frozen and row['member'] == 0 and row['satisfied'] == 1
                       for row in late), late
            assert not exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
                f"WHERE tablet_id={late_tablet} AND table_type='MAJOR' AND end_log_scn={frozen}", log=False)
            assert not [row for row in trace.layouts if row['tablet'] == late_tablet and row['F'] == frozen
                        and row['ret'] == 0]
            # Merging the old F must leave the newer DDL usable in both branches.
            exp.sql('UPDATE merge_layout.t SET after_f=21')
            exp.sql('UPDATE merge_layout.t SET after_f=23', child)
            assert exp.sql('SELECT id,v,parent_col,after_f FROM merge_layout.t') == ((1,10,9,21),)
            assert exp.sql('SELECT id,v,child_col,after_f FROM merge_layout.t', child) == ((1,20,7,23),)
            exp.record('PASS', case='physical_merge_layout', frozen=frozen, layouts=selected,
                       late=late[-1], post_freeze_ddl=True, post_freeze_mini=True, independent_branches=True)
    finally:
        exp.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True)
    a = p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(a.binary)
