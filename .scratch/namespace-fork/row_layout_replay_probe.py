#!/usr/bin/env python3
"""A transaction touching two definitions must log each row's own version."""
import argparse
import os
import resource

from fork_parent_truncate_probe import BootstrapExperiment, physical_id
from physical_merge_layout_probe import definition, wait_for
from sstable_layout_probe import Trace


def run(binary):
    os.environ['SEEKDB_SSTABLE_LAYOUT_PROBE'] = '1'
    exp = BootstrapExperiment(binary, 'row_layout_replay', prototype=6)
    exp.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
    try:
        exp.start()
        trace = Trace(exp)
        exp.sql('CREATE DATABASE merge_layout')
        exp.sql('CREATE TABLE merge_layout.a(id INT PRIMARY KEY,v INT)')
        exp.sql('CREATE TABLE merge_layout.b(id INT PRIMARY KEY,v INT, extra INT DEFAULT 7)')
        a, av = definition(exp, table='a')
        b, bv = definition(exp, table='b')
        assert av < bv
        a, b = physical_id(1, a), physical_id(1, b)
        exp.sql('BEGIN')
        exp.sql('INSERT INTO merge_layout.a VALUES(1,10),(2,20)')
        exp.sql('INSERT INTO merge_layout.b VALUES(1,30,7)')
        exp.sql('UPDATE merge_layout.a SET v=11 WHERE id=1')
        exp.sql('SELECT * FROM merge_layout.a WHERE id=2 FOR UPDATE')
        exp.sql('COMMIT')
        def crash():
            exp.connection.close()
            exp.connection = None
            exp.proc.kill()
            exp.proc.wait(timeout=15)
            exp.record('crash_for_recovery', pid=exp.proc.pid)
            exp.start()
        crash()
        assert exp.sql('SELECT * FROM merge_layout.a ORDER BY id') == ((1,11),(2,20))
        assert exp.sql('SELECT * FROM merge_layout.b') == ((1,30,7),)
        # Make the recovered active memtables participate in a fresh freeze.
        exp.sql('UPDATE merge_layout.a SET v=v+1')
        exp.sql('UPDATE merge_layout.b SET v=v+1')
        bound = int(exp.sql('SELECT current_scn()', log=False)[0][0])
        wait_for(lambda: int(exp.sql('SELECT weak_read_scn FROM oceanbase.__all_virtual_ls_info',
            log=False)[0][0]) >= bound, 'weak horizon did not cover writes')
        exp.sql('ALTER SYSTEM MINOR FREEZE')
        wait_for(lambda: len(exp.sql("SELECT DISTINCT tablet_id FROM oceanbase.V$OB_SSTABLES "
            f"WHERE tablet_id IN ({a},{b}) AND table_type IN ('MINI','MINOR')", log=False)) == 2,
            'replayed rows were not dumped')
        crash()
        summaries = trace.read('SSTABLE_LAYOUT_AUDIT_END ')
        assert summaries[-1]['ret'] == 0, summaries
        rows = [row for row in trace.read('SSTABLE_LAYOUT_AUDIT ') if row['tablet'] in (a,b)]
        for tablet, version in ((a,av), (b,bv)):
            own = [row for row in rows if row['tablet'] == tablet]
            assert own and all(row['V'] == version for row in own), own
        assert exp.sql('SELECT * FROM merge_layout.a ORDER BY id') == ((1,12),(2,21))
        assert exp.sql('SELECT * FROM merge_layout.b') == ((1,31,7),)
        exp.record('PASS', case='row_layout_replay', versions=[av,bv], rows=rows,
                   batch_insert=True, single_update=True, row_lock=True, replay_dump_recovery=True)
    finally:
        exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
