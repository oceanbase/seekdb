#!/usr/bin/env python3
"""Validate one completed SQL build from saved SQL evidence and bounded logs.

Read-only with respect to the database, services and source logs. Only proof
artifacts under the supplied round directory are written. API names below are
source-proven dispatches, never invented names of profiled GPU kernels.
"""
import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import sys


GPU_APIS = ['cuvsCagraBuild', 'cuvsHnswFromCagra', 'cuvsHnswSerialize']
BAD_LOG = re.compile(
    r'\[seekdb_hipvs_hnsw\]\s+event=error\b|'
    r'bridge (?:unavailable|ABI incomplete); CPU fallback|'
    r'lazily built VSAG fallback|'
    r'GPU CAGRA build failed during snapshot serialize|'
    r'failed to (?:write snapshot stream|load CAGRA graph from snapshot)|'
    r'(?:truncated|invalid) snapshot (?:header|payload)|'
    r'cannot rewind snapshot stream', re.I)


def require(value, message):
    if not value:
        raise ValueError(message)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n',
                          encoding='utf-8')


def timestamp(value):
    result = dt.datetime.fromisoformat(value.replace('Z', '+00:00'))
    require(result.tzinfo is not None, 'UTC timestamp must include timezone')
    return result


def safe_identifier(value):
    require(re.fullmatch(r'[a-z][a-z0-9_]{0,63}', value), 'Unsafe SQL identifier')
    return '`' + value + '`'


def check_readback(path, receipt, expected, count, dim):
    require(receipt.get('bitwise_equal') is True and receipt.get('rows') == count
            and receipt.get('float32_values') == count * dim,
            'Full data readback receipt missing or incomplete: ' + path.name)
    require(receipt.get('readback_float32_sha256') == expected,
            'Readback receipt hash differs from actual input')
    digest = hashlib.sha256()
    rows = 0
    with path.open(encoding='utf-8') as stream:
        for line in stream:
            item = json.loads(line)
            require(type(item.get('id')) is int and item['id'] == rows,
                    'Readback IDs missing, duplicated or reordered')
            vector = json.loads(item['vector']) if isinstance(item['vector'], str) else item['vector']
            require(isinstance(vector, list) and len(vector) == dim and
                    all(type(x) in (int, float) and math.isfinite(x) for x in vector),
                    'Readback vector shape/value mismatch')
            digest.update(struct.pack('<%df' % dim, *vector))
            rows += 1
    require(rows == count and digest.hexdigest() == expected,
            'Actual saved readback bytes differ from full raw float32 input')
    return {'rows': rows, 'float32_values': rows * dim,
            'bitwise_equal': True, 'float32_sha256': digest.hexdigest()}


def capture_logs(starts, destination):
    require(isinstance(starts, list), 'server-log-starts must be a list')
    captures = []
    for number, item in enumerate(starts):
        source_path = Path(item['path'])
        require(source_path.is_absolute(), 'Log path must be absolute')
        offset = item['offset']
        require(type(offset) is int and offset >= 0, 'Invalid log start offset')
        target = destination / ('phase-log-%02d.delta' % number)
        with source_path.open('rb') as source:
            before = os.fstat(source.fileno())
            require(before.st_ino == item['inode'] and before.st_size >= offset,
                    'Log rotated or truncated before proof capture')
            end = before.st_size
            source.seek(offset)
            with target.open('wb') as output:
                remaining = end - offset
                while remaining:
                    block = source.read(min(1048576, remaining))
                    require(block, 'Log truncated during proof capture')
                    output.write(block)
                    remaining -= len(block)
            after = os.fstat(source.fileno())
            require(after.st_ino == before.st_ino and after.st_size >= end and
                    source_path.stat().st_ino == before.st_ino,
                    'Log rotated or truncated during proof capture')
        # A partial trailing line is not completion evidence. It is preserved,
        # but only complete newline-terminated lines enter marker parsing.
        raw = target.read_bytes()
        complete = raw.rsplit(b'\n', 1)[0] if b'\n' in raw else b''
        text = complete.decode('utf-8', errors='replace')
        require(not BAD_LOG.search(text), 'Actual bridge error or CPU fallback in current round')
        captures.append({'source': str(source_path), 'start_offset': offset,
            'end_offset': end, 'inode': before.st_ino, 'path': str(target),
            'sha256': sha256(target), 'text': text})
    return captures


def check_gpu_markers(captures, n, dim, parameters):
    expected = ['build_start', 'gpu_cagra_complete', 'build_complete', 'serialize_complete']
    complete = []
    for capture in captures:
        stages = []
        for line in capture['text'].splitlines():
            match = re.search(r'\[seekdb_hipvs_hnsw\]\s+event=(\w+)\b(.*)', line)
            if not match:
                continue
            event = match.group(1)
            fields = dict(re.findall(r'(\w+)=([^\s]+)', match.group(2)))
            if event == 'build_start':
                require(fields.get('n') == str(n) and fields.get('dim') == str(dim)
                        and fields.get('hierarchy') == 'gpu'
                        and fields.get('metric') == 'L2Expanded'
                        and fields.get('fallback') == 'disabled',
                        'Build marker is not this full-N GPU L2 index')
                for key, argument in [('search_ef', 'ef_search'), ('hnsw_m', 'm'),
                                      ('ef_construction', 'ef_construction')]:
                    require(fields.get(key) == str(parameters[argument]),
                            'Bridge build parameter differs from SQL: ' + key)
            elif event == 'build_complete':
                require(fields.get('backend') == 'cagra-to-hnsw'
                        and fields.get('hierarchy') == 'gpu'
                        and fields.get('fallback') == 'disabled',
                        'Full GPU-hierarchy HNSW conversion not completed')
            elif event == 'serialize_complete':
                require(fields.get('format') == 'SKHNSW01'
                        and fields.get('hnsw_data') == 'included'
                        and fields.get('target') == 'caller-proc-fd'
                        and int(fields.get('payload_bytes', '0')) > 0,
                        'Caller-FD full-HNSW snapshot not completed')
            if event in expected:
                stages.append(event)
        if stages:
            # One complete isolated index build. Repeated or interleaved builds
            # cannot be attributed safely because bridge logs lack SQL identity.
            require(stages == expected, 'Incomplete, repeated or interleaved GPU build sequence')
            complete.append(capture)
    require(complete, 'No same-round complete GPU build/conversion/caller-FD sequence')
    return complete


def check_source(path):
    source = path.read_text(encoding='utf-8')
    require(re.search(r'cuvsStreamSync\s*\(\s*h->res\s*\)', source),
            'Source has no real cuVS stream synchronization')
    for api, stage, marker in [
            ('cuvsCagraBuild', 'gpu_cagra_build_sync', 'event=gpu_cagra_complete'),
            ('cuvsHnswFromCagra', 'cagra_to_full_hnsw_sync', 'event=build_complete'),
            ('cuvsHnswSerialize', 'serialize_full_hnsw_sync', 'event=serialize_complete')]:
        # Each real call must be checked, then synchronized before its marker.
        call = source.find('check_cuvs(' + api + '(')
        sync = source.find('sync_index(h, "' + stage + '")', call)
        end = source.find(marker, sync)
        require(0 <= call < sync < end, 'Missing checked API/sync/completion sequence: ' + api)
    return sha256(path)


def prove(args):
    folder = args.output.resolve()
    root = folder.parent
    require(folder.is_dir(), 'Round output must already exist')
    proof_dir = folder / 'completion-evidence'
    proof_dir.mkdir(exist_ok=False)
    manifest = read_json(root / 'input-manifest.json')
    parameters = read_json(root / 'arguments.json')
    report = read_json(root / 'report.json')
    n, dim, q, k = [manifest[x] for x in ('base_count', 'dimension', 'query_count', 'k')]
    require(manifest.get('status') == 'complete' and manifest.get('metric') == 'l2'
            and n == args.base_count and manifest.get('recall_targets') == q * k,
            'Full raw/GT manifest mismatch')
    require(report.get('schema') == args.schema, 'Report schema mismatch')
    rows = [x for x in report['rounds'] if x.get('round') == args.round
            and x.get('backend') == args.backend and x.get('table') == args.table
            and x.get('index') == args.index]
    require(len(rows) == 1, 'Report round identity mismatch')
    row = rows[0]
    require(row.get('raw_data_ready_before_timer') is True
            and row.get('target_index_absent_before_timer') is True
            and row.get('build_started_utc') == args.ddl_start_utc,
            'Missing before-timer full data/index absence identity')
    quoted_table = safe_identifier(args.schema) + '.' + safe_identifier(args.table)
    quoted_index = safe_identifier(args.index)
    before = read_json(folder / 'indexes-before.json')
    require(not any(str(cell) == args.index for cells in before for cell in cells),
            'Target index already existed before build')
    readback = {}
    for label, count, key in [('base', n, 'raw_data_readback'), ('query', q, 'query_data_readback')]:
        require(manifest['inputs'][label]['shape'] == [count, dim], 'Manifest raw shape mismatch')
        readback[label] = check_readback(folder / (label + '-readback.jsonl'), row[key],
            manifest['inputs'][label]['sha256'], count, dim)
    events = [json.loads(line) for line in (root / 'sql-events.jsonl').read_text(encoding='utf-8').splitlines()]
    require(len(events) >= 3, 'Missing DDL/first-query/EXPLAIN SQL events')
    selected = events[-3:]
    ddl_start = timestamp(args.ddl_start_utc)
    previous = ddl_start
    scripts, results = [], []
    for event in selected:
        require(event.get('status') == 'ok', 'DDL, first-query or EXPLAIN failed')
        started = timestamp(event['started_utc'])
        require(started >= previous, 'SQL event outside this build interval or unordered')
        previous = started
        require(event.get('actual_session_ob_hnsw_ef_search') == parameters['ef_search'],
                'SQL event session ef_search differs from configured value')
        sql_path = root / event['sql']
        require(sql_path.parent == root and re.fullmatch(r'sql-\d{6}\.sql', sql_path.name),
                'Unsafe SQL event file path')
        session = read_json(sql_path.with_suffix('.session.json'))
        require(session.get('matches') is True
                and session.get('requested_ef_search') == parameters['ef_search']
                and session.get('actual_session_ob_hnsw_ef_search') == parameters['ef_search'],
                'Actual SQL session receipt missing/mismatched')
        scripts.append(sql_path.read_text(encoding='utf-8'))
        results.append(read_json(sql_path.with_suffix('.rows.json')))
        for path in [sql_path, sql_path.with_suffix('.session.json'), sql_path.with_suffix('.rows.json'),
                     sql_path.with_suffix('.stderr.txt')]:
            if path.is_file():
                shutil.copyfile(path, proof_dir / path.name)
    sql_prefix = r'SELECT\s+@@session\.ob_hnsw_ef_search\s*;\s*'
    lib = 'vsag' if args.backend == 'cpu' else 'cuvs'
    ddl_pattern = (sql_prefix + r'CREATE\s+VECTOR\s+INDEX\s+' + re.escape(quoted_index) +
        r'\s+ON\s+' + re.escape(quoted_table) + r'\s*\(c2\)\s+WITH\s*\(\s*' +
        re.escape('distance=l2,type=hnsw,lib=%s,m=%d,ef_construction=%d,ef_search=%d' %
            (lib, parameters['m'], parameters['ef_construction'], parameters['ef_search'])) + r'\s*\)\s*;\s*')
    require(re.fullmatch(ddl_pattern, scripts[0], re.I), 'Successful DDL is not this index/backend/configuration')
    query_pattern = (r'SELECT\s+c1\s+FROM\s+' + re.escape(quoted_table) +
        r'\s+ORDER\s+BY\s+l2_distance\(c2,\x27[^\x27]+\x27\)\s+APPROXIMATE\s+LIMIT\s+' + str(k))
    require(re.fullmatch(sql_prefix + query_pattern + r'\s*;\s*', scripts[1], re.I),
            'First-query SQL is not this full-data index query')
    require(re.fullmatch(sql_prefix + r'EXPLAIN\s+' + query_pattern + r'\s*;\s*', scripts[2], re.I),
            'EXPLAIN SQL is not this same query')
    require(results[1] == read_json(folder / 'first-query.json')
            and results[2] == read_json(folder / 'explain.json'), 'Saved SQL result artifacts disagree')
    first_ids = [int(cells[0]) for cells in results[1]]
    require(len(first_ids) == k and len(set(first_ids)) == k and all(0 <= x < n for x in first_ids),
            'First query lacks exactly K valid distinct IDs')
    plan = json.dumps(results[2])
    require(re.search(r'VECTOR\s+INDEX\s+SCAN', plan, re.I)
            and re.search(r'(?<![a-z0-9_])' + re.escape(args.index) + r'(?![a-z0-9_])', plan)
            and re.search(r'(?<![a-z0-9_])' + re.escape(args.table) + r'(?![a-z0-9_])', plan),
            'Plan lacks requested VECTOR INDEX SCAN')
    captures = capture_logs(read_json(folder / 'server-log-starts.json'), proof_dir)
    proof = {'schema': args.schema, 'table': args.table, 'index': args.index,
        'backend': args.backend, 'indexed_rows': n, 'complete_index_ready': True,
        'no_lazy_work_remaining': True, 'index_used': True, 'hnsw_semantics': True,
        'cpu_fallback': False, 'raw_data_readback': readback,
        'sql_success_event_numbers': [x['number'] for x in selected]}
    if args.backend == 'gpu':
        for name in ('bridge_source', 'bridge_library', 'gpu_environment_file'):
            require(getattr(args, name) and getattr(args, name).is_file(),
                    'GPU proof requires actual ' + name)
        phases = check_gpu_markers(captures, n, dim, parameters)
        source_hash = check_source(args.bridge_source)
        environment = args.gpu_environment_file.read_text(encoding='utf-8')
        require('gfx1100' in environment and re.search(r'GPU|AMD Radeon', environment),
                'Actual AMD GPU environment evidence missing')
        library_hash = sha256(args.bridge_library)
        shutil.copyfile(args.bridge_source, proof_dir / 'bridge-source.c')
        shutil.copyfile(args.gpu_environment_file, proof_dir / 'gpu-environment.txt')
        library_receipt = {'path': str(args.bridge_library.resolve()),
                           'bytes': args.bridge_library.stat().st_size, 'sha256': library_hash}
        if args.observer_pid is not None:
            maps = Path('/proc/%d/maps' % args.observer_pid).read_text(encoding='utf-8')
            resolved = str(args.bridge_library.resolve())
            require(any(line.rstrip().endswith(' ' + resolved) for line in maps.splitlines()),
                    'Requested bridge library not loaded in actual observer process')
            (proof_dir / 'observer-maps.txt').write_text(maps, encoding='utf-8')
            library_receipt['observer_pid'] = args.observer_pid
            library_receipt['loaded_in_observer_maps'] = True
        write_json(proof_dir / 'bridge-library.json', library_receipt)
        proof.update(gpu_build_participation=True, gpu_build_evidence={
            'method': 'source_and_real_api_phase_log', 'gpu_apis': GPU_APIS,
            'source_sha256': source_hash, 'bridge_library_sha256': library_hash,
            'gpu_arch': 'gfx1100', 'phase_log_sha256': [x['sha256'] for x in phases],
            'synchronized_full_hnsw_and_caller_fd_snapshot': True,
            'profiled_kernel_names_available': False})
    else:
        require(not any(re.search(r'\[seekdb_hipvs_hnsw\]\s+event=build_start\b', x['text'])
                        for x in captures), 'Unexpected GPU build during isolated CPU round')
    for name in ('input-manifest.json', 'arguments.json', 'version.json', 'report.json'):
        shutil.copyfile(root / name, proof_dir / name)
    write_json(proof_dir / 'sql-success-events.json', selected)
    write_json(proof_dir / 'bounded-log-captures.json',
               [{key: value for key, value in x.items() if key != 'text'} for x in captures])
    write_json(proof_dir / 'readback-verification.json', readback)
    shutil.copyfile(Path(__file__), proof_dir / 'completion_proof.py')
    existing = [folder / name for name in ('server-log-starts.json', 'indexes-before.json',
        'first-query.json', 'explain.json', 'base-readback.jsonl', 'query-readback.jsonl')]
    evidence = sorted([p for p in proof_dir.iterdir() if p.is_file()] + existing)
    proof['evidence'] = [{'path': str(p.resolve()), 'sha256': sha256(p)} for p in evidence]
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('schema', 'table', 'index', 'ddl-start-utc'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--backend', choices=['cpu', 'gpu'], required=True)
    parser.add_argument('--round', type=int, required=True)
    parser.add_argument('--base-count', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    for name in ('bridge-source', 'bridge-library', 'gpu-environment-file'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--observer-pid', type=int)
    args = parser.parse_args()
    try:
        print(json.dumps(prove(args), allow_nan=False))
    except (ValueError, OSError, KeyError, TypeError, struct.error, OverflowError) as error:
        parser.exit(2, 'Completion proof rejected: %s\n' % error)


if __name__ == '__main__':
    main()
