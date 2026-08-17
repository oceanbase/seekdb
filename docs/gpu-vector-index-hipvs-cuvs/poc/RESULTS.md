# PoC measured results (2026-08-17, AMD W7900 / gfx1100)

Dataset: N=10,000, dim=128, Q=100, K=10, L2-normalized random-Gaussian; GT = brute-force top-10.

| Engine | recall@10 | build | per-query |
| --- | --- | --- | --- |
| seekdb CPU HNSW/VSAG (ef_search=200) | 0.648 | 0.23s add + ~3s async index | ~0.41 ms (single SQL round-trip) |
| hipVS GPU CAGRA (default) | 0.879 | 1.11s | 0.006 ms (batch-100, ~161k qps) |

Same base/query/ground-truth used for both. See `cuvs_bridge.c` (GPU CAGRA),
`m1_seekdb_baseline.py` (seekdb CPU), `l1_util.py` (export + recall scoring).
