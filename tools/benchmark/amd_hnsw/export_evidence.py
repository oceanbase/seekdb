#!/usr/bin/env python3
"""Export only named AMD benchmark evidence; never inspect server data or credentials.

Stdout is one zlib-compressed, base64-encoded UTF-8 JSON payload. Progress and the
download summary go to stderr. The complete tar.gz always retains selected SQL
and readback originals; only the clipboard payload omits explicitly named bulk
data, with original byte sizes and SHA256 values retained in its manifest.
"""
import argparse
import base64
from datetime import datetime, timezone
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import sys
import tarfile
import zlib


TOP_NAMES = frozenset({
    'report.json', 'arguments.json', 'input-manifest.json', 'verified-contract.json',
    'version.json', 'sql-events.jsonl', 'failure.txt', 'stdout.txt', 'stderr.txt',
    'run.json', 'run.txt', 'launch.json', 'launch-receipt.json', 'environment.json',
    'source-report.json', 'source-arguments.json',
})
ROUND_NAMES = frozenset({
    'base-readback.jsonl', 'query-readback.jsonl', 'indexes-before.json',
    'server-log-starts.json', 'first-query.json', 'explain.json', 'show-create.json',
    'predictions.json', 'query-times-sec.json', 'recall.json', 'proof-command.json',
    'proof.stdout.txt', 'proof.stderr.txt', 'table-count.json',
})
PROOF_NAMES = frozenset({
    'bridge-source.c', 'gpu-environment.txt', 'observer-maps.txt', 'bridge-library.json',
    'input-manifest.json', 'arguments.json', 'version.json', 'report.json',
    'sql-success-events.json', 'bounded-log-captures.json', 'readback-verification.json',
    'completion_proof.py',
})
ROOT_NAMES = frozenset({
    'export_evidence.py', 'sql_benchmark.py', 'completion_proof.py',
    'generate_dataset.py', 'prepare_ground_truth.py', 'generate_contract.py',
    'replay_existing.py', 'restart_persisted.py',
    'environment.json', 'run.json',
    'candidate-quality-launch.json', 'candidate-persisted-launch.json',
    'candidate-persisted.out',
    'hnsw-build-fdfix.txt', 'hnsw-build-fdfix.json',
    'hnsw-final-real-gpu512.txt', 'hnsw-final-real-gpu512.json',
    'hnsw-real-gpu512.txt', 'hnsw-real-gpu512.json',
    'hnsw-fdfix-smoke.txt', 'hnsw-fdfix-smoke.json',
    'quality64-100k.txt', 'quality64-100k.json',
    'hnsw-quality64-100k.txt', 'hnsw-quality64-100k.json',
})
BRIDGE_NAMES = frozenset({
    'seekdb_cuvs_bridge.c', 'seekdb_cuvs_bridge.h', 'build.sh', 'smoke_ctypes.py',
    'evaluate_files_ctypes.py', 'fd-fix-local-validation.json', 'local-validation.json',
})
SQL_NAME = re.compile(r'sql-\d{6}\.(?:sql|rows\.json|session\.json|stdout\.txt|stderr\.txt)\Z')
ROUND_DIR = re.compile(r'(?:\d{2}-)?(?:cpu|gpu)\Z')
PHASE_NAME = re.compile(r'(?:server-log|phase-log)-\d{2}\.delta\Z')
SCRIPT_NAME = re.compile(r'(?:start|run)[_-][A-Za-z0-9_-]+\.(?:py|sh)\Z')
RECEIPT_NAME = re.compile(
    r'(?:candidate108|quality64)[A-Za-z0-9_-]*[-_](?:launch|run|stdout|stderr|receipt)\.(?:txt|json|log)\Z')
BRIDGE_RUN_NAME = re.compile(r'(?:hnsw[-_])?(?:build-fdfix|realGPU512|quality64-100k)\.(?:txt|json)\Z')
DENIED_NAME = re.compile(r'(?i)(?:private|credential|token|secret|download|transfer|signed|base64|b64)')


def utc():
    return datetime.now(timezone.utc).isoformat()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def checked_path(root, value, directory=False):
    path = Path(value)
    if not path.is_absolute():
        path = root / path
    absolute = Path(os.path.abspath(path))
    require(absolute.is_relative_to(root), 'Input path must stay inside --root')
    relative = absolute.relative_to(root)
    require(relative.parts, 'Do not select the whole root as a result directory')
    current = root
    for part in relative.parts:
        require(not DENIED_NAME.search(part), 'Private/credential/transfer path excluded by policy')
        current = current / part
        require(not current.is_symlink(), 'Symlink inputs are excluded')
    require(absolute.is_dir() if directory else absolute.is_file(), 'Input is missing or has wrong type: ' + str(relative))
    return absolute


def collect_result(root, directory):
    """Inspect only the documented result layout, without a recursive root scan."""
    report = directory / 'report.json'
    require(report.is_file() and not report.is_symlink(), 'Result directory must contain report.json')
    files = []
    for path in sorted(directory.iterdir()):
        if path.name in TOP_NAMES or SQL_NAME.fullmatch(path.name):
            if path.is_file():
                files.append(checked_path(root, path))
        elif ROUND_DIR.fullmatch(path.name) and path.is_dir():
            require(not path.is_symlink(), 'Symlink round directory is excluded')
            for child in sorted(path.iterdir()):
                if child.name in ROUND_NAMES or PHASE_NAME.fullmatch(child.name):
                    if child.is_file():
                        files.append(checked_path(root, child))
                elif child.name == 'completion-evidence' and child.is_dir():
                    require(not child.is_symlink(), 'Symlink proof directory is excluded')
                    for evidence in sorted(child.iterdir()):
                        if (evidence.name in PROOF_NAMES or SQL_NAME.fullmatch(evidence.name)
                                or PHASE_NAME.fullmatch(evidence.name)) and evidence.is_file():
                            files.append(checked_path(root, evidence))
    return files


def collect(root, results, includes):
    files = []
    for directory in results:
        files.extend(collect_result(root, directory))
    # Only immediate, named source/receipt entries are considered outside results.
    for path in sorted(root.iterdir()):
        if not DENIED_NAME.search(path.name) and (path.name in ROOT_NAMES or SCRIPT_NAME.fullmatch(path.name)
                or RECEIPT_NAME.fullmatch(path.name) or BRIDGE_RUN_NAME.fullmatch(path.name)):
            if path.is_file():
                files.append(checked_path(root, path))
    bridge = root / 'hnsw_bridge'
    if bridge.is_dir() and not bridge.is_symlink():
        for name in sorted(BRIDGE_NAMES):
            path = bridge / name
            if path.is_file():
                files.append(checked_path(root, path))
    for value in includes:
        path = checked_path(root, value)
        require(path.suffix.lower() in {'.py', '.sh', '.c', '.h', '.json', '.jsonl', '.txt', '.log', '.delta'},
                '--include only accepts source and textual receipt types')
        files.append(path)
    return sorted(set(files), key=lambda p: p.relative_to(root).as_posix())


def state(path):
    st = path.stat()
    return (st.st_size, st.st_mtime_ns, st.st_ino)


def compact_omission(path, size, max_bulk):
    if path.name.endswith('readback.jsonl') and size > max_bulk:
        return 'large vector readback; original retained in complete archive'
    if SQL_NAME.fullmatch(path.name):
        stem = path.name.split('.')[0]
        sql = path.with_name(stem + '.sql')
        if sql.is_file() and not sql.is_symlink():
            with sql.open('rb') as source:
                head = source.read(65536).decode('utf-8', errors='strict')
            if path.suffix == '.sql' and re.search(r'\bINSERT\s+INTO\b', head, re.I):
                return 'bulk SQL INSERT values; original retained in complete archive'
            if (path.name.endswith(('.rows.json', '.stdout.txt')) and size > max_bulk
                    and re.search(r'\bSELECT\b', head, re.I)
                    and re.search(r'\b(?:array_map|c2|vector)\b', head, re.I)):
                return 'large SQL vector readback result; original retained in complete archive'
    return None


class HashReader:
    def __init__(self, source, collect_text):
        self.source = source
        self.digest = hashlib.sha256()
        self.parts = [] if collect_text else None
        self.size = 0

    def read(self, size=-1):
        block = self.source.read(size)
        self.digest.update(block)
        self.size += len(block)
        if self.parts is not None:
            self.parts.append(block)
        return block


def digest_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            digest.update(block)
    return digest.hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--results', type=Path, nargs='+', required=True,
                        help='Existing result directories, relative to root or absolute within root')
    parser.add_argument('--output-prefix', type=Path, required=True,
                        help='New output prefix; parents must already exist')
    parser.add_argument('--include', type=Path, nargs='*', default=[],
                        help='Additional exact textual source/receipt paths within root; no globs')
    parser.add_argument('--compact-bulk-bytes', type=int, default=65536)
    parser.add_argument('--no-print-payload', action='store_true')
    args = parser.parse_args(argv)
    root = args.root.resolve(strict=True)
    require(root.is_dir(), '--root must be a directory')
    require(args.compact_bulk_bytes > 0, 'compact bulk threshold must be positive')
    results = [checked_path(root, p, directory=True) for p in args.results]
    require(len(set(results)) == len(results), 'Duplicate result directory')
    prefix = args.output_prefix.absolute()
    require(prefix.parent.is_dir(), 'Output parent directory must already exist')
    for directory in results:
        require(not prefix.is_relative_to(directory), 'Outputs must be outside result directories')
    targets = {name: Path(str(prefix) + suffix) for name, suffix in {
        'archive': '.full.tar.gz', 'manifest': '.manifest.json',
        'compact': '.compact.zlib.b64', 'summary': '.summary.json'}.items()}
    require(not any(p.exists() for p in targets.values()), 'Output already exists; choose a new prefix')
    inputs = collect(root, results, args.include)
    require(inputs, 'No whitelisted evidence files found')
    manifest = {'schema': 'amd107-evidence-export-v1', 'created_utc': utc(),
        'root': str(root), 'result_directories': [p.relative_to(root).as_posix() for p in results],
        'policy': 'Only selected benchmark layout and named source/receipt files; no private transfers, credentials, binaries or database data.',
        'complete_archive_preserves_all_selected_original_bytes': True,
        'compact_bulk_threshold_bytes': args.compact_bulk_bytes, 'files': [], 'warnings': []}
    texts = {}
    before = {p: state(p) for p in inputs}
    # gzip/tar stream reads each original once and computes its exact archive-byte hash.
    with targets['archive'].open('xb') as raw:
        with gzip.GzipFile(filename='', fileobj=raw, mode='wb', compresslevel=6, mtime=0) as zipped:
            with tarfile.open(fileobj=zipped, mode='w|', format=tarfile.PAX_FORMAT) as archive:
                for path in inputs:
                    name = path.relative_to(root).as_posix()
                    size = before[path][0]
                    omitted = compact_omission(path, size, args.compact_bulk_bytes)
                    with path.open('rb') as source:
                        reader = HashReader(source, collect_text=omitted is None)
                        info = tarfile.TarInfo(name)
                        info.size, info.mtime, info.mode = size, before[path][1] // 1000000000, 0o644
                        archive.addfile(info, reader)
                    require(reader.size == size, 'Source truncated while exporting: ' + name)
                    row = {'path': name, 'size': size, 'sha256': reader.digest.hexdigest(),
                           'source_mtime_ns': before[path][1], 'compact_included': omitted is None}
                    if omitted:
                        row['omitted_reason'] = omitted
                    else:
                        data = b''.join(reader.parts)
                        require(b'\0' not in data, 'Non-text content in whitelisted file: ' + name)
                        texts[name] = data.decode('utf-8', errors='strict')
                    manifest['files'].append(row)
                after_inputs = collect(root, results, args.include)
                changed = [p.relative_to(root).as_posix() for p in inputs
                           if not p.exists() or state(p) != before[p]]
                added = sorted(p.relative_to(root).as_posix() for p in set(after_inputs) - set(inputs))
                manifest['snapshot_stable_during_export'] = not changed and not added
                manifest['changed_during_export'] = changed
                manifest['added_during_export'] = added
                statuses = {}
                for directory in results:
                    name = (directory / 'report.json').relative_to(root).as_posix()
                    status = json.loads(texts[name]).get('status')
                    statuses[directory.relative_to(root).as_posix()] = status
                    if status == 'running':
                        manifest['warnings'].append('IN_PROGRESS result snapshot: ' + directory.relative_to(root).as_posix())
                manifest['result_statuses'] = statuses
                if changed or added:
                    manifest['warnings'].append('Inputs changed during export; this is an explicitly nonfinal snapshot.')
                encoded_manifest = (json.dumps(manifest, indent=2, ensure_ascii=True) + '\n').encode()
                info = tarfile.TarInfo('EXPORT_MANIFEST.json')
                info.size, info.mode = len(encoded_manifest), 0o644
                archive.addfile(info, io.BytesIO(encoded_manifest))
    targets['manifest'].write_bytes(encoded_manifest)
    compact = {'schema': 'amd107-evidence-compact-v1', 'manifest': manifest,
        'encoding': 'UTF-8 JSON, zlib, standard base64', 'files': texts,
        'omitted': [row for row in manifest['files'] if not row['compact_included']]}
    encoder = json.JSONEncoder(ensure_ascii=True, separators=(',', ':'), allow_nan=False)
    compressor = zlib.compressobj(9)
    compact_hash = hashlib.sha256()
    compressed = []
    for part in encoder.iterencode(compact):
        data = part.encode('utf-8')
        compact_hash.update(data)
        compressed.append(compressor.compress(data))
    compressed.append(compressor.flush())
    payload = base64.b64encode(b''.join(compressed)).decode('ascii')
    targets['compact'].write_text(payload + '\n', encoding='ascii')
    summary = {'status': 'exported', 'created_utc': utc(), 'files': len(inputs),
        'original_bytes': sum(r['size'] for r in manifest['files']),
        'compact_files': len(texts), 'compact_omitted': len(compact['omitted']),
        'snapshot_stable_during_export': manifest['snapshot_stable_during_export'],
        'warnings': manifest['warnings'], 'compact_decoded_json_sha256': compact_hash.hexdigest(),
        'outputs': {name: {'path': str(path), 'size': path.stat().st_size, 'sha256': digest_file(path)}
                    for name, path in targets.items() if name != 'summary'}}
    targets['summary'].write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary, separators=(',', ':')), file=sys.stderr)
    if not args.no_print_payload:
        print(payload)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print('Evidence export failed: ' + type(error).__name__ + ': ' + str(error), file=sys.stderr)
        raise SystemExit(1)
