#!/usr/bin/env python3
"""Compare JSON SQL latency using identical task-owned copies of an existing fixture."""

import argparse
import hashlib
import json
import os
import re
import signal
import statistics
import subprocess
import time
import uuid
from pathlib import Path

import pymysql


def process_memory(pid):
    if not pid:
        return {}
    values = {}
    for line in Path('/proc', str(pid), 'status').read_text().splitlines():
        name, _, value = line.partition(':')
        if name in ['VmRSS', 'VmHWM', 'RssAnon']:
            values[name + '_kib'] = int(value.split()[0])
        elif name == 'Cpus_allowed_list':
            values[name] = value.strip()
    return values


def process_cpu(pid):
    if not pid:
        return 0.0
    fields = Path('/proc', str(pid), 'stat').read_text().split()
    return (int(fields[13]) + int(fields[14])) * 1000 / os.sysconf('SC_CLK_TCK')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-port', required=True, type=int)
    parser.add_argument('--candidate-port', required=True, type=int)
    parser.add_argument('--baseline-pid', type=int)
    parser.add_argument('--candidate-pid', type=int)
    parser.add_argument('--baseline-table', default='json_rt')
    parser.add_argument('--source-port', type=int)
    parser.add_argument('--rows', type=int, default=100000)
    parser.add_argument('--rounds', type=int, default=7)
    parser.add_argument('--query', action='append')
    parser.add_argument('--freeze', action='store_true')
    parser.add_argument('--profile-dir', type=Path)
    args = parser.parse_args()
    if not re.fullmatch('[a-zA-Z0-9_]+', args.baseline_table):
        parser.error('baseline table must be an unqualified SQL identifier')
    if args.rows < 1 or args.rounds < 5:
        parser.error('rows must be positive and at least five rounds are required')
    connections = [pymysql.connect(host='127.0.0.1', port=port, user='root', database='test',
                                    autocommit=True, read_timeout=180, write_timeout=180)
                   for port in [args.baseline_port, args.candidate_port]]
    source_connection = pymysql.connect(host='127.0.0.1', port=args.source_port or args.baseline_port,
                                       user='root', database='test')
    target = 'json_domain_bench_' + uuid.uuid4().hex[:16]
    tables = [target + '_0', target + '_1']
    created = []
    fixture_hash = hashlib.sha256()
    copied = 0
    samples = {}
    cpu_samples = {}
    expected = {}
    errors = []
    memory = {'before': [process_memory(args.baseline_pid), process_memory(args.candidate_pid)]}
    queries = {
        'scan': 'select sum(length(doc)) from {table}',
        'valid': 'select sum(json_valid(doc)) from {table}',
        'extract': "select sum(cast(json_extract(doc,'$.id') as signed)) from {table}",
        'schema': '''select sum(json_schema_valid('{"type":"object","required":["id"]}',doc)) from {table}''',
        'set': "select sum(cast(json_extract(json_set(doc,'$.id',id+1),'$.id') as signed)) from {table}",
        'merge': '''select sum(cast(json_extract(json_merge_patch(doc,'{"active":false}'),'$.id') as signed)) from {table}''',
        'construct': "select sum(json_length(json_object('a',id,'b',doc))) from {table}",
        'table': "select sum(j.v) from {table} t,json_table(t.doc,'$' columns(v bigint path '$.id')) j",
        'group': 'select count(*) from (select cast(doc as json) j from (select doc from {table} order by id limit 10000) inputs group by j) g',
        'aggregate': 'select json_length(json_arrayagg(cast(doc as json))),json_storage_size(json_arrayagg(cast(doc as json))) from {table}',
    }
    selected = args.query or list(queries)
    if set(selected) - queries.keys():
        parser.error('unknown query name')

    def run(side, name):
        # Substitute only the identifier, leaving JSON object braces intact.
        sql = queries[name].replace('{table}', tables[side])
        start = time.perf_counter_ns()
        cpu_start = process_cpu([args.baseline_pid, args.candidate_pid][side])
        try:
            with connections[side].cursor() as cursor:
                cursor.execute(sql)
                rows = cursor.fetchall()
            elapsed = (time.perf_counter_ns() - start) / 1e6
            if name in expected and rows != expected[name]:
                raise RuntimeError('result mismatch: ' + repr((name, side, rows, expected[name])))
            expected.setdefault(name, rows)
            return elapsed, process_cpu([args.baseline_pid, args.candidate_pid][side]) - cpu_start
        except Exception as error:
            errors.append({'side': side, 'query': name, 'error': str(error)})
            raise

    try:
        with source_connection.cursor() as cursor:
            cursor.execute('show create table `' + args.baseline_table + '`')
            definition = cursor.fetchone()[1]
            cursor.execute('select count(*) from `' + args.baseline_table + '`')
            if cursor.fetchone()[0] != args.rows:
                raise RuntimeError('baseline row count differs from requested rows')
        for side, connection in enumerate(connections):
            with connection.cursor() as cursor:
                cursor.execute(definition.replace('`' + args.baseline_table + '`', '`' + tables[side] + '`', 1))
                created.append(side)
        with source_connection.cursor(pymysql.cursors.SSCursor) as source:
            source.execute('select id,doc from `' + args.baseline_table + '` order by id')
            while True:
                rows = source.fetchmany(1000)
                if not rows:
                    break
                for identifier, document in rows:
                    fixture_hash.update(str(identifier).encode() + b'\0' + document.encode() + b'\n')
                for side, connection in enumerate(connections):
                    with connection.cursor() as cursor:
                        cursor.executemany('insert into `' + tables[side] + '`(id,doc) values(%s,%s)', rows)
                copied += len(rows)
        if copied != args.rows:
            raise RuntimeError('fixture copy is incomplete')
        for connection in connections:
            with connection.cursor() as cursor:
                cursor.execute('set session ob_query_timeout=120000000')
        if args.freeze:
            for connection in connections:
                with connection.cursor() as cursor:
                    cursor.execute('alter system minor freeze')
            # Latency measurement starts after two complete warmups on both sides.
        print(json.dumps({'fixture_rows': copied, 'fixture_sha256': fixture_hash.hexdigest(),
                          'tables': tables, 'freeze_requested': args.freeze}), flush=True)
        memory['before'] = [process_memory(args.baseline_pid), process_memory(args.candidate_pid)]
        for name in selected:
            for side in range(2):
                with connections[side].cursor() as cursor:
                    cursor.execute('explain ' + queries[name].replace('{table}', tables[side]))
                    print(json.dumps({'plan': name, 'side': side, 'rows': cursor.fetchall()}, default=str), flush=True)
            for _ in range(2):
                for side in range(2):
                    run(side, name)
            samples[name] = [[], []]
            cpu_samples[name] = [[], []]
            for iteration in range(args.rounds):
                for side in ([0, 1] if iteration % 2 == 0 else [1, 0]):
                    elapsed, cpu = run(side, name)
                    samples[name][side].append(elapsed)
                    cpu_samples[name][side].append(cpu)
                    print(json.dumps({'query': name, 'round': iteration + 1, 'side': side,
                                      'milliseconds': elapsed, 'process_cpu_ms': cpu,
                                      'rows': min(copied, 10000) if name == 'group' else copied}), flush=True)
        memory['after'] = [process_memory(args.baseline_pid), process_memory(args.candidate_pid)]
        summary = {}
        for name, sides in samples.items():
            summary[name] = [{'median_ms': statistics.median(values), 'min_ms': min(values),
                              'max_ms': max(values), 'samples_ms': values} for values in sides]
        print(json.dumps({'summary': summary, 'cpu_samples_ms': cpu_samples, 'memory': memory, 'errors': errors,
                          'verified_results': expected}, default=str), flush=True)
        if args.profile_dir:
            args.profile_dir.mkdir(parents=True, exist_ok=True)
            for side, pid in enumerate([args.baseline_pid, args.candidate_pid]):
                if not pid:
                    parser.error('profiling requires both server PIDs')
                output = args.profile_dir / ('side' + str(side) + '.data')
                profiler = subprocess.Popen(['perf', 'record', '-q', '-F', '199', '-g', '-p', str(pid), '-o', str(output)])
                try:
                    deadline = time.monotonic() + 4
                    while time.monotonic() < deadline:
                        for name in selected:
                            run(side, name)
                finally:
                    profiler.send_signal(signal.SIGINT)
                    if profiler.wait() not in [0, 130, -signal.SIGINT]:
                        raise RuntimeError('perf record failed for side ' + str(side))
    finally:
        source_connection.close()
        for side in created:
            with connections[side].cursor() as cursor:
                cursor.execute('drop table `' + tables[side] + '`')
        for connection in connections:
            connection.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
