#!/usr/bin/env python3
"""Measure fork -> first client read/write on fresh namespaces without intervening diagnostics."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import statistics
import subprocess
import time
import pymysql
from fork_parent_truncate_probe import BootstrapExperiment, physical_id


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def query(connection, statement):
    with connection.cursor() as cur:
        cur.execute(statement)
        return cur.fetchall() if cur.description else ()


def run(args):
    exp = BootstrapExperiment(args.binary, 'fork_service_latency', prototype=6)
    exp.extra_parameters = [('memory_budget', '8G'), ('datafile_size', '512M'),
                            ('datafile_maxsize', '2G'), ('ob_compaction_schedule_interval', '5m')]
    binary = str(Path(args.binary).resolve())
    samples = []
    result = dict(binary=binary, binary_sha256=digest(binary),
                  source_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                  partitions=args.partitions, rows=args.rows, samples=args.samples,
                  cpu_count=4, memory_budget='8G', background_maintenance=True,
                  point_read='SELECT id,v FROM latency_fork.t WHERE id=1',
                  first_write='UPDATE latency_fork.t SET v=11 WHERE id=1 (autocommit)',
                  all_partition_read='SELECT COUNT(*),SUM(v) FROM latency_fork.t',
                  base=str(exp.base), host_loadavg_before=os.getloadavg())
    try:
        started = time.perf_counter()
        exp.start()
        result['instance_bootstrap_s'] = time.perf_counter() - started
        exp.connection._read_timeout = 300
        exp.sql('SET ob_query_timeout=300000000')
        exp.sql('SET ob_trx_timeout=300000000')
        exp.sql('CREATE DATABASE latency_fork')
        started = time.perf_counter()
        exp.sql(f'CREATE TABLE latency_fork.t(id INT PRIMARY KEY,v INT) PARTITION BY HASH(id) PARTITIONS {args.partitions}')
        result['create_table_s'] = time.perf_counter() - started
        started = time.perf_counter()
        with exp.connection.cursor() as cur:
            for offset in range(0, args.rows, 500):
                cur.executemany('INSERT INTO latency_fork.t VALUES(%s,%s)',
                                [(i, 10) for i in range(offset, min(offset + 500, args.rows))])
        result['load_rows_s'] = time.perf_counter() - started
        table_id = int(query(exp.connection, "SELECT table_id FROM oceanbase.__all_table WHERE "
                      "table_name='t' AND database_id=(SELECT database_id FROM oceanbase.__all_database "
                      "WHERE database_name='latency_fork')")[0][0])
        tablets = [int(row[0]) for row in query(exp.connection,
                   f'SELECT tablet_id FROM oceanbase.__all_part WHERE table_id={table_id}')]
        assert len(set(tablets)) == args.partitions
        physical = {int(row[0]) for row in query(exp.connection,
                    'SELECT tablet_id FROM oceanbase.__all_virtual_tablet_info WHERE is_empty_shell=0')}
        assert all(physical_id(1, tablet) in physical for tablet in tablets)
        result['verified_table_physical_tablets'] = len(tablets)
        exp.record('fixture_ready', **result)

        for index in range(args.samples):
            name = f'latency_child_{index}'
            child = None
            try:
                # No inventory/catalog SQL or event logging between these clocks.
                begin = time.perf_counter()
                query(exp.connection, f'FORK NAMESPACE {name} FROM ns1')
                fork_done = time.perf_counter()
                child = pymysql.connect(host='127.0.0.1', port=exp.port, user='root@' + name,
                                        password='', autocommit=True, connect_timeout=30,
                                        read_timeout=300, write_timeout=30)
                login_done = time.perf_counter()
                first = query(child, 'SELECT id,v FROM latency_fork.t WHERE id=1')
                read_done = time.perf_counter()
                assert first == ((1, 10),), first
                query(child, 'UPDATE latency_fork.t SET v=11 WHERE id=1')
                write_done = time.perf_counter()
                verified = query(child, 'SELECT id,v FROM latency_fork.t WHERE id=1')
                verify_done = time.perf_counter()
                assert verified == ((1, 11),), verified
                # The full scan is separate from time-to-first-service. Give it
                # an explicit statement deadline without affecting earlier clocks.
                query(child, 'SET ob_query_timeout=300000000')
                scan_start = time.perf_counter()
                aggregate = query(child, 'SELECT COUNT(*),SUM(v) FROM latency_fork.t')
                scan_done = time.perf_counter()
                assert tuple(map(int, aggregate[0])) == (args.rows, args.rows * 10 + 1), aggregate
                assert query(exp.connection, 'SELECT id,v FROM latency_fork.t WHERE id=1') == ((1, 10),)
                sample = dict(namespace=name, fork_s=fork_done-begin,
                              first_login_s=login_done-fork_done, first_point_read_s=read_done-login_done,
                              first_write_commit_s=write_done-read_done, read_after_write_s=verify_done-write_done,
                              fork_to_read_s=read_done-begin, fork_to_write_commit_s=write_done-begin,
                              all_partition_scan_s=scan_done-scan_start)
                samples.append(sample)
                exp.record('latency_sample', **sample)
            finally:
                if child is not None: child.close()
        result['measurements'] = samples
        fields = [key for key in samples[0] if key.endswith('_s')]
        result['summary_seconds'] = {key: dict(min=min(row[key] for row in samples),
             median=statistics.median(row[key] for row in samples), max=max(row[key] for row in samples))
             for key in fields}
        result['host_loadavg_after'] = os.getloadavg()
        result['result'] = 'PASS'
        args.result.parent.mkdir(parents=True, exist_ok=True)
        args.result.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
        exp.record('PASS', case='fork_service_latency', result=str(args.result),
                   summary_seconds=result['summary_seconds'])
    finally:
        exp.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--partitions', type=int, default=8000)
    parser.add_argument('--rows', type=int, default=8000)
    parser.add_argument('--samples', type=int, default=5)
    parser.add_argument('--result', type=Path, default=Path(__file__).with_name('fork-service-latency-results.json'))
    args = parser.parse_args()
    if args.partitions < 1 or args.rows < 2 or args.samples < 1:
        parser.error('partitions/samples must be positive and rows must be at least 2')
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run(args)
