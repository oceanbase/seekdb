#!/usr/bin/env python3
"""seekdb SQL build/recall diagnostic. Never starts or stops any service.

Only existing NumPy plus existing PyMySQL (or mysql CLI) are used. Smoke is
diagnostic ONLY: lib=cuvs and a successful first query are not GPU-build proof.
Formal mode additionally requires an image-verified completion/proof command.
"""
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys
import time
import traceback

import numpy as np


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False,
                                    default=str, allow_nan=False) + '\n', encoding='utf-8')


def ident(value):
    if not re.fullmatch(r'[a-z][a-z0-9_]{0,63}', value):
        raise ValueError('Unsafe SQL identifier: ' + repr(value))
    return '`' + value + '`'


def vector_literal(value):
    # Inputs are already verified numeric JSON; no six-decimal reformatting.
    parsed = json.loads(value)
    if not isinstance(parsed, list) or not all(type(x) in (int, float) for x in parsed):
        raise ValueError('Expected numeric vector JSON')
    if not np.isfinite(np.asarray(parsed, dtype=np.float64)).all():
        raise ValueError('Nonfinite vector text')
    if not re.fullmatch(r'[\[\]0-9eE+.,\-\s]+', value):
        raise ValueError('Unexpected vector text characters')
    return "'" + value + "'"


class SQL:
    def __init__(self, args, output):
        self.args, self.output, self.number = args, output, 0
        self.init_command = 'SET SESSION ob_hnsw_ef_search=%d' % args.ef_search
        self.session_check = 'SELECT @@session.ob_hnsw_ef_search'
        self.driver = args.driver
        if self.driver == 'auto':
            self.driver = 'pymysql' if importlib.util.find_spec('pymysql') else 'mysql'
        self.connection = None
        if self.driver == 'pymysql':
            import pymysql
            self.connection = pymysql.connect(host=args.host, port=args.port,
                user=args.user, password=os.environ.get(args.password_env, ''),
                charset='utf8mb4', autocommit=True, connect_timeout=10,
                read_timeout=args.sql_timeout, write_timeout=args.sql_timeout,
                init_command=self.init_command)
        elif not shutil.which(args.mysql):
            raise RuntimeError('No existing pymysql/mysql CLI; do not install dependencies')
        self.events = (output / 'sql-events.jsonl').open('a', encoding='utf-8')

    def execute(self, statement, *, keep_rows=True):
        self.number += 1
        stem = self.output / ('sql-%06d' % self.number)
        script = self.session_check + ';\n' + statement + ';\n'
        stem.with_suffix('.sql').write_text(script, encoding='utf-8')
        event = {'number': self.number, 'started_utc': utc(), 'sql': stem.name + '.sql',
                 'session_init_command': self.init_command}
        started = time.monotonic()
        try:
            if self.driver == 'pymysql':
                with self.connection.cursor() as cursor:
                    cursor.execute(self.session_check)
                    session_rows = list(cursor.fetchall())
                    self.check_session(session_rows, event, stem)
                    cursor.execute(statement)
                    rows = list(cursor.fetchall())
            else:
                env = os.environ.copy()
                password = env.get(self.args.password_env)
                if password is not None:
                    env['MYSQL_PWD'] = password
                argv = [self.args.mysql, '--protocol=TCP', '-h', self.args.host,
                    '-P', str(self.args.port), '-u', self.args.user,
                    '--default-character-set=utf8mb4', '--batch', '--raw',
                    '--skip-column-names', '--init-command=' + self.init_command]
                result = subprocess.run(argv, input=script, text=True,
                    encoding='utf-8', errors='strict', capture_output=True,
                    timeout=self.args.sql_timeout, env=env)
                stem.with_suffix('.stderr.txt').write_text(result.stderr, encoding='utf-8')
                if result.returncode:
                    stem.with_suffix('.stdout.txt').write_text(result.stdout, encoding='utf-8')
                    raise RuntimeError('mysql exit %d: %s' % (result.returncode, result.stderr))
                rows = [line.split('\t') for line in result.stdout.splitlines() if line]
                self.check_session(rows[:1], event, stem)
                rows = rows[1:]
            event.update(status='ok', row_count=len(rows))
            if keep_rows:
                write_json(stem.with_suffix('.rows.json'), rows)
            return rows
        except BaseException as error:
            event.update(status='failed', error=type(error).__name__ + ': ' + str(error))
            raise
        finally:
            event['elapsed_sec'] = time.monotonic() - started
            self.events.write(json.dumps(event, ensure_ascii=False) + '\n')
            self.events.flush()

    def check_session(self, rows, event, stem):
        actual = None
        if len(rows) == 1 and len(rows[0]) == 1:
            actual = int(rows[0][0])
        receipt = {'driver': self.driver, 'init_command': self.init_command,
                   'requested_ef_search': self.args.ef_search,
                   'actual_session_ob_hnsw_ef_search': actual,
                   'matches': actual == self.args.ef_search}
        write_json(stem.with_suffix('.session.json'), receipt)
        event['actual_session_ob_hnsw_ef_search'] = actual
        if actual != self.args.ef_search:
            raise ValueError('Connection ef_search differs from requested value: ' + str(actual))

    def close(self):
        if self.connection:
            self.connection.close()
        self.events.close()


def inputs(gt_dir, base_override=None, query_override=None):
    manifest = json.loads((gt_dir / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('status') != 'complete' or manifest.get('metric') != 'l2':
        raise ValueError('Require completed actual L2 ground-truth manifest')
    for name in ('dimension', 'base_count', 'query_count', 'k'):
        if type(manifest.get(name)) is not int or manifest[name] <= 0:
            raise ValueError('Invalid manifest ' + name)
    dim, n, q, k = [manifest[x] for x in ('dimension', 'base_count', 'query_count', 'k')]
    if k > n or manifest.get('recall_targets') != q * k:
        raise ValueError('Invalid complete recall denominator')
    arrays, texts = {}, {}
    for label, count, override in [('base', n, base_override), ('query', q, query_override)]:
        raw = Path(override or manifest['inputs'][label]['path'])
        if raw.stat().st_size != count * dim * 4 or sha256(raw) != manifest['inputs'][label]['sha256']:
            raise ValueError('Actual raw file shape/hash mismatch: ' + label)
        array = np.memmap(raw, mode='r', dtype='<f4', shape=(count, dim))
        if not np.isfinite(array).all():
            raise ValueError('Nonfinite raw data: ' + label)
        path = gt_dir / (label + '.sql-vectors.txt')
        if sha256(path) != manifest['artifacts'][path.name]['sha256']:
            raise ValueError('SQL vector text hash mismatch: ' + label)
        text = path.read_text(encoding='utf-8').splitlines()
        if len(text) != count:
            raise ValueError('SQL vector line count mismatch: ' + label)
        for i, line in enumerate(text):
            vector_literal(line)
            actual = np.asarray(json.loads(line), dtype='<f4')
            if actual.shape != (dim,) or not np.array_equal(actual.view('<u4'), array[i].view('<u4')):
                raise ValueError('SQL text not bitwise raw float32: %s row %d' % (label, i))
        arrays[label], texts[label] = array, text
    gold_path = gt_dir / 'gold.json'
    if sha256(gold_path) != manifest['artifacts']['gold.json']['sha256']:
        raise ValueError('Ground-truth hash mismatch')
    gold = json.loads(gold_path.read_text(encoding='utf-8'))
    recall(gold, gold, k, n, q)
    return manifest, arrays, texts, gold


def recall(gold, predictions, k, n, q):
    expected_keys = {str(i) for i in range(q)}
    if set(gold) != expected_keys or set(predictions) != expected_keys:
        raise ValueError('Missing/extra query IDs; every physical query enters denominator')
    counts = {}
    for key in sorted(expected_keys, key=int):
        for values in (gold[key], predictions[key]):
            if len(values) != k or len(set(values)) != k:
                raise ValueError('Need exactly K unique IDs for query ' + key)
            if any(type(x) is not int or not 0 <= x < n for x in values):
                raise ValueError('Invalid zero-based ID mapping: ' + key)
        counts[key] = len(set(gold[key]) & set(predictions[key]))
    hits = sum(counts.values())
    return {'hits': hits, 'targets': q * k, 'pooled': hits / (q * k),
        'minimum_query': min(counts.values()) / k, 'query_count': q, 'hits_per_query': counts}


def load_and_check(sql, table, vectors, raw, batch, output):
    n, dim = raw.shape
    sql.execute('CREATE TABLE %s (c1 INT PRIMARY KEY, c2 VECTOR(%d))' % (table, dim))
    for start in range(0, n, batch):
        values = ','.join('(%d,%s)' % (i, vector_literal(vectors[i]))
                          for i in range(start, min(n, start + batch)))
        sql.execute('INSERT INTO %s VALUES %s' % (table, values), keep_rows=False)
    count = sql.execute('SELECT COUNT(*), MIN(c1), MAX(c1) FROM ' + table)
    if tuple(map(int, count[0])) != (n, 0, n - 1):
        raise ValueError('Incomplete table or ID range: ' + table)
    checked = 0
    readback_hash = hashlib.sha256()
    with output.open('w', encoding='utf-8') as receipt:
        for start in range(0, n, batch):
            # The image's native VECTOR printer emits only ~6 significant digits.
            # ARRAY_MAP promotes actual stored float32 values to DOUBLE before the
            # ARRAY(double) printer runs. This exact expression was checked on the
            # real 1.4.0 image; the comparison below still requires every float32 bit.
            rows = sql.execute('SELECT c1,array_map(x -> CAST(x AS DOUBLE),c2) FROM %s ORDER BY c1 LIMIT %d OFFSET %d' %
                               (table, min(batch, n - start), start), keep_rows=False)
            for offset, (row_id, value) in enumerate(rows):
                row_id = int(row_id)
                if row_id != start + offset:
                    raise ValueError('Database readback ID discontinuity')
                if isinstance(value, bytes):
                    value = value.decode('utf-8')
                receipt.write(json.dumps({'id': row_id, 'vector': value}) + '\n')
                # No tolerance or decimal rounding is applied to the actual values.
                actual = np.asarray(json.loads(value), dtype='<f4')
                if actual.shape != (dim,) or not np.array_equal(actual.view('<u4'), raw[row_id].view('<u4')):
                    raise ValueError('Database vector not bitwise raw float32: %s row %d' % (table, row_id))
                readback_hash.update(actual.tobytes())
                checked += 1
            if len(rows) != min(batch, n - start):
                raise ValueError('Missing readback rows')
    return {'rows': checked, 'float32_values': checked * dim, 'bitwise_equal': True,
            'sql_readback_expression': 'array_map(x -> CAST(x AS DOUBLE),c2)',
            'readback_float32_sha256': readback_hash.hexdigest()}


def snapshot_logs(paths):
    return [{'path': str(p), 'offset': p.stat().st_size,
             'inode': p.stat().st_ino} for p in paths]


def finish_logs(before, output):
    result = []
    for number, item in enumerate(before):
        path = Path(item['path'])
        stat = path.stat()
        if stat.st_ino != item['inode'] or stat.st_size < item['offset']:
            result.append(dict(item, status='rotated_or_truncated', end_size=stat.st_size))
            continue
        target = output / ('server-log-%02d.delta' % number)
        with path.open('rb') as source, target.open('wb') as destination:
            source.seek(item['offset'])
            remaining = stat.st_size - item['offset']
            while remaining:
                block = source.read(min(1048576, remaining))
                if not block:
                    raise RuntimeError('Server log changed during capture')
                destination.write(block)
                remaining -= len(block)
        result.append(dict(item, status='captured', end_offset=stat.st_size,
                           artifact=str(target), sha256=sha256(target)))
    return result


def completion_proof(contract, context, output, timeout):
    argv = [part.format_map(context) for part in contract['completion_command']]
    write_json(output / 'proof-command.json', argv)
    result = subprocess.run(argv, capture_output=True, text=True, encoding='utf-8',
                            errors='strict', timeout=timeout)
    (output / 'proof.stdout.txt').write_text(result.stdout, encoding='utf-8')
    (output / 'proof.stderr.txt').write_text(result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError('Completion/proof command failed, not a GPU result')
    proof = json.loads(result.stdout)
    for key in ('schema', 'table', 'index', 'backend'):
        if proof.get(key) != context[key]:
            raise ValueError('Completion proof identity mismatch: ' + key)
    for key in ('complete_index_ready', 'no_lazy_work_remaining', 'index_used', 'hnsw_semantics'):
        if proof.get(key) is not True:
            raise ValueError('Completion proof missing true ' + key)
    if proof.get('indexed_rows') != context['base_count'] or proof.get('cpu_fallback') is not False:
        raise ValueError('Incomplete index or CPU fallback not excluded')
    if context['backend'] == 'gpu':
        gpu = proof.get('gpu_build_evidence')
        if (proof.get('gpu_build_participation') is not True or not isinstance(gpu, dict)
                or gpu.get('method') != 'source_and_real_api_phase_log'
                or gpu.get('gpu_apis') != ['cuvsCagraBuild', 'cuvsHnswFromCagra', 'cuvsHnswSerialize']
                or not re.fullmatch(r'[0-9a-f]{64}', str(gpu.get('source_sha256', '')))
                or not re.fullmatch(r'[0-9a-f]{64}', str(gpu.get('bridge_library_sha256', '')))
                or not gpu.get('gpu_arch') or not isinstance(gpu.get('phase_log_sha256'), list)
                or not gpu['phase_log_sha256']
                or not all(re.fullmatch(r'[0-9a-f]{64}', str(x)) for x in gpu['phase_log_sha256'])
                or gpu.get('synchronized_full_hnsw_and_caller_fd_snapshot') is not True):
            raise ValueError('No source-correlated real GPU API/completion evidence')
    if not proof.get('evidence'):
        raise ValueError('No raw correlated readiness/build evidence')
    for item in proof['evidence']:
        path = Path(item['path']).resolve()
        if output.resolve() not in path.parents or not path.is_file() or sha256(path) != item['sha256']:
            raise ValueError('Raw proof must exist under this round with matching SHA256')
    if context['backend'] == 'gpu':
        evidence_hashes = {item['sha256'] for item in proof['evidence']}
        if (gpu['source_sha256'] not in evidence_hashes or
                not set(gpu['phase_log_sha256']).issubset(evidence_hashes)):
            raise ValueError('GPU source/phase hashes do not reference the saved raw evidence')
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gt-dir', type=Path, required=True)
    parser.add_argument('--base', help='Optional relocated raw file, must match manifest SHA256')
    parser.add_argument('--query', help='Optional relocated raw file, must match manifest SHA256')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=['smoke', 'formal'], default='smoke')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=2881)
    parser.add_argument('--user', default='root')
    parser.add_argument('--password-env', default='SEEKDB_PASSWORD')
    parser.add_argument('--driver', choices=['auto', 'pymysql', 'mysql'], default='auto')
    parser.add_argument('--mysql', default='mysql')
    parser.add_argument('--schema', help='NEW schema, must not already exist')
    parser.add_argument('--batch-rows', type=int, default=250)
    parser.add_argument('--sql-timeout', type=int, default=900)
    parser.add_argument('--m', type=int, default=16)
    parser.add_argument('--ef-construction', type=int, default=200)
    parser.add_argument('--ef-search', type=int, default=64)
    parser.add_argument('--server-log', type=Path, action='append', default=[])
    parser.add_argument('--verified-contract', type=Path,
        help='Formal-only explicit image-verified completion command; no default guessed marker')
    parser.add_argument('--recall-threshold', type=float,
        help='Formal quality gate, explicitly supplied from verified rules/config')
    args = parser.parse_args()
    if min(args.batch_rows, args.sql_timeout, args.m, args.ef_construction, args.ef_search) <= 0:
        parser.error('Batch/timeout/index parameters must be positive')
    if args.ef_search > 160000:
        parser.error('Session ob_hnsw_ef_search maximum is 160000 in verified seekdb source')
    contract = None
    if args.mode == 'formal':
        if not args.verified_contract or args.recall_threshold is None or not 0 < args.recall_threshold <= 1:
            parser.error('Formal requires verified-contract and explicit recall-threshold; run smoke first')
        contract = json.loads(args.verified_contract.read_text(encoding='utf-8'))
        if (contract.get('image_adapter_verified') is not True or
            not contract.get('integration_source') or contract.get('build_kind') != 'hnsw' or
            not isinstance(contract.get('completion_command'), list) or
            not contract['completion_command'] or
            not all(isinstance(x, str) for x in contract['completion_command'])):
            parser.error('Invalid verified HNSW completion/proof contract')
    args.output.mkdir(parents=True, exist_ok=False)
    args.output = args.output.resolve()
    report = {'status': 'running', 'started_utc': utc(), 'mode': args.mode,
              'qualified_gpu_build': False, 'script_sha256': sha256(__file__), 'rounds': []}
    report_path = args.output / 'report.json'
    sql = None
    try:
        write_json(args.output / 'arguments.json', vars(args))
        manifest, arrays, texts, gold = inputs(args.gt_dir.resolve(), args.base, args.query)
        write_json(args.output / 'input-manifest.json', manifest)
        if contract:
            write_json(args.output / 'verified-contract.json', contract)
        schema = args.schema or ('amd107_' + dt.datetime.now(dt.timezone.utc).strftime('%Y%m%d%H%M%S') + '_' + str(os.getpid()))
        ident(schema)
        report.update(schema=schema, query_count=manifest['query_count'], k=manifest['k'],
                      recall_targets=manifest['recall_targets'], dataset_sha256=manifest['inputs']['base']['sha256'])
        sql = SQL(args, args.output)
        report['driver'] = sql.driver
        version_session = sql.execute('SELECT VERSION(),@@session.ob_hnsw_ef_search')
        write_json(args.output / 'version.json', version_session)
        report['actual_session_ob_hnsw_ef_search'] = int(version_session[0][1])
        # No IF NOT EXISTS and no DROP: never take ownership of an existing schema.
        sql.execute('CREATE DATABASE ' + ident(schema))
        for number in range(1 if args.mode == 'smoke' else 3):
            order = ('cpu', 'gpu') if number % 2 == 0 else ('gpu', 'cpu')
            for backend in order:
                table_name, index_name = '%s_r%02d' % (backend, number), 'ix_%s_r%02d' % (backend, number)
                table = ident(schema) + '.' + ident(table_name)
                folder = args.output / ('%02d-%s' % (number, backend))
                folder.mkdir()
                row = {'backend': backend, 'round': number, 'table': table_name,
                       'index': index_name, 'status': 'preparing', 'qualified_gpu_build': False}
                report['rounds'].append(row)
                write_json(report_path, report)
                row['raw_data_readback'] = load_and_check(sql, table, texts['base'], arrays['base'],
                                                        args.batch_rows, folder / 'base-readback.jsonl')
                # Query table is independently loaded/read back so both corpus and queries are
                # proved to survive actual SQL storage. Retrieval still uses the exact raw text.
                query_table = ident(schema) + '.' + ident('queries_' + table_name)
                row['query_data_readback'] = load_and_check(sql, query_table, texts['query'], arrays['query'],
                                                         args.batch_rows, folder / 'query-readback.jsonl')
                indexes = sql.execute('SHOW INDEX FROM ' + table)
                write_json(folder / 'indexes-before.json', indexes)
                if any(str(cell) == index_name for cells in indexes for cell in cells):
                    raise ValueError('Target index already exists before timing')
                logs_before = snapshot_logs(args.server_log)
                write_json(folder / 'server-log-starts.json', logs_before)
                ddl = ('CREATE VECTOR INDEX %s ON %s(c2) WITH '
                    '(distance=l2,type=hnsw,lib=%s,m=%d,ef_construction=%d,ef_search=%d)') % (
                    ident(index_name), table, 'vsag' if backend == 'cpu' else 'cuvs',
                    args.m, args.ef_construction, args.ef_search)
                def query(qid):
                    return 'SELECT c1 FROM %s ORDER BY l2_distance(c2,%s) APPROXIMATE LIMIT %d' % (
                        table, vector_literal(texts['query'][qid]), manifest['k'])
                row.update(status='building', build_started_utc=utc(), raw_data_ready_before_timer=True,
                           target_index_absent_before_timer=True)
                write_json(report_path, report)
                start = time.monotonic()
                try:
                    sql.execute(ddl)
                    row['ddl_return_sec'] = time.monotonic() - start
                    first = sql.execute(query(0))
                    row['ddl_to_first_query_return_sec'] = time.monotonic() - start
                    write_json(folder / 'first-query.json', first)
                    plan = sql.execute('EXPLAIN ' + query(0))
                    write_json(folder / 'explain.json', plan)
                    row['plan_contains_index_name'] = index_name in json.dumps(plan, default=str)
                    if contract:
                        context = {'schema': schema, 'table': table_name, 'index': index_name,
                            'backend': backend, 'round': number, 'base_count': manifest['base_count'],
                            'output': str(folder), 'ddl_start_utc': row['build_started_utc']}
                        row['completion_proof'] = completion_proof(contract, context, folder, args.sql_timeout)
                        row['build_to_proven_ready_sec'] = time.monotonic() - start
                        row['qualified_gpu_build'] = backend == 'gpu'
                    else:
                        row['completion_state'] = 'UNVERIFIED: async/lazy build and CPU fallback not excluded'
                finally:
                    row['build_interval_finished_utc'] = utc()
                    row['server_log_deltas'] = finish_logs(logs_before, folder)
                    write_json(report_path, report)
                write_json(folder / 'show-create.json', sql.execute('SHOW CREATE TABLE ' + table))
                predictions, durations = {}, []
                for qid in range(manifest['query_count']):
                    before = time.monotonic()
                    result = sql.execute(query(qid))
                    durations.append(time.monotonic() - before)
                    predictions[str(qid)] = [int(cells[0]) for cells in result]
                write_json(folder / 'predictions.json', predictions)
                write_json(folder / 'query-times-sec.json', durations)
                row['recall'] = recall(gold, predictions, manifest['k'], manifest['base_count'], manifest['query_count'])
                write_json(folder / 'recall.json', row['recall'])
                if contract and row['recall']['pooled'] < args.recall_threshold:
                    raise ValueError('%s Recall below explicit threshold' % backend)
                row['status'] = 'diagnostic_complete' if not contract else 'verified_round_complete'
                write_json(report_path, report)
        if contract:
            medians = {backend: statistics.median(x['build_to_proven_ready_sec'] for x in report['rounds']
                                                 if x['backend'] == backend) for backend in ('cpu', 'gpu')}
            report.update(status='formal_measured_requires_evidence_review', qualified_gpu_build=True,
                          median_build_sec=medians, speedup=medians['cpu'] / medians['gpu'])
        else:
            report.update(status='smoke_complete_diagnostic_only',
                note='Original lib=cuvs can lazily build CAGRA on first query. Not qualified HNSW build acceleration. No formal speedup emitted.')
    except BaseException as error:
        report.update(status='failed', qualified_gpu_build=False,
                      error=type(error).__name__ + ': ' + str(error))
        (args.output / 'failure.txt').write_text(traceback.format_exc(), encoding='utf-8')
        raise
    finally:
        report['finished_utc'] = utc()
        write_json(report_path, report)
        if sql:
            sql.close()
    print(json.dumps({'status': report['status'], 'report': str(report_path),
                      'qualified_gpu_build': report['qualified_gpu_build']}))


if __name__ == '__main__':
    main()
