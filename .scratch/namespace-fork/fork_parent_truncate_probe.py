#!/usr/bin/env python3
"""Regression: truncating a parent must preserve a cold descendant's fork view.

Runs against a disposable real engine. A failed descendant read exits nonzero.
The optional materialization switches provide controls for the same scenario.
The confirmed failing scenario uses --prime-parent-schema to execute one
metadata-only DDL in the parent before forking the descendant.
"""
import argparse
import json
import os
from pathlib import Path
import resource
import sys
import time

import pymysql

sys.path.insert(0, str(Path(__file__).resolve().parents[1] /
                       'excluded-from-branch/tools/obtest'))
from namespace_worker_bootstrap_prototype import BootstrapExperiment
from namespace_inprocess_prototype import connect


def metadata(experiment, collection):
    rows = experiment.sql(
        'SELECT key_json,value_json FROM oceanbase.__all_virtual_instance_metadata '
        f'WHERE collection_id={collection}', log=False)
    return [(json.loads(key), json.loads(value)) for key, value in rows]


def namespace_id(experiment, name):
    matches = [key['namespace_id'] for key, value in metadata(experiment, 1)
               if value['name'] == name]
    assert len(matches) == 1, (name, matches)
    return int(matches[0])


def tablet_id(experiment, table, connection=None):
    rows = experiment.sql(
        "SELECT tablet_id FROM oceanbase.__all_table "
        "WHERE database_id=(SELECT database_id FROM oceanbase.__all_database "
        "WHERE database_name='nstrunc_repro') "
        f"AND table_name='{table}'", connection, log=False)
    assert len(rows) == 1, rows
    return int(rows[0][0])


def physical_id(ns_id, logical):
    return (1 << 62) | (ns_id << 37) | logical


def physical_state(experiment, ids):
    values = ','.join(str(value) for value in ids)
    return experiment.sql(
        'SELECT tablet_id,tablet_status,is_committed,is_empty_shell '
        f'FROM oceanbase.__all_virtual_tablet_info WHERE tablet_id IN ({values}) '
        'ORDER BY tablet_id', log=False)


def exercise(experiment, iteration, args):
    table = f't{iteration}'
    parent_name = f'trunc_parent_{iteration}'
    child_name = f'trunc_child_{iteration}'
    root_expected = ((1, 10),)
    expected = ((1, 20 if args.materialize_parent else 10),)
    experiment.sql(f'CREATE TABLE nstrunc_repro.{table}(id INT PRIMARY KEY,v INT)')
    experiment.sql(f'INSERT INTO nstrunc_repro.{table} VALUES(1,10)')
    original = tablet_id(experiment, table)
    experiment.sql(f'FORK NAMESPACE {parent_name} FROM ns1')
    parent_id = namespace_id(experiment, parent_name)
    parent = connect(experiment, 'root@' + parent_name)
    child = None
    try:
        experiment.sql('SET ob_query_timeout=10000000', parent)
        if args.prime_parent_schema:
            experiment.sql(f"ALTER TABLE nstrunc_repro.{table} COMMENT='parent metadata ddl'", parent)
        experiment.record('parent_recyclebin', iteration=iteration,
                          value=experiment.sql('SELECT @@recyclebin', parent, log=False))
        if args.materialize_parent:
            experiment.sql(f'UPDATE nstrunc_repro.{table} SET v=20', parent)
        experiment.sql(f'FORK NAMESPACE {child_name} FROM {parent_name}')
        child_id = namespace_id(experiment, child_name)
        child = connect(experiment, 'root@' + child_name)
        experiment.sql('SET ob_query_timeout=10000000', child)
        if args.materialize_parent_after_fork:
            experiment.sql(f'UPDATE nstrunc_repro.{table} SET v=30', parent)
        if args.materialize_child:
            experiment.sql(f'UPDATE nstrunc_repro.{table} SET v=v+1', child)
            experiment.sql(f'UPDATE nstrunc_repro.{table} SET v=v-1', child)
        select = f'SELECT id,v FROM nstrunc_repro.{table} ORDER BY id'
        assert experiment.sql(select, parent) == (((1, 30),) if args.materialize_parent_after_fork else expected)
        assert experiment.sql(select, child) == expected
        assert tablet_id(experiment, table, parent) == original
        assert tablet_id(experiment, table, child) == original
        parent_physical = physical_id(parent_id, original)
        child_physical = physical_id(child_id, original)
        source_physical = physical_id(1, original)
        before = physical_state(experiment, [source_physical, parent_physical, child_physical])
        existing = {row[0] for row in before}
        assert source_physical in existing, before
        assert (parent_physical in existing) == (args.materialize_parent or args.materialize_parent_after_fork), before
        assert (child_physical in existing) == args.materialize_child, before
        experiment.record('before_truncate', iteration=iteration,
                          parent_namespace=parent_id, child_namespace=child_id,
                          old_logical_tablet=original, physical=before,
                          materialize_parent=args.materialize_parent,
                          materialize_child=args.materialize_child,
                          prime_parent_schema=args.prime_parent_schema)
        experiment.sql(f'TRUNCATE TABLE nstrunc_repro.{table}', parent)
        assert experiment.sql(select, parent) == ()
        assert experiment.sql(select) == root_expected
        replacement = tablet_id(experiment, table, parent)
        assert replacement != original, (original, replacement)
        assert tablet_id(experiment, table, child) == original
        from ddl_catalog_atomic_probe import roots, graph
        parent_sources = graph(experiment, roots(experiment, parent_id))
        child_sources = graph(experiment, roots(experiment, child_id))
        assert original not in parent_sources and replacement in parent_sources
        assert original in child_sources
        assert metadata(experiment, 4) == [], 'removed exception collection contains rows'
        experiment.record('after_truncate', iteration=iteration,
                          old_logical_tablet=original, new_parent_tablet=replacement,
                          physical=physical_state(experiment, [
                              source_physical, parent_physical, child_physical,
                              physical_id(parent_id, replacement)]),
                          parent_source=parent_sources[replacement], child_source=child_sources[original])
        experiment.sql('SET ob_query_timeout=2000000', child)
        start = time.monotonic()
        error = None
        actual = None
        try:
            actual = experiment.sql(select, child)
        except pymysql.MySQLError as failure:
            error = failure.args
        okay = error is None and actual == expected
        experiment.record('descendant_result', iteration=iteration, expected=expected,
                          actual=actual, error=error,
                          elapsed_s=round(time.monotonic() - start, 3), passed=okay)
        if okay and args.write_child:
            experiment.sql(f'UPDATE nstrunc_repro.{table} SET v=v+100', child)
            assert experiment.sql(select, child) == ((1, expected[0][1] + 100),)
            assert experiment.sql(select, parent) == ()
            assert experiment.sql(select) == root_expected
            experiment.record('first_write_after_truncate', rows=experiment.sql(select, child))
        return okay
    finally:
        if child is not None:
            child.close()
        parent.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--materialize-parent', action='store_true')
    parser.add_argument('--materialize-child', action='store_true')
    parser.add_argument('--materialize-parent-after-fork', action='store_true')
    parser.add_argument('--write-child', action='store_true')
    parser.add_argument('--prime-parent-schema', action='store_true',
                        help='make one metadata-only parent DDL before the descendant fork')
    parser.add_argument('--test-root', type=Path, default=Path('/data/1/tmp/seekdb-ns-probes'))
    args = parser.parse_args()
    assert args.repeat > 0
    assert not (args.materialize_parent and args.materialize_parent_after_fork)
    args.test_root.mkdir(parents=True, exist_ok=True)
    os.environ['SEEKDB_FORK_PROTOTYPE_TEST_ROOT'] = str(args.test_root)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    experiment = BootstrapExperiment(args.binary, 'parent_truncate', prototype=6)
    results = []
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE nstrunc_repro')
        for iteration in range(1, args.repeat + 1):
            results.append(exercise(experiment, iteration, args))
        experiment.record('PASS' if all(results) else 'FAIL',
                          case='fork_parent_truncate', results=results,
                          materialize_parent=args.materialize_parent,
                          materialize_child=args.materialize_child,
                          prime_parent_schema=args.prime_parent_schema)
    finally:
        experiment.close()
    return 0 if results and all(results) else 1


if __name__ == '__main__':
    sys.exit(main())
