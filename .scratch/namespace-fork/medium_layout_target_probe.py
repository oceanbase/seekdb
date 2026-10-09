#!/usr/bin/env python3
"""Exercise a tablet-only medium with a protected physical layout/target pair."""
import argparse
import os
import resource
import time

from fork_parent_truncate_probe import BootstrapExperiment, physical_id
from physical_merge_layout_probe import Trace, definition, wait_for


def run(binary, force_old):
    os.environ['SEEKDB_PHYSICAL_MERGE_LAYOUT_PROBE'] = '1'
    if force_old:
        os.environ['SEEKDB_MEDIUM_FORCE_OLD_TARGET'] = '1'
    exp = BootstrapExperiment(binary, 'medium_layout_target', prototype=6)
    try:
        exp.start()
        exp.sql("ALTER SYSTEM SET ob_compaction_schedule_interval='3s'")
        exp.sql('CREATE DATABASE merge_layout')
        exp.sql('CREATE TABLE merge_layout.t(id INT PRIMARY KEY,v INT)')
        exp.sql('INSERT INTO merge_layout.t VALUES(1,10)')
        exp.sql('ALTER TABLE merge_layout.t ADD COLUMN extra INT DEFAULT 7')
        logical, version = definition(exp)
        physical = physical_id(1, logical)
        exp.sql('ALTER SYSTEM MINOR FREEZE')
        trace = Trace(exp)
        last_request = 0
        def selected():
            nonlocal last_request
            trace.read()
            found = [row for row in trace.mediums if row['tablet'] == physical and row['ret'] == 0]
            if found:
                return found[0]
            if time.monotonic() - last_request > 1:
                exp.sql(f'ALTER SYSTEM MAJOR FREEZE TABLET_ID={physical}')
                last_request = time.monotonic()
            return None
        chosen = wait_for(selected, ('tablet-only medium did not select layout', physical))
        assert chosen['B'] >= chosen['C'] and chosen['V'] == version, chosen
        if force_old:
            assert chosen['proposed'] == 1 and chosen['B'] > chosen['proposed'], chosen
        wait_for(lambda: exp.sql("SELECT tablet_id FROM oceanbase.V$OB_SSTABLES "
            f"WHERE tablet_id={physical} AND table_type='MAJOR' AND end_log_scn={chosen['B']}", log=False),
            ('tablet-only medium did not produce the selected B', chosen))
        assert exp.sql('SELECT id,v,extra FROM merge_layout.t') == ((1,10,7),)
        exp.record('PASS', case='medium_layout_target', selected=chosen, forced_old=force_old)
    finally:
        exp.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True)
    p.add_argument('--force-old-target', action='store_true')
    a = p.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(a.binary, a.force_old_target)
