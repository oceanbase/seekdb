#!/usr/bin/env python3
"""Historical B data survives physical deletion, GC and restart; final refs release."""
import argparse
import os
from pathlib import Path
import resource
import sys
import time

import pymysql
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fork_parent_truncate_probe import (BootstrapExperiment, connect, metadata,
                                       namespace_id, physical_id, physical_state, tablet_id)


def wait_for(fn, message, seconds=60):
    end = time.monotonic() + seconds
    last = None
    while time.monotonic() < end:
        last = fn()
        if last:
            return last
        time.sleep(.25)
    raise AssertionError((message, last))


def drop_namespace(experiment, name):
    def drop():
        try:
            experiment.sql(f'DROP NAMESPACE {name}')
            return True
        except pymysql.MySQLError as exc:
            if 'active connections' not in str(exc).lower():
                raise
            return False
    wait_for(drop, 'namespace connections did not drain', 15)


def run(binary, complete_baseline=False):
    experiment = BootstrapExperiment(binary, 'historical_gc', prototype=6)
    if complete_baseline:
        experiment.extra_parameters = [('ob_compaction_schedule_interval', '3s')]
    parent = child = new_child = None
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE nstrunc_repro')
        experiment.sql('CREATE TABLE nstrunc_repro.t1(id INT PRIMARY KEY,v INT)')
        experiment.sql('INSERT INTO nstrunc_repro.t1 VALUES(1,10)')
        logical = tablet_id(experiment, 't1')
        experiment.sql('FORK NAMESPACE history_b FROM ns1')
        b = namespace_id(experiment, 'history_b')
        parent = connect(experiment, 'root@history_b')
        experiment.sql('UPDATE nstrunc_repro.t1 SET v=20', parent)
        experiment.sql('FORK NAMESPACE history_c FROM history_b')
        c = namespace_id(experiment, 'history_c')
        retired_snapshots = {
            int(value['fork_cap']) for key, value in metadata(experiment, 1)
            if int(key['namespace_id']) in (b, c)
        }
        child = connect(experiment, 'root@history_c')
        query = 'SELECT id,v FROM nstrunc_repro.t1 ORDER BY id'
        assert experiment.sql(query, child) == ((1,20),)
        experiment.sql('TRUNCATE TABLE nstrunc_repro.t1', parent)
        parent.close()
        parent = None
        drop_namespace(experiment, 'history_b')
        experiment.sql('DROP TABLE nstrunc_repro.t1')
        experiment.sql('CREATE TABLE nstrunc_repro.t1(id INT PRIMARY KEY,v INT)')
        experiment.sql('INSERT INTO nstrunc_repro.t1 VALUES(1,99)')
        assert tablet_id(experiment, 't1') != logical
        experiment.sql('FORK NAMESPACE history_new FROM ns1')
        new_child = connect(experiment, 'root@history_new')
        assert experiment.sql(query, new_child) == ((1,99),)
        source = physical_id(b, logical)
        root_source = physical_id(1, logical)
        ids = [source, root_source, physical_id(c, logical)]
        rows = physical_state(experiment, ids)
        assert any(r[0] == source and r[1:] == (3,1,0) for r in rows), rows
        assert experiment.sql(query, child) == ((1,20),)
        experiment.record('deleted_source_readable', physical=rows)
        log = experiment.base / 'log' / 'seekdb.log'
        initial = log.stat().st_size
        wait_for(lambda: 'task check ls' in log.read_text(errors='replace')[initial:],
                 'physical GC did not execute', 15)
        for _ in range(8):
            assert experiment.sql(query, child) == ((1,20),)
            assert any(r[0] == source and r[3] == 0 for r in physical_state(experiment, ids))
            time.sleep(.5)
        experiment.record('retained_through_gc', physical=physical_state(experiment, ids))
        child.close()
        child = None
        new_child.close()
        new_child = None
        experiment.connection.close()
        experiment.connection = None
        experiment.proc.terminate()
        try:
            experiment.proc.wait(timeout=20)
        except Exception:
            experiment.proc.kill()
            experiment.proc.wait(timeout=10)
        experiment.record('restart', old_returncode=experiment.proc.returncode)
        experiment.start()
        child = connect(experiment, 'root@history_c')
        assert experiment.sql(query, child) == ((1,20),)
        experiment.record('retained_through_restart', physical=physical_state(experiment, ids))
        experiment.sql('UPDATE nstrunc_repro.t1 SET v=v+100', child)
        assert experiment.sql(query, child) == ((1,120),)
        assert experiment.sql(query) == ((1,99),)
        experiment.record('first_write_from_deleted_source', physical=physical_state(experiment, ids))
        if complete_baseline:
            experiment.sql('ALTER SYSTEM MINOR FREEZE')
            def baseline_released():
                rows = physical_state(experiment, [source, root_source])
                assert experiment.sql(query, child, log=False) == ((1,120),)
                return not rows or all(row[3] == 1 for row in rows)
            wait_for(baseline_released, 'baseline did not release retired sources', 90)
            assert experiment.sql(query, child) == ((1,120),)
            experiment.record('baseline_completed_sources_reclaimed', physical=physical_state(experiment, ids))
        child.close()
        child = None
        drop_namespace(experiment, 'history_c')
        def released():
            rows = physical_state(experiment, ids)
            return rows if rows and all(r[3] == 1 for r in rows) else (True if not rows else None)
        result = wait_for(released, 'historical objects not reclaimed after last dependency', 60)
        experiment.record('last_dependency_released_to_shells', physical=result)
        wait_for(lambda: not physical_state(experiment, ids),
                 'empty shells did not complete native physical GC', 60)
        for collection in (3, 8):
            assert not [key for key, _ in metadata(experiment, collection)
                        if int(key['snapshot_id']) in retired_snapshots]
        if complete_baseline:
            experiment.connection.close()
            experiment.connection = None
            experiment.proc.kill()
            experiment.proc.wait(timeout=15)
            experiment.start()
            assert not physical_state(experiment, ids)
            assert experiment.sql(query) == ((1,99),)
            with connect(experiment, 'root@history_new') as surviving:
                assert experiment.sql(query, surviving) == ((1,99),)
            for collection in (3, 8):
                assert not [key for key, _ in metadata(experiment, collection)
                            if int(key['snapshot_id']) in retired_snapshots]
            experiment.record('release_survived_restart', physical=[],
                              retired_snapshots=sorted(retired_snapshots))
        experiment.record('PASS', case='historical_gc_restart_release',
                          physical=physical_state(experiment, ids),
                          all_old_tablets_removed=True, retired_pins_removed=True)
    finally:
        for connection in (parent, child, new_child):
            if connection is not None:
                connection.close()
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--complete-baseline', action='store_true')
    args = parser.parse_args()
    path = Path('/data/1/tmp/seekdb-ns-probes')
    path.mkdir(parents=True, exist_ok=True)
    os.environ['SEEKDB_FORK_PROTOTYPE_TEST_ROOT'] = str(path)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.complete_baseline)
