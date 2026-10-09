#!/usr/bin/env python3
"""Pause physical materialization before owned publication; race fork and GC."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import resource
import time

from fork_parent_truncate_probe import (BootstrapExperiment, connect, namespace_id,
                                       physical_id, physical_state, tablet_id, metadata)
from fork_history_gc_probe import wait_for


def run(binary):
    experiment = BootstrapExperiment(binary, 'historical_concurrency', prototype=6)
    parent = child = fork_connection = signal_connection = descendant = None
    try:
        experiment.start()
        experiment.sql("ALTER SYSTEM SET debug_sync_timeout='60s'")
        experiment.sql('CREATE DATABASE nstrunc_repro')
        experiment.sql('CREATE TABLE nstrunc_repro.t1(id INT PRIMARY KEY,v INT)')
        experiment.sql('INSERT INTO nstrunc_repro.t1 VALUES(1,10)')
        logical = tablet_id(experiment, 't1')
        experiment.sql('FORK NAMESPACE race_b FROM ns1')
        b = namespace_id(experiment, 'race_b')
        parent = connect(experiment, 'root@race_b')
        experiment.sql('UPDATE nstrunc_repro.t1 SET v=20', parent)
        experiment.sql('FORK NAMESPACE race_c FROM race_b')
        child = connect(experiment, 'root@race_c')
        experiment.sql('SET ob_query_timeout=30000000', child)
        assert experiment.sql('SELECT id,v FROM nstrunc_repro.t1', child) == ((1,20),)
        experiment.sql('TRUNCATE TABLE nstrunc_repro.t1', parent)
        fork_connection = connect(experiment, 'root')
        signal_connection = connect(experiment, 'root')
        experiment.sql('SET ob_query_timeout=30000000', fork_connection)
        snapshots_before = metadata(experiment, 3)
        pins_before = metadata(experiment, 8)
        experiment.sql("SET ob_global_debug_sync='AFTER_UPDATE_TABLET_TO_LS signal history_mat_ready "
                       "wait_for history_mat_release timeout 20000000 execute 1'", signal_connection)
        with ThreadPoolExecutor(max_workers=2) as pool:
            writer = pool.submit(experiment.sql, 'UPDATE nstrunc_repro.t1 SET v=120', child)
            try:
                experiment.sql("SET ob_global_debug_sync='now wait_for history_mat_ready timeout 10000000'",
                               signal_connection)
                assert not writer.done()
                fork = pool.submit(experiment.sql, 'FORK NAMESPACE race_d FROM race_c', fork_connection)
                time.sleep(.3)
                retry = fork.done()
                if retry:
                    error = fork.exception()
                    assert error is not None and any(word in str(error).lower()
                                                     for word in ('lock', 'conflict', 'retry')), error
                    assert not any(value['name'] == 'race_d' for key, value in metadata(experiment, 1))
                    assert metadata(experiment, 3) == snapshots_before
                    assert metadata(experiment, 8) == pins_before
                    experiment.record('fork_rejected_unpublished_copy', error=str(error))
                log = experiment.base / 'log/seekdb.log'
                before = log.stat().st_size
                wait_for(lambda: 'empty shell timer task' in log.read_text(errors='replace')[before:],
                         'GC did not run while materialization was paused', 8)
                rows = physical_state(experiment, [physical_id(b, logical)])
                assert rows == ((physical_id(b, logical),3,1,0),), rows
                assert not writer.done()
                if not retry:
                    assert not fork.done()
                experiment.record('fork_and_gc_respected_materialization_boundary', source=rows,
                                  fork_requested_retry=retry)
            finally:
                experiment.sql("SET ob_global_debug_sync='now signal history_mat_release'", signal_connection)
            writer.result(timeout=20)
            if retry:
                experiment.sql('FORK NAMESPACE race_d FROM race_c', fork_connection)
            else:
                fork.result(timeout=20)
        descendant = connect(experiment, 'root@race_d')
        rows = experiment.sql('SELECT id,v FROM nstrunc_repro.t1', descendant)
        assert rows in (((1,20),), ((1,120),)), rows
        if retry:
            assert rows == ((1,120),), rows
        assert experiment.sql('SELECT id,v FROM nstrunc_repro.t1', child) == ((1,120),)
        assert experiment.sql('SELECT id,v FROM nstrunc_repro.t1', parent) == ()
        assert experiment.sql('SELECT id,v FROM nstrunc_repro.t1') == ((1,10),)
        experiment.sql('UPDATE nstrunc_repro.t1 SET v=300', descendant)
        assert experiment.sql('SELECT id,v FROM nstrunc_repro.t1', descendant) == ((1,300),)
        assert experiment.sql('SELECT id,v FROM nstrunc_repro.t1', child) == ((1,120),)
        experiment.record('PASS', case='historical_materialize_fork_gc_interleaving', fork_rows=rows)
    finally:
        for connection in (parent, child, fork_connection, signal_connection, descendant):
            if connection is not None:
                connection.close()
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    directory = Path('/data/1/tmp/seekdb-ns-probes')
    directory.mkdir(parents=True, exist_ok=True)
    os.environ['SEEKDB_FORK_PROTOTYPE_TEST_ROOT'] = str(directory)
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    run(args.binary)
