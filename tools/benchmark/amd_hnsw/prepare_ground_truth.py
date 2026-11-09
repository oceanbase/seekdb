#!/usr/bin/env python3
"""Exact full-input float64 L2 truth for raw little-endian float32 vectors.

Uses existing NumPy only. IDs are zero-based physical rows. Output gold.json is
the mapping expected by run_once.recall; no provided reference GT is consumed.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time

import numpy as np


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def load_raw(path, count, dimension, block_rows):
    path = Path(path).resolve()
    expected = count * dimension * 4
    actual = path.stat().st_size
    if actual != expected:
        raise ValueError(f'{path}: expected exactly {expected} bytes, got {actual}')
    vectors = np.memmap(path, dtype='<f4', mode='r', shape=(count, dimension))
    for offset in range(0, count, block_rows):
        if not np.isfinite(vectors[offset:offset + block_rows]).all():
            raise ValueError(f'{path}: NaN/Inf in rows {offset}..{min(count, offset + block_rows)-1}')
    return vectors


def write_sql_vectors(vectors, path):
    """Nine significant decimal digits; verify every reconstructed f32 bit."""
    checked = 0
    with Path(path).open('w', encoding='ascii', newline='\n') as stream:
        for row in vectors:
            # One leading digit + eight fractional digits = nine significant.
            values = ','.join(format(float(value), '.8e') for value in row)
            restored = np.fromstring(values, sep=',', dtype='<f4')
            if restored.shape != row.shape or not np.array_equal(restored.view('<u4'), row.view('<u4')):
                raise ValueError('Decimal text did not round-trip to identical float32 bits')
            stream.write('[' + values + ']\n')
            checked += int(row.size)
    return checked


def topk(distances, ids, k):
    """Bound partial selection with deterministic smallest-ID boundary ties."""
    if len(ids) > k:
        cutoff = np.partition(distances, k - 1)[k - 1]
        lower = np.flatnonzero(distances < cutoff)
        equal = np.flatnonzero(distances == cutoff)
        equal = equal[np.argsort(ids[equal], kind='stable')]
        selected = np.concatenate((lower, equal[:k - len(lower)]))
        distances, ids = distances[selected], ids[selected]
    order = np.lexsort((ids, distances))
    return distances[order], ids[order]


def exact_l2(base, queries, k, block_rows):
    # State is O(query_count*K + block_rows*dimension); no full distance matrix.
    count = len(queries)
    best_d = [np.empty(0, dtype=np.float64) for _ in range(count)]
    best_i = [np.empty(0, dtype=np.int64) for _ in range(count)]
    for offset in range(0, len(base), block_rows):
        block = np.asarray(base[offset:offset + block_rows], dtype=np.float64)
        workspace = np.empty_like(block)
        block_ids = np.arange(offset, offset + len(block), dtype=np.int64)
        for query_id in range(count):
            query = np.asarray(queries[query_id], dtype=np.float64)
            np.subtract(block, query, out=workspace)
            np.square(workspace, out=workspace)
            distances = np.sum(workspace, axis=1, dtype=np.float64)
            if not np.isfinite(distances).all():
                raise ValueError('Nonfinite float64 squared L2 distance')
            best_d[query_id], best_i[query_id] = topk(
                np.concatenate((best_d[query_id], distances)),
                np.concatenate((best_i[query_id], block_ids)), k)
    gold = {str(q): [int(value) for value in best_i[q]] for q in range(count)}
    if set(gold) != {str(q) for q in range(count)} or any(
            len(values) != k or len(set(values)) != k for values in gold.values()):
        raise ValueError('Incomplete truth output')
    return gold, {str(q): best_d[q].tolist() for q in range(count)}


def prepare(base_path, query_path, dimension, base_count, query_count, k, output,
            metric='l2', block_rows=8192):
    for name, value in [('dimension', dimension), ('base_count', base_count),
                        ('query_count', query_count), ('k', k), ('block_rows', block_rows)]:
        if type(value) is not int or value <= 0:
            raise ValueError(f'{name} must be a positive integer')
    if k > base_count:
        raise ValueError('K must not exceed base_count')
    if metric != 'l2':
        raise ValueError('Only explicit l2 is supported; no implicit metric conversion')
    started = time.monotonic()
    base_path, query_path = Path(base_path).resolve(), Path(query_path).resolve()
    initial_stats = [(p.stat().st_size, p.stat().st_mtime_ns) for p in (base_path, query_path)]
    base = load_raw(base_path, base_count, dimension, block_rows)
    queries = load_raw(query_path, query_count, dimension, block_rows)
    inputs = {'base': {'path': str(base_path), 'shape': [base_count, dimension], 'sha256': sha256(base_path)},
              'query': {'path': str(query_path), 'shape': [query_count, dimension], 'sha256': sha256(query_path)}}
    output = Path(output).resolve()
    if output in (base_path, query_path) or output in base_path.parents or output in query_path.parents:
        raise ValueError('Output must be a new directory separate from input parents')
    output.mkdir(parents=True, exist_ok=False)
    # A manifest is only emitted after all input checks, text round-trips and GT finish.
    checked = {
        'base': write_sql_vectors(base, output / 'base.sql-vectors.txt'),
        'query': write_sql_vectors(queries, output / 'query.sql-vectors.txt'),
    }
    gold, distances = exact_l2(base, queries, k, block_rows)
    for path, initial in zip((base_path, query_path), initial_stats):
        if (path.stat().st_size, path.stat().st_mtime_ns) != initial:
            raise ValueError(f'Input changed during calculation: {path}')
    (output / 'gold.json').write_text(json.dumps(gold, separators=(',', ':')) + '\n', encoding='utf-8')
    (output / 'gold-distances.json').write_text(json.dumps(distances, separators=(',', ':'), allow_nan=False) + '\n', encoding='utf-8')
    manifest = {
        'status': 'complete', 'created_utc': datetime.now(timezone.utc).isoformat(),
        'script_sha256': sha256(Path(__file__)), 'numpy_version': np.__version__,
        'inputs': inputs, 'metric': 'l2', 'ranking_distance': 'squared_l2',
        'input_precision': 'raw little-endian IEEE754 float32; no header',
        'calculation_precision': 'float64 subtraction, square, sum over dimension',
        'tie_order': ['ascending float64 squared distance', 'ascending zero-based base row ID'],
        'query_ids': 'all physical query rows, decimal strings 0..query_count-1',
        'base_ids': 'all physical base rows, integers 0..base_count-1',
        'dimension': dimension, 'base_count': base_count, 'query_count': query_count,
        'k': k, 'recall_targets': query_count * k, 'block_rows': block_rows,
        'sql_text': {'format': 'one JSON numeric vector per line; line index is ID',
                     'significant_digits': 9, 'python_format': '.8e',
                     'float32_bitwise_roundtrip_all_values': True, 'values_checked': checked,
                     'database_readback': 'not performed by this tool; adapter must verify actual SQL storage'},
        'artifacts': {name: {'bytes': (output / name).stat().st_size, 'sha256': sha256(output / name)}
                      for name in ['gold.json', 'gold-distances.json', 'base.sql-vectors.txt', 'query.sql-vectors.txt']},
        'elapsed_sec': time.monotonic() - started,
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True)
    parser.add_argument('--query', required=True)
    for name in ['dimension', 'base-count', 'query-count', 'k']:
        parser.add_argument('--' + name, type=int, required=True)
    parser.add_argument('--metric', choices=['l2'], required=True)
    parser.add_argument('--block-rows', type=int, default=8192)
    parser.add_argument('--output', required=True, help='New directory, must not already exist')
    args = parser.parse_args()
    try:
        manifest = prepare(args.base, args.query, args.dimension, args.base_count,
                           args.query_count, args.k, args.output, args.metric, args.block_rows)
    except (ValueError, OSError) as error:
        parser.exit(2, f'ERROR: {error}\n')
    print(json.dumps({'status': 'complete', 'output': str(Path(args.output).resolve()),
                      'query_count': manifest['query_count'], 'recall_targets': manifest['recall_targets']}))


if __name__ == '__main__':
    main()
