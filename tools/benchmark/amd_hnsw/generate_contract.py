#!/usr/bin/env python3
"""Generate a candidate108 completion contract; unverified by default.

Activation is explicit and validates a completed real smoke's saved receipts.
No SQL, subprocess, network, service or GPU command is executed by this tool.
"""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import sys

import completion_proof as adapter


def verify_smoke(path, threshold, source_hash, library, pid, reviewed_gpu_log=None):
    root = path.parent.resolve()
    report = adapter.read_json(path)
    manifest = adapter.read_json(root / 'input-manifest.json')
    parameters = adapter.read_json(root / 'arguments.json')
    adapter.require(report.get('mode') == 'smoke'
        and report.get('status') == 'smoke_complete_diagnostic_only',
        'Activation requires a completed real CPU/GPU SQL smoke')
    adapter.require(manifest.get('status') == 'complete' and manifest.get('metric') == 'l2',
                    'Smoke did not use full exact-L2 inputs')
    n, dim, q, k = [manifest[x] for x in ('base_count', 'dimension', 'query_count', 'k')]
    adapter.require(manifest.get('recall_targets') == q * k and
                    report.get('actual_session_ob_hnsw_ef_search') == parameters['ef_search'],
                    'Smoke denominator/session mismatch')
    rows = report.get('rounds', [])
    adapter.require(len(rows) == 2 and {x['backend'] for x in rows} == {'cpu', 'gpu'},
                    'Need exactly one CPU and one GPU smoke round')
    reviewed = []
    manually_reviewed_log = None
    for row in rows:
        folder = root / ('%02d-%s' % (row['round'], row['backend']))
        adapter.require(row.get('status') == 'diagnostic_complete'
            and row.get('raw_data_ready_before_timer') is True
            and row.get('target_index_absent_before_timer') is True,
            'Smoke preparation/build/query incomplete')
        result = row.get('recall', {})
        adapter.require(result.get('targets') == q * k and result.get('query_count') == q
            and result.get('pooled', 0) >= threshold, 'Full smoke recall below requested threshold')
        for label, count, key in [('base', n, 'raw_data_readback'), ('query', q, 'query_data_readback')]:
            adapter.check_readback(folder / (label + '-readback.jsonl'), row[key],
                manifest['inputs'][label]['sha256'], count, dim)
        plans = json.dumps(adapter.read_json(folder / 'explain.json'))
        adapter.require(re.search(r'VECTOR\s+INDEX\s+SCAN', plans, re.I)
            and re.search(r'(?<![a-z0-9_])' + re.escape(row['index']) + r'(?![a-z0-9_])', plans)
            and re.search(r'(?<![a-z0-9_])' + re.escape(row['table']) + r'(?![a-z0-9_])', plans),
            'Smoke plan does not use actual requested vector index')
        starts = adapter.read_json(folder / 'server-log-starts.json')
        captures = []
        deltas = row.get('server_log_deltas', [])
        adapter.require(len(deltas) == len(starts), 'Smoke log start/end evidence missing')
        for start, delta in zip(starts, deltas):
            capture_path = Path(delta.get('artifact', '')).resolve()
            adapter.require(delta.get('status') == 'captured'
                and all(delta.get(key) == start.get(key) for key in ('path', 'offset', 'inode'))
                and delta.get('end_offset', -1) >= start['offset']
                and folder.resolve() in capture_path.parents and capture_path.is_file()
                and adapter.sha256(capture_path) == delta.get('sha256'),
                'Smoke bounded log identity/hash invalid')
            raw = capture_path.read_bytes()
            adapter.require(len(raw) == delta['end_offset'] - start['offset'],
                            'Smoke log delta byte extent mismatch')
            text = (raw.rsplit(b'\n', 1)[0] if b'\n' in raw else b'').decode('utf-8', errors='replace')
            adapter.require(not adapter.BAD_LOG.search(text), 'Smoke contains actual bridge fallback/error')
            captures.append({'text': text, 'sha256': delta['sha256'], 'path': str(capture_path)})
        if row['backend'] == 'gpu':
            if reviewed_gpu_log:
                # Explicit root review of a real smoke log is an alternative
                # activation receipt, never mislabelled as an offset capture.
                log_text = reviewed_gpu_log.read_text(encoding='utf-8')
                adapter.require(not adapter.BAD_LOG.search(log_text),
                                'Manually reviewed smoke log contains actual error/fallback')
                adapter.check_gpu_markers([{'text': log_text,
                    'sha256': adapter.sha256(reviewed_gpu_log)}], n, dim, parameters)
                manually_reviewed_log = {'path': str(reviewed_gpu_log.resolve()),
                    'sha256': adapter.sha256(reviewed_gpu_log),
                    'correlation': 'Explicit root review of successful smoke plus real complete API log; old smoke offsets do not establish this correlation.',
                    'smoke_log_offsets_verified': False}
            else:
                adapter.check_gpu_markers(captures, n, dim, parameters)
        else:
            adapter.require(not any(re.search(r'\[seekdb_hipvs_hnsw\]\s+event=build_start\b', x['text'])
                                    for x in captures), 'GPU build leaked into CPU smoke round')
        reviewed.append({'backend': row['backend'], 'recall': result['pooled'],
                         'hits': result['hits'], 'targets': result['targets']})
    # Check the library actually mapped by the launched observer, including inode;
    # a newer file at the same pathname cannot stand in for the loaded binary.
    maps = Path('/proc/%d/maps' % pid).read_text(encoding='utf-8')
    loaded = [line.split(None, 5) for line in maps.splitlines() if len(line.split(None, 5)) == 6]
    adapter.require(any(fields[5] == str(library.resolve()) and int(fields[4]) == library.stat().st_ino
                        for fields in loaded), 'Actual observer has not loaded this bridge-library inode')
    return {'report_path': str(path.resolve()), 'report_sha256': adapter.sha256(path),
            'base_count': n, 'dimension': dim, 'query_count': q, 'k': k,
            'parameters': {key: parameters[key] for key in ('m', 'ef_construction', 'ef_search')},
            'reviewed_smoke_recall': reviewed, 'required_recall_threshold': threshold,
            'bridge_source_sha256': source_hash, 'loaded_bridge_library_sha256': adapter.sha256(library),
            'observer_pid': pid, 'observer_maps_sha256': hashlib.sha256(maps.encode('utf-8')).hexdigest(),
            'smoke_gpu_log_offsets_verified': manually_reviewed_log is None,
            'manually_reviewed_gpu_log': manually_reviewed_log,
            'formal_completion_still_requires_same_round_offsets': True,
            'scope': 'Saved real SQL smoke, full bitwise readback, complete recall, real API logs and actual loaded library; not a speedup measurement.'}


def generate(args):
    workspace = args.workspace.resolve()
    launch_path = args.launch or workspace / 'candidate108-launch.json'
    launch = adapter.read_json(launch_path)
    pid = launch.get('pid')
    adapter.require(type(pid) is int and pid > 0, 'Launch receipt must contain positive integer pid')
    source = workspace / 'hnsw_bridge' / 'seekdb_cuvs_bridge.c'
    library = workspace / 'hnsw_bridge' / 'libseekdb_cuvs_bridge.so'
    environment = workspace / 'environment.json'
    for path in (source, library, environment, workspace / 'completion_proof.py'):
        adapter.require(path.is_file(), 'Missing actual candidate artifact: ' + str(path))
    source_hash = adapter.check_source(source)
    adapter.require('gfx1100' in environment.read_text(encoding='utf-8'),
                    'Actual AMD gfx1100 environment receipt required')
    contract = {'image_adapter_verified': False,
        'integration_source': 'candidate108; launch_sha256=%s; bridge_source_sha256=%s; bridge_library_sha256=%s' %
            (adapter.sha256(launch_path), source_hash, adapter.sha256(library)),
        'build_kind': 'hnsw', 'completion_command': [
            sys.executable, str(workspace / 'completion_proof.py'),
            '--schema', '{schema}', '--table', '{table}', '--index', '{index}',
            '--backend', '{backend}', '--round', '{round}', '--base-count', '{base_count}',
            '--output', '{output}', '--ddl-start-utc', '{ddl_start_utc}',
            '--bridge-source', str(source), '--bridge-library', str(library),
            '--gpu-environment-file', str(environment), '--observer-pid', str(pid)],
        'created_utc': dt.datetime.now(dt.timezone.utc).isoformat(),
        'observer_pid': pid, 'generator_sha256': adapter.sha256(__file__),
        'activation_note': 'Unverified by default. Activation requires explicit completed real smoke report and threshold; this tool executes no SQL/services.'}
    if args.activate_after_reviewed_smoke:
        adapter.require(args.recall_threshold is not None and 0 < args.recall_threshold <= 1,
                        'Explicit recall-threshold required for activation')
        contract['smoke_verification'] = verify_smoke(args.activate_after_reviewed_smoke,
            args.recall_threshold, source_hash, library, pid, args.reviewed_gpu_build_log)
        contract['image_adapter_verified'] = True
    args.output.parent.mkdir(parents=True, exist_ok=True)
    adapter.require(not args.output.exists(), 'Refuse to overwrite an existing contract')
    adapter.write_json(args.output, contract)
    return contract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--launch', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--activate-after-reviewed-smoke', type=Path,
                        help='Explicit real smoke report; omit to keep image_adapter_verified=false')
    parser.add_argument('--recall-threshold', type=float)
    parser.add_argument('--reviewed-gpu-build-log', type=Path,
        help='Explicitly reviewed real smoke API log when old smoke captured the wrong log; records offsets as unverified')
    args = parser.parse_args()
    try:
        contract = generate(args)
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(2, 'Contract generation rejected: %s\n' % error)
    print(json.dumps({'output': str(args.output.resolve()),
                      'image_adapter_verified': contract['image_adapter_verified'],
                      'observer_pid': contract['observer_pid']}))


if __name__ == '__main__':
    main()
