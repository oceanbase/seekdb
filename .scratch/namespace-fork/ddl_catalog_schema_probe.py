#!/usr/bin/env python3
"""Compare the published graph with real SQL schema for table-family DDL."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import resource
import time
import pymysql

from ddl_catalog_atomic_probe import roots, graph
from fork_parent_truncate_probe import BootstrapExperiment, connect, namespace_id


def verify(experiment, connection, name):
    state = roots(experiment, namespace_id(experiment, name))
    sources = graph(experiment, state)
    rows = experiment.sql("SELECT table_id,tablet_id,part_level FROM oceanbase.__all_table "
        "WHERE database_id=(SELECT database_id FROM oceanbase.__all_database "
        "WHERE database_name='catalog_ddl')", connection, log=False)
    expected = {}
    for table_id, tablet, level in rows:
        if level == 0:
            if tablet:
                expected[int(tablet)] = int(table_id)
        else:
            table = '__all_part' if level == 1 else '__all_sub_part'
            for item, in experiment.sql(f'SELECT tablet_id FROM oceanbase.{table} WHERE table_id={table_id}',
                                       connection, log=False):
                expected[int(item)] = int(table_id)
    for tablet, table_id in expected.items():
        assert tablet in sources, (name, tablet, table_id)
        value, cap = sources[tablet]
        assert value[0] == table_id, (name, tablet, value, table_id)
        # Every logical main/LOB binding must resolve to the same binding unit.
        for bound in value[3:]:
            if bound:
                assert bound in sources and sources[bound][0][3:] == value[3:], (tablet, bound)
    return state, expected, sources


def run(binary):
    experiment = BootstrapExperiment(binary, 'ddl_catalog_schema', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE catalog_ddl')
        experiment.sql('CREATE TABLE catalog_ddl.t(id INT PRIMARY KEY,v INT,b LONGTEXT) '
                       'PARTITION BY HASH(id) PARTITIONS 8')
        experiment.sql("INSERT INTO catalog_ddl.t VALUES(1,10,REPEAT('x',12000))")
        verify(experiment, experiment.connection, 'ns1')
        experiment.sql('FORK NAMESPACE catalog_owner FROM ns1')
        experiment.sql('FORK NAMESPACE catalog_old FROM catalog_owner')
        with connect(experiment, 'root@catalog_owner') as child:
            state, expected, sources = verify(experiment, child, 'catalog_owner')
            for statement in (
                "ALTER TABLE catalog_ddl.t COMMENT='cold description change'",
                'ALTER TABLE catalog_ddl.t ADD COLUMN extra INT DEFAULT 7',
                'CREATE INDEX by_v ON catalog_ddl.t(v)',
                'DROP INDEX by_v ON catalog_ddl.t',
                'TRUNCATE TABLE catalog_ddl.t',
                'DROP TABLE catalog_ddl.t',
            ):
                experiment.sql(statement, child)
                next_state, next_expected, next_sources = verify(experiment, child, 'catalog_owner')
                assert next_state['schema_version'] > state['schema_version']
                for removed in expected.keys() - next_expected.keys():
                    assert removed not in next_sources, (statement, removed)
                state, expected, sources = next_state, next_expected, next_sources
                experiment.record('case_pass', statement=statement, tablets=len(expected))
        with connect(experiment, 'root@catalog_old') as old:
            assert experiment.sql('SELECT id,v,LENGTH(b) FROM catalog_ddl.t', old) == ((1, 10, 12000),)
            verify(experiment, old, 'catalog_old')
        def create(index):
            with connect(experiment, 'root@catalog_owner') as child:
                for attempt in range(10):
                    try:
                        experiment.sql(f'CREATE TABLE catalog_ddl.parallel_{index}(id INT PRIMARY KEY,b LONGTEXT)', child)
                        return
                    except pymysql.MySQLError as error:
                        # Concurrent DDL can conflict on native transaction locks.
                        # Reject schema-staleness and other publication errors.
                        if error.args[0] != 6005 or attempt == 9:
                            raise
                        assert experiment.sql("SELECT table_id FROM oceanbase.__all_table "
                            f"WHERE table_name='parallel_{index}'", child, log=False) == ()
                        experiment.record('ddl_lock_retry', index=index, attempt=attempt)
                        time.sleep(.05)
        with ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(create, range(4)))
        with connect(experiment, 'root@catalog_owner') as child:
            verify(experiment, child, 'catalog_owner')
            experiment.sql('DROP DATABASE catalog_ddl', child)
            _, dropped, final_sources = verify(experiment, child, 'catalog_owner')
            assert not dropped
        experiment.record('PASS', case='ddl_catalog_schema', partitioned=True,
                          lob_bindings=True, parallel_ddl=4, parent_view_retained=True)
    finally:
        experiment.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args.binary)
