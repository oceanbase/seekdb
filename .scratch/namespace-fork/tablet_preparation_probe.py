#!/usr/bin/env python3
"""Partitioned cold writes/DDL and direct outrow-vector reads across Namespace fork."""
import argparse
import resource
import os
import re
from collections import Counter
from fork_parent_truncate_probe import BootstrapExperiment, connect


def run(binary, serial=False, instrumented=False):
    if instrumented:
        os.environ['SEEKDB_PREPARATION_PROBE'] = '1'
    experiment = BootstrapExperiment(binary, 'tablet_preparation', prototype=6)
    try:
        experiment.start()
        experiment.sql('CREATE DATABASE preparation')
        experiment.sql('CREATE TABLE preparation.parts(id INT PRIMARY KEY,v INT,b LONGTEXT) PARTITION BY HASH(id) PARTITIONS 32')
        values = ','.join(f"({i},{i},REPEAT('x',12000))" for i in range(32))
        experiment.sql('INSERT INTO preparation.parts VALUES ' + values, log=False)
        experiment.sql('CREATE TABLE preparation.vec(id INT PRIMARY KEY,v VECTOR(2048)) LOB_INROW_THRESHOLD=4096')
        vector = '[' + ','.join(['1'] * 2048) + ']'
        doubled = '[' + ','.join(['2'] * 2048) + ']'
        experiment.sql("INSERT INTO preparation.vec VALUES(1,'" + vector + "')", log=False)
        experiment.sql('FORK NAMESPACE preparation_child FROM ns1')
        experiment.sql('FORK NAMESPACE preparation_ddl FROM ns1')
        with connect(experiment, 'root@preparation_ddl') as ddl:
            log = experiment.base / 'log/seekdb.log'
            cold_offset = log.stat().st_size
            experiment.sql('CREATE INDEX by_v ON preparation.parts(v)', ddl)
            assert experiment.sql('SELECT COUNT(*),SUM(v) FROM preparation.parts FORCE INDEX(by_v)', ddl) == ((32,496),)
            if instrumented:
                with log.open('rb') as stream:
                    stream.seek(cold_offset)
                    trace = stream.read().decode(errors='replace')
                descriptions = Counter(re.findall(r'PREPARATION_CATALOG table=(\d+)', trace))
                # Every materialized partition reads exactly one compact table
                # description per member of its main/LOB binding unit.
                assert sum(count == 32 for count in descriptions.values()) >= 3, descriptions
                experiment.record('case_pass', case='cold_ddl_persisted_descriptions', descriptions=dict(descriptions))
            experiment.record('case_pass', case='cold_partitioned_ddl', partitions=32)
        with connect(experiment, 'root@preparation_child') as child:
            assert experiment.sql('SELECT SUM(v),SUM(LENGTH(b)) FROM preparation.parts', child) == ((496,384000),)
            experiment.sql('UPDATE ' + ('/*+ NO_PARALLEL */ ' if serial else '') + 'preparation.parts SET v=v+100', child)
            log = experiment.base / 'log/seekdb.log'
            warm_offset = log.stat().st_size
            experiment.sql('UPDATE ' + ('/*+ NO_PARALLEL */ ' if serial else '') + 'preparation.parts SET v=v+100', child)
            if instrumented:
                with log.open('rb') as stream:
                    stream.seek(warm_offset)
                    trace = stream.read().decode(errors='replace')
                assert 'PREPARATION_CATALOG ' not in trace, 'owned update read creation descriptions'
                assert 'PREPARATION_MAPPING ' not in trace, 'owned update queried materialization mapping'
                experiment.record('case_pass', case='owned_update_without_creation_preparation')
            experiment.sql('CREATE INDEX by_v ON preparation.parts(v)', child)
            assert experiment.sql('SELECT COUNT(*),SUM(v) FROM preparation.parts FORCE INDEX(by_v)', child) == ((32,6896),)
            experiment.sql("ALTER TABLE preparation.parts MODIFY v BIGINT", child)
            assert experiment.sql('SELECT COUNT(*),SUM(v),SUM(LENGTH(b)) FROM preparation.parts', child) == ((32,6896,384000),)
            experiment.sql('BEGIN', child)
            experiment.sql('UPDATE preparation.parts SET v=v+1', child)
            assert experiment.sql('SELECT SUM(v) FROM preparation.parts FORCE INDEX(by_v)', child) == ((6928,),)
            experiment.sql('DELETE FROM preparation.parts WHERE id=0', child)
            assert experiment.sql('SELECT COUNT(*) FROM preparation.parts FORCE INDEX(by_v)', child) == ((31,),)
            experiment.sql('ROLLBACK', child)
            assert experiment.sql('SELECT COUNT(*),SUM(v) FROM preparation.parts FORCE INDEX(by_v)', child) == ((32,6896),)
            assert experiment.sql("SELECT l2_distance(v,'" + vector + "') FROM preparation.vec", child, log=False) == ((0.0,),)
            assert experiment.sql("SELECT l2_distance(vec_vector(v),'" + vector + "') FROM preparation.vec", child, log=False) == ((0.0,),)
            experiment.sql("UPDATE preparation.vec SET v='" + doubled + "'", child, log=False)
            expected = experiment.sql("SELECT l2_distance(vec_vector(v),'" + vector + "') FROM preparation.vec", child, log=False)[0][0]
            assert abs(expected - 2048 ** .5) < 0.001, expected
            assert experiment.sql("SELECT l2_distance(v,'" + vector + "') FROM preparation.vec", log=False) == ((0.0,),)
            if instrumented:
                trace = log.read_text(errors='replace')
                direct = re.findall(r'PREPARATION_LOB_DIRECT bytes=(\d+) ret=0', trace)
                assert any(int(size) >= 8192 for size in direct), direct
                experiment.record('case_pass', case='outrow_vector_caller_buffer', bytes=direct)
            experiment.record('case_pass', case='partitioned_write_ddl_outrow_vector', partitions=32)
        experiment.connection.close()
        experiment.connection = None
        experiment.proc.terminate()
        experiment.proc.wait(timeout=20)
        experiment.start()
        with connect(experiment, 'root@preparation_child') as child:
            assert experiment.sql('SELECT COUNT(*),SUM(v),SUM(LENGTH(b)) FROM preparation.parts', child) == ((32,6896,384000),)
        experiment.record('pass', restart=True)
    finally:
        experiment.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--serial', action='store_true')
    parser.add_argument('--instrumented', action='store_true')
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0,0))
    run(args.binary, args.serial, args.instrumented)
