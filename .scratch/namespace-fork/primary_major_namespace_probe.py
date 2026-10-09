#!/usr/bin/env python3
"""Regression for physical system-tablet IDs in major checksum validation."""
import argparse
import time
from fork_parent_truncate_probe import BootstrapExperiment, physical_id

p = argparse.ArgumentParser()
p.add_argument('--binary', required=True)
a = p.parse_args()
exp = BootstrapExperiment(a.binary, 'major_progress', prototype=6)
try:
    exp.start()
    columns = [row[0] for row in exp.sql('show columns from oceanbase.__all_freeze_info', log=False)]
    assert 'schema_version' not in columns and 'frozen_scn' in columns and 'data_version' in columns, columns
    exp.sql('create database major_probe')
    exp.sql('create table major_probe.t(id int primary key,v int)')
    exp.sql('insert into major_probe.t values(1,10),(2,20),(3,30)')
    logical = int(exp.sql("select tablet_id from oceanbase.__all_table where table_name='t'", log=False)[0][0])
    exp.sql("alter system set ob_compaction_schedule_interval='3s'")
    exp.sql('alter system minor freeze')
    exp.sql('alter system major freeze')
    end = time.monotonic()+60
    while time.monotonic() < end:
        rows = exp.sql('select frozen_scn,global_broadcast_scn,last_scn from oceanbase.DBA_OB_MAJOR_COMPACTION', log=False)
        if rows and rows[0][0] > 1 and rows[0][0] == rows[0][1] == rows[0][2]:
            break
        time.sleep(.3)
    else:
        raise AssertionError(rows)
    scn = rows[0][0]
    freeze_row = exp.sql('select frozen_scn,data_version from oceanbase.__all_freeze_info '
                         f'where frozen_scn={scn}', log=False)
    assert len(freeze_row) == 1 and freeze_row[0][1] > 0, freeze_row
    exp.record('freeze_without_global_schema', columns=columns, row=freeze_row)
    for tid in (logical, physical_id(1, logical)):
        found = exp.sql(f"select tablet_id,table_type from oceanbase.V$OB_SSTABLES where tablet_id={tid} and end_log_scn={scn}", log=False)
        assert bool(found) == (tid != logical), (tid, found)
        exp.record('sstable_identity', tablet_id=tid, rows=found)
    exp.record('PASS', case='major_progress_physical_ids', rows=rows)
finally:
    exp.close()
