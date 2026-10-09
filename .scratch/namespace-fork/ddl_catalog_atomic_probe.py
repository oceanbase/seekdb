#!/usr/bin/env python3
"""Observe SQL and source roots across actual native DDL commit/abort/replay."""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import resource
import struct
import time

import pymysql
from fork_parent_truncate_probe import BootstrapExperiment, connect, metadata, namespace_id, tablet_id, physical_id, physical_state
from fork_history_gc_probe import wait_for


def roots(experiment, owner_id):
    return next(value for key, value in metadata(experiment, 1)
                if int(key['namespace_id']) == owner_id)


def graph(experiment, state):
    rows = experiment.sql('SELECT key_json,value_base64 FROM '
        'oceanbase.__all_virtual_instance_metadata WHERE collection_id=5', log=False)
    pages = {int(json.loads(key)['page_id']): base64.b64decode(value) for key, value in rows}
    entries = {}
    def minimum(a, b):
        return min(a, b) if a and b else a or b
    def visit(page, cap):
        data = pages[page]
        offset = 0
        def number():
            nonlocal offset
            value, = struct.unpack_from('<Q', data, offset)
            offset += 8
            return value
        def blob():
            nonlocal offset
            size = number()
            value = data[offset:offset+size]
            assert len(value) == size
            offset += size
            return value
        assert number() == 1
        leaf, count = number(), number()
        keys = [int(blob()) for _ in range(count)]
        if leaf:
            for key in keys:
                value, local_cap = blob(), number()
                decoded = struct.unpack('<QQqQQQ', value)
                assert decoded[0] > 0 and decoded[1] > 0 and decoded[2] > 0 and decoded[3] > 0
                assert key not in entries
                entries[key] = (decoded, minimum(cap, local_cap))
        else:
            children = [(number(), number()) for _ in range(count + 1)]
            for child, child_cap in children:
                visit(child, minimum(cap, child_cap))
        assert offset == len(data)
    visit(int(state['directory_page']), int(state['directory_cap']))
    return entries


def run(binary, owner_kind, fault):
    experiment = BootstrapExperiment(binary, 'ddl_catalog_' + owner_kind + '_' + fault, prototype=6)
    control = experiment.base / 'ddl-catalog-control'
    os.environ['SEEKDB_DDL_CATALOG_CONTROL'] = str(control)
    os.environ['SEEKDB_SHARED_TX_CONTROL'] = str(control) + '.redo'
    Path(str(control) + '.redo.flush_redo').touch()
    owner = observer = child = None
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE nstrunc_repro')
        experiment.sql('CREATE TABLE nstrunc_repro.t1(id INT PRIMARY KEY,v INT)')
        experiment.sql('INSERT INTO nstrunc_repro.t1 VALUES(1,10)')
        owner_name = 'ns1'
        if owner_kind == 'child':
            owner_name = 'atomic_owner'
            experiment.sql('FORK NAMESPACE atomic_owner FROM ns1')
        owner_id = namespace_id(experiment, owner_name)
        owner = connect(experiment, 'root@' + owner_name)
        observer = connect(experiment, 'root@' + owner_name)
        if owner_kind == 'child':
            experiment.sql('UPDATE nstrunc_repro.t1 SET v=20', owner)
        expected = 20 if owner_kind == 'child' else 10
        original = tablet_id(experiment, 't1', owner)
        experiment.sql('FORK NAMESPACE atomic_old FROM ' + owner_name)
        child = connect(experiment, 'root@atomic_old')
        child_id = namespace_id(experiment, 'atomic_old')
        query = 'SELECT id,v FROM nstrunc_repro.t1'
        assert experiment.sql(query, child) == ((1, expected),)
        before = roots(experiment, owner_id)
        old_graph = graph(experiment, before)
        assert original in old_graph
        stage = 'after_commit' if fault == 'commit_crash' else 'before_commit'
        action = 'fail' if fault == 'rollback' else 'pause'
        control.write_text(f'{owner_id} {stage} {action}\n')
        with ThreadPoolExecutor(max_workers=1) as pool:
            ddl = pool.submit(experiment.sql, 'TRUNCATE TABLE nstrunc_repro.t1', owner)
            wait_for(lambda: Path(str(control) + '.ready').exists(), 'DDL boundary not reached', 20)
            if fault == 'rollback':
                try:
                    ddl.result(timeout=20)
                except pymysql.MySQLError as error:
                    assert error.args[0] == 4016, error
                else:
                    raise AssertionError('injected rollback returned success')
            visible = roots(experiment, owner_id)
            visible_tablet = tablet_id(experiment, 't1', observer)
            visible_graph = graph(experiment, visible)
            if fault == 'commit_crash':
                assert visible['schema_version'] > before['schema_version'], (before, visible)
                assert visible_tablet != original
                assert original not in visible_graph and visible_tablet in visible_graph
            else:
                assert visible['schema_version'] == before['schema_version'], (before, visible)
                assert visible['catalog_page'] == before['catalog_page']
                assert visible_tablet == original
                assert visible_graph.keys() == old_graph.keys()
                assert visible_graph[original] == old_graph[original]
                # Preparing a child DDL can independently materialize inherited
                # system tablets before its SQL metadata transaction starts.
                # Those committed source changes are valid even if DDL aborts.
                for logical, (value, cap) in visible_graph.items():
                    old_value, old_cap = old_graph[logical]
                    if (value, cap) != (old_value, old_cap):
                        assert value[0] == old_value[0] and value[3:] == old_value[3:]
                        assert value[1] == (1 << 62) | (owner_id << 37) | logical
                        assert old_cap > 0 and cap == 0
                        experiment.record('preparation_materialization', logical=logical, source=value)
            assert original in graph(experiment, roots(experiment, child_id))
            old_physical = physical_id(owner_id, original)
            expected_status = (1, 1, 0) if fault == 'rollback' else (3, int(fault == 'commit_crash'), 0)
            wait_for(lambda: physical_state(experiment, [old_physical]) == ((old_physical,) + expected_status,),
                     'physical DELETE does not match atomic DDL boundary', 5)
            experiment.record('physical_delete_boundary', fault=fault,
                              physical=physical_state(experiment, [old_physical]))
            experiment.record('atomic_boundary', owner=owner_kind, fault=fault,
                schema_before=before['schema_version'], schema_visible=visible['schema_version'],
                old_tablet=original, visible_tablet=visible_tablet, source_count=len(visible_graph))
            if fault != 'rollback':
                experiment.proc.kill()
                experiment.proc.wait(timeout=15)
                try:
                    ddl.result(timeout=15)
                except pymysql.MySQLError as error:
                    assert error.args[0] in (2006, 2013), error
                else:
                    raise AssertionError('DDL completed before controlled crash')
        control.unlink(missing_ok=True)
        if fault != 'rollback':
            for connection in (owner, observer, child, experiment.connection):
                connection.close()
            owner = observer = child = experiment.connection = None
            experiment.start()
            # Check persisted roots before loading the owner Namespace Runtime.
            recovered = roots(experiment, owner_id)
            for key in ('schema_version', 'directory_page', 'catalog_page'):
                assert recovered[key] == visible[key], (key, visible, recovered)
            assert graph(experiment, recovered) == visible_graph
            recovered_status = (3, 1, 0) if fault == 'commit_crash' else (1, 1, 0)
            assert physical_state(experiment, [old_physical]) == ((old_physical,) + recovered_status,)
            child = connect(experiment, 'root@atomic_old')
            assert experiment.sql(query, child) == ((1, expected),)
            owner = connect(experiment, 'root@' + owner_name)
        assert experiment.sql(query, owner) == (() if fault == 'commit_crash' else ((1, expected),))
        assert experiment.sql(query, child) == ((1, expected),)
        if fault != 'commit_crash':
            experiment.sql('TRUNCATE TABLE nstrunc_repro.t1', owner)
        experiment.sql('INSERT INTO nstrunc_repro.t1 VALUES(2,30)', owner)
        experiment.sql('UPDATE nstrunc_repro.t1 SET v=v+100', child)
        assert experiment.sql(query, child) == ((1, expected+100),)
        assert experiment.sql(query, owner) == ((2, 30),)
        experiment.record('PASS', case='atomic_ddl_catalog', owner=owner_kind, fault=fault)
    finally:
        control.unlink(missing_ok=True)
        for connection in (owner, observer, child):
            if connection is not None:
                connection.close()
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--owner', choices=('initial', 'child'), required=True)
    parser.add_argument('--fault', choices=('rollback', 'abort_crash', 'commit_crash'), required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary, args.owner, args.fault)
