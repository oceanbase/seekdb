#!/usr/bin/env python3
"""Focused real-engine atomicity, rollback, crash and contention acceptance.

Fault cases use the local instrumented binary; production runs success and
concurrent first creation. All generated data lives in a disposable instance.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import resource
import re
import tempfile
import time

import pymysql
from fork_parent_truncate_probe import (BootstrapExperiment, connect, namespace_id,
                                       physical_id, physical_state, metadata)
from fork_history_gc_probe import wait_for
from ddl_catalog_atomic_probe import roots, graph


def run(binary, faults, selected=None, flush_redo=False):
    control_dir = Path(tempfile.mkdtemp(prefix='seekdb-shared-tx-'))
    control = control_dir / 'control'
    ready = control_dir / 'control.ready'
    release = control_dir / 'control.release'
    redo = control_dir / 'control.flush_redo'
    os.environ['SEEKDB_SHARED_TX_CONTROL'] = str(control)
    experiment = BootstrapExperiment(binary, 'shared_transaction', prototype=6)
    child = second = None
    logical = []
    def disarm():
        release.touch()
        for path in (control, ready, redo):
            if path.exists():
                path.unlink()
    def arm(ns, stage, action):
        disarm()
        release.unlink()
        control.write_text(f'{ns} {stage} {action}\n')
        if flush_redo:
            redo.touch()
    def reached():
        wait_for(ready.exists, 'transaction did not reach controlled boundary', 20)
        experiment.record('controlled_boundary', state=ready.read_text().strip())
    def state(ns, committed):
        ids = [physical_id(ns, value) for value in logical]
        mapped = experiment.sql('SELECT tablet_id FROM oceanbase.__all_tablet_to_table '
                                'WHERE tablet_id IN (' + ','.join(map(str, ids)) + ') ORDER BY tablet_id', log=False)
        physical = physical_state(experiment, ids)
        source_graph = graph(experiment, roots(experiment, ns))
        selected_sources = [source_graph[tablet] for tablet in logical]
        if committed:
            assert {value[1] for value, cap in selected_sources} == set(ids), selected_sources
            assert all(value[2] > 0 and cap == 0 for value, cap in selected_sources), selected_sources
        else:
            parent_graph = graph(experiment, roots(experiment, 1))
            assert all(source_graph[tablet][0] == parent_graph[tablet][0]
                       and source_graph[tablet][1] > 0 for tablet in logical), selected_sources
        visible = [row for row in physical if row[2] and not row[3] and row[1] == 1]
        if committed:
            assert not mapped, mapped
            assert {row[0] for row in visible} == set(ids), physical
        else:
            assert not mapped, mapped
            assert not visible, physical
        experiment.record('atomic_state', namespace=ns, committed=committed,
                          mappings=mapped, physical=physical, sources=selected_sources)
    def restart():
        nonlocal child, second
        for connection in (child, second, experiment.connection):
            if connection is not None:
                connection.close()
        child = second = experiment.connection = None
        disarm()
        expected_versions = {}
        cursors = {}
        if os.environ.get('SEEKDB_CREATION_IDENTITY_PROBE'):
            committed = re.compile(r'CREATION_PHYSICAL_COMMIT transaction=(\d+) physical=(\d+) native=(\d+)')
            for log in (experiment.base / 'log').glob('seekdb.log*'):
                cursors[log.stat().st_ino] = log.stat().st_size
                with log.open(errors='replace') as stream:
                    for line in stream:
                        if 'CREATION_PHYSICAL_COMMIT' not in line:
                            continue
                        match = committed.search(line)
                        if match:
                            tx, physical, actual_commit = map(int, match.groups())
                            assert physical == actual_commit, line
                            assert tx not in expected_versions or expected_versions[tx] == physical, line
                            expected_versions[tx] = physical
            assert expected_versions, 'missing pre-crash native commit evidence'
        experiment.start()
        if expected_versions:
            recovered = re.compile(r'CREATION_IDENTITY_RECOVER .*expected=(\d+) actual=(\d+) logical=(\d+) physical=(\d+) node_version_valid=(\d+) ret=0')
            checked = 0
            persisted = 0
            for log in (experiment.base / 'log').glob('seekdb.log*'):
                with log.open('rb') as stream:
                    stream.seek(cursors.get(log.stat().st_ino, 0))
                    for raw in stream:
                        match = recovered.search(raw.decode(errors='replace'))
                        if match:
                            expected_tx, actual_tx, logical, physical, node_version_valid = map(int, match.groups())
                            assert actual_tx == expected_tx and physical > logical, match.groups()
                            assert expected_versions.get(actual_tx) == physical, (match.groups(), expected_versions.get(actual_tx))
                            checked += 1
                            persisted += node_version_valid == 0
            assert checked > 0, 'missing recovered physical creation evidence'
            assert persisted > 0, 'persisted MDS branch was not exercised'
            experiment.record('physical_creation_versions_verified', tablets=checked,
                              authority='pre_crash_native_commit', persisted_mds_tablets=persisted)
    def check_data(connection, value):
        assert experiment.sql('SELECT id,v,LENGTH(b),LEFT(b,1) FROM nstrunc_repro.t1 ORDER BY id', connection) == (
            (1, value, 12000 if value == 11 else 10000, 'c' if value == 11 else 'p'),)
    update = "UPDATE nstrunc_repro.t1 SET v=11,b=REPEAT('c',12000) WHERE id=1"
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE nstrunc_repro')
        experiment.sql('CREATE TABLE nstrunc_repro.t1(id BIGINT AUTO_INCREMENT PRIMARY KEY,v INT,b LONGTEXT)')
        experiment.sql("INSERT INTO nstrunc_repro.t1(v,b) VALUES(10,REPEAT('p',10000))")
        main = experiment.sql("SELECT table_id,tablet_id FROM oceanbase.__all_table WHERE table_name='t1' "
                              "AND database_id=(SELECT database_id FROM oceanbase.__all_database "
                              "WHERE database_name='nstrunc_repro')", log=False)
        assert len(main) == 1, main
        logical = [int(row[0]) for row in experiment.sql(
            f'SELECT tablet_id FROM oceanbase.__all_table WHERE table_id={main[0][0]} '
            f'OR data_table_id={main[0][0]} ORDER BY tablet_id', log=False)]
        assert len(logical) == 3 and int(main[0][1]) in logical, logical
        cases = [('success', None, None)]
        if faults:
            cases += [('rollback_create', 'after_create', 'fail'),
                      ('rollback_owned', 'after_owned', 'fail'),
                      ('crash_create', 'after_create', 'pause'),
                      ('crash_owned', 'after_owned', 'pause'),
                      ('crash_committed', 'after_commit', 'pause'),
                      ('unknown_commit', 'after_commit', 'unknown')]
        if selected:
            cases = [case for case in cases if case[0] in selected]
            assert cases, selected
        for name, stage, action in cases:
            experiment.sql(f'FORK NAMESPACE {name} FROM ns1')
            ns = namespace_id(experiment, name)
            child = connect(experiment, 'root@' + name)
            experiment.sql('SET ob_query_timeout=30000000', child)
            check_data(child, 10)  # Preload the inherited/owned cache before failure.
            state(ns, False)
            if stage:
                arm(ns, stage, action)
            if action == 'pause':
                with ThreadPoolExecutor(max_workers=1) as pool:
                    pending = pool.submit(experiment.sql, update, child)
                    reached()
                    assert not pending.done()
                    state(ns, stage == 'after_commit')
                    # A CREATE with a fork birth SCN is still uncommitted here.
                    # Another reader must retain the inherited view while the
                    # materializer is paused, including its out-of-row LOB.
                    with connect(experiment, 'root@' + name) as reader:
                        check_data(reader, 10)
                    experiment.record('read_during_create', stage=stage,
                                      inherited_rows=True, lob=True)
                    experiment.proc.kill()
                    experiment.proc.wait(timeout=15)
                    try:
                        pending.result(timeout=15)
                    except pymysql.MySQLError as error:
                        assert error.args[0] in (2006, 2013), error
                    else:
                        raise AssertionError('request returned before controlled crash')
                restart()
                # Inspect before any child login/schema publication can repair it.
                state(ns, stage == 'after_commit')
                child = connect(experiment, 'root@' + name)
                check_data(child, 10)
            elif action:
                try:
                    experiment.sql(update, child)
                except pymysql.MySQLError as error:
                    expected = 4016 if action == 'fail' else 4012
                    assert error.args[0] == expected, error
                    experiment.record('expected_failure', case=name, error=error.args)
                else:
                    raise AssertionError('injected failure did not reach the client')
                reached()
                disarm()
                state(ns, action == 'unknown')
                check_data(child, 10)
            experiment.sql(update, child)
            state(ns, True)
            check_data(child, 11)
            check_data(experiment.connection, 10)
            experiment.sql("INSERT INTO nstrunc_repro.t1(v,b) VALUES(12,'sequence')", child)
            generated = experiment.sql('SELECT MAX(id) FROM nstrunc_repro.t1', child)[0][0]
            assert generated > 1, generated
            experiment.sql("INSERT INTO nstrunc_repro.t1(v,b) VALUES(13,'next sequence')", child)
            assert experiment.sql('SELECT MAX(id) FROM nstrunc_repro.t1', child)[0][0] > generated
            durable_rows = experiment.sql('SELECT id,v,LENGTH(b) FROM nstrunc_repro.t1 ORDER BY id', child)
            experiment.record('case_pass', case=name, lob_binding=True, sequence=True)
            child.close()
            child = None
        if selected:
            experiment.proc.kill()
            experiment.proc.wait(timeout=15)
            restart()
            state(ns, True)
            child = connect(experiment, 'root@' + name)
            assert experiment.sql('SELECT id,v,LENGTH(b) FROM nstrunc_repro.t1 ORDER BY id', child) == durable_rows
            experiment.record('PASS', case='shared_transaction_selected', selected=selected, flushed_redo=flush_redo, recovered_committed_retry=True)
            return
        # First writes to the same inherited binding unit must serialize.
        experiment.sql('FORK NAMESPACE concurrent_copy FROM ns1')
        ns = namespace_id(experiment, 'concurrent_copy')
        child = connect(experiment, 'root@concurrent_copy')
        second = connect(experiment, 'root@concurrent_copy')
        for connection in (child, second):
            experiment.sql('SET ob_query_timeout=30000000', connection)
        if faults:
            arm(ns, 'after_owned', 'pause')
        with ThreadPoolExecutor(max_workers=2) as pool:
            # Materialization commits before the user's row write. Either
            # writer may acquire that row first; use two increments so both
            # valid serial orders must preserve both successful updates.
            first = pool.submit(experiment.sql,
                "UPDATE nstrunc_repro.t1 SET v=v+1,b=REPEAT('c',12000) WHERE id=1", child)
            if faults:
                reached()
                state(ns, False)
            contender = pool.submit(experiment.sql, "UPDATE nstrunc_repro.t1 SET v=v+1 WHERE id=1", second)
            if faults:
                time.sleep(.2)
                assert not first.done()
                disarm()
            first.result(timeout=30)
            try:
                contender.result(timeout=30)
            except pymysql.MySQLError as error:
                # Existing inherited scans can ask for retry while CREATE is
                # uncommitted; they must never expose the incomplete copy.
                assert error.args[0] == 4023, error
                experiment.record('inherited_read_requested_retry', error=error.args)
                experiment.sql("UPDATE nstrunc_repro.t1 SET v=v+1 WHERE id=1", second)
        state(ns, True)
        actual = experiment.sql('SELECT id,v,LENGTH(b) FROM nstrunc_repro.t1', child)
        assert actual == ((1,12,12000),), actual
        check_data(experiment.connection, 10)
        experiment.record('case_pass', case='concurrent_creation', rows=actual)
        child.close()
        second.close()
        experiment.sql('FORK NAMESPACE locked_creation FROM ns1')
        locked_ns = namespace_id(experiment, 'locked_creation')
        child = connect(experiment, 'root@locked_creation')
        second = connect(experiment, 'root@locked_creation')
        insert_a = "INSERT INTO nstrunc_repro.t1 VALUES(20,20,REPEAT('a',11000))"
        insert_b = "INSERT INTO nstrunc_repro.t1 VALUES(30,30,REPEAT('b',11000))"
        if faults:
            # A pins its KV snapshot, then B commits creation. A's lock/read
            # must use the committed winner, not its earlier pinned snapshot.
            arm(locked_ns, 'before_lock', 'pause_once')
            with ThreadPoolExecutor(max_workers=1) as pool:
                pending = pool.submit(experiment.sql, insert_a, child)
                reached()
                assert not pending.done()
                experiment.sql(insert_b, second)
                state(locked_ns, True)
                disarm()
                pending.result(timeout=30)
        else:
            with ThreadPoolExecutor(max_workers=2) as pool:
                one = pool.submit(experiment.sql, insert_a, child)
                two = pool.submit(experiment.sql, insert_b, second)
                one.result(timeout=30)
                two.result(timeout=30)
        state(locked_ns, True)
        assert experiment.sql('SELECT id,v FROM nstrunc_repro.t1 ORDER BY id', child) == ((1,10),(20,20),(30,30))
        experiment.record('case_pass', case='concurrent_insert_and_stale_snapshot', faults=faults)
        experiment.proc.kill()
        experiment.proc.wait(timeout=15)
        restart()
        state(ns, True)
        child = connect(experiment, 'root@concurrent_copy')
        assert experiment.sql('SELECT id,v,LENGTH(b) FROM nstrunc_repro.t1', child) == actual
        experiment.record('PASS', case='shared_transaction', faults=faults,
                          atomic_lob_unit=True, rollback=True if faults else None,
                          crash_recovery=True, concurrent_creation=True)
    finally:
        disarm()
        for connection in (child, second):
            if connection is not None:
                connection.close()
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--faults', action='store_true')
    parser.add_argument('--case', action='append')
    parser.add_argument('--flush-redo', action='store_true')
    parser.add_argument('--creation-identities', action='store_true')
    args = parser.parse_args()
    if args.creation_identities:
        os.environ['SEEKDB_CREATION_IDENTITY_PROBE'] = '1'
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.faults, args.case, args.flush_redo)
