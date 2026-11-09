#!/usr/bin/env python3
"""Generate a reproducible, separate scale corpus; never edits image data."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import numpy as np


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--base-count', type=int, default=100000)
    p.add_argument('--query-count', type=int, default=100)
    p.add_argument('--dimension', type=int, default=128)
    p.add_argument('--seed', type=int, default=20261028)
    a = p.parse_args()
    if min(a.base_count, a.query_count, a.dimension) <= 0:
        p.error('positive sizes required')
    a.output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    rng = np.random.Generator(np.random.PCG64(a.seed))
    report = {'seed': a.seed, 'generator': 'NumPy PCG64, normal, unit L2',
              'numpy_version': np.__version__, 'dimension': a.dimension,
              'index_reused': False, 'files': {}}
    for name, n in [('base', a.base_count), ('query', a.query_count)]:
        values = rng.standard_normal((n, a.dimension), dtype=np.float32)
        norms = np.linalg.norm(values.astype(np.float64), axis=1, keepdims=True)
        values = np.asarray(values / norms, dtype='<f4')
        assert np.isfinite(values).all() and (norms > 0).all()
        path = a.output / (name + '.f32')
        values.tofile(path)
        report['files'][name] = {'rows': n, 'bytes': path.stat().st_size,
                                'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    report['elapsed_sec'] = time.monotonic() - started
    (a.output / 'generation.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    main()
