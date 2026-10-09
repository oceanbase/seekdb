#!/usr/bin/env python3
"""Real DDL/materialization/recovery plus native layout binding race tests."""
import argparse
import os
from pathlib import Path
import re
import resource

from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id


def log_tail(exp, cursors):
    lines = []
    for path in (exp.base / 'log').glob('seekdb.log*'):
        with path.open('rb') as stream:
            stream.seek(cursors.get(path.stat().st_ino, 0))
            lines.extend(line.decode(errors='replace') for line in stream
                         if b'TABLE_LAYOUT_' in line or b'TABLET_LAYOUT_' in line)
    return ''.join(lines)


def definitions(exp, connection):
    return {int(table): int(version) for table, version in exp.sql(
        "SELECT table_id,schema_version FROM oceanbase.__all_table WHERE database_id="
        "(SELECT database_id FROM oceanbase.__all_database WHERE database_name='layout_sql')",
        connection, log=False)}


def run(binary):
    os.environ['SEEKDB_TABLE_LAYOUT_PROBE'] = '1'
    exp = BootstrapExperiment(binary, 'table_storage_layout', prototype=6)
    try:
        exp.start()
        initial = log_tail(exp, {})
        assert 'TABLE_LAYOUT_PASS ' in initial and 'TABLE_LAYOUT_FAIL' not in initial, initial[-5000:]
        assert 'retire_mvcc=1 cross_store_atomic=1 exact_without_head=1' in initial
        initial_physical = re.findall(r'TABLET_LAYOUT_AUDIT tablet=(\d+) table=(\d+) layout=(\d+)', initial)
        assert initial_physical and all(int(row[2]) > 0 for row in initial_physical), initial_physical
        exp.sql('CREATE DATABASE layout_sql')
        exp.sql('CREATE TABLE layout_sql.t(id INT PRIMARY KEY,v INT,b LONGTEXT) '
                'PARTITION BY HASH(id) PARTITIONS 8')
        exp.sql("INSERT INTO layout_sql.t VALUES(1,10,REPEAT('x',12000))")
        exp.sql('FORK NAMESPACE layout_child FROM ns1')
        child_id = namespace_id(exp, 'layout_child')
        with connect(exp, 'root@layout_child') as child:
            exp.sql("ALTER TABLE layout_sql.t COMMENT='independent child layout'", child)
            exp.sql('UPDATE layout_sql.t SET v=11 WHERE id=1', child)
            exp.sql('ALTER TABLE layout_sql.t ADD COLUMN extra INT DEFAULT 7', child)
            # Force additional partitions after the child's layout has advanced.
            for key in (0, 2, 3, 4, 5, 6, 7):
                exp.sql(f"INSERT INTO layout_sql.t(id,v,b) VALUES({key},20,REPEAT('y',12000))", child)
            exp.sql('CREATE INDEX by_v ON layout_sql.t(v)', child)
            assert exp.sql('SELECT COUNT(*),SUM(extra) FROM layout_sql.t', child) == ((8, 56),)
            child_defs = definitions(exp, child)
        exp.sql('ALTER TABLE layout_sql.t ADD COLUMN parent_only INT DEFAULT 9')
        parent_defs = definitions(exp, exp.connection)
        assert exp.sql('SELECT id,v,parent_only FROM layout_sql.t') == ((1, 10, 9),)
        expected = {(1, table): version for table, version in parent_defs.items()}
        expected.update({(child_id, table): version for table, version in child_defs.items()})
        cursors = {p.stat().st_ino: p.stat().st_size for p in (exp.base / 'log').glob('seekdb.log*')}
        exp.connection.close()
        exp.connection = None
        exp.proc.kill()
        exp.proc.wait(timeout=15)
        exp.start()
        recovered = log_tail(exp, cursors)
        assert 'TABLE_LAYOUT_AUDIT_END ' in recovered and 'ret=0' in recovered, recovered[-5000:]
        bindings = {(int(ns), int(table)): (int(layout), int(version))
                    for ns, table, layout, version in re.findall(
                        r'TABLE_LAYOUT_AUDIT ns=(\d+) table=(\d+) layout=(\d+) version=(\d+) ret=0', recovered)}
        for pair, version in expected.items():
            assert pair in bindings, pair
            assert bindings[pair][1] == version, (pair, version, bindings[pair])
        for table in parent_defs.keys() & child_defs.keys():
            assert bindings[1, table][0] != bindings[child_id, table][0], table
        physical = re.findall(r'TABLET_LAYOUT_AUDIT tablet=(\d+) table=(\d+) layout=(\d+)', recovered)
        checked = 0
        partition_counts = {}
        for tablet, table, layout in physical:
            tablet, table, layout = int(tablet), int(table), int(layout)
            owner = (tablet >> 37) & ((1 << 25) - 1) if tablet & (1 << 62) else 1
            pair = owner, table
            if pair in expected:
                assert layout == bindings[pair][0] and layout > 0, (pair, tablet, layout, bindings[pair])
                partition_counts[pair] = partition_counts.get(pair, 0) + 1
                checked += 1
        assert checked >= 16 and max(partition_counts.values()) >= 8, (checked, partition_counts)
        with connect(exp, 'root@layout_child') as child:
            assert exp.sql('SELECT COUNT(*),SUM(extra) FROM layout_sql.t', child) == ((8, 56),)
            assert exp.sql('SELECT v FROM layout_sql.t WHERE id=1', child) == ((11,),)
        assert exp.sql('SELECT id,v,parent_only FROM layout_sql.t') == ((1, 10, 9),)
        # DROP retires ownership, while descendants can still use old sources.
        exp.sql('FORK NAMESPACE layout_grandchild FROM layout_child')
        exp.sql('SET recyclebin=off')
        exp.sql('DROP TABLE layout_sql.t')
        exp.sql('DROP NAMESPACE layout_child')
        cursors = {p.stat().st_ino: p.stat().st_size for p in (exp.base / 'log').glob('seekdb.log*')}
        exp.connection.close()
        exp.connection = None
        exp.proc.kill()
        exp.proc.wait(timeout=15)
        exp.start()
        retired = log_tail(exp, cursors)
        remaining = {(int(ns), int(table)) for ns, table in re.findall(
            r'TABLE_LAYOUT_AUDIT ns=(\d+) table=(\d+)', retired)}
        assert 'TABLE_LAYOUT_AUDIT_END ' in retired, retired[-5000:]
        assert not any(ns == child_id for ns, _ in remaining), remaining
        assert all((1, table) not in remaining for table in parent_defs), (remaining, parent_defs)
        with connect(exp, 'root@layout_grandchild') as grandchild:
            assert exp.sql('SELECT COUNT(*),SUM(extra) FROM layout_sql.t', grandchild) == ((8, 56),)
            exp.sql('UPDATE layout_sql.t SET v=77 WHERE id=1', grandchild)
            assert exp.sql('SELECT v FROM layout_sql.t WHERE id=1', grandchild) == ((77,),)
        exp.record('PASS', case='table_storage_layout', owner_definitions=len(expected),
                   checked_physical=checked, parent_child_independent=True,
                   late_partition_no_overwrite=True, index_and_lob=True,
                   conflict_nowait=True, rollback=True, participant_lifetime=True, crash_recovery=True,
                   retire_mvcc=True, cross_store_atomic=True, exact_without_head=True,
                   drop_table=True, drop_namespace=True, surviving_descendant=True)
    finally:
        exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
