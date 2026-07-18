# FTS Reuse + 4-Column Build Benchmark Summary

Date: 2026-07-17

## Scope

This round validates the integrated FTS optimization set on `feat/index` with `DIAG=1`:

- remove runtime `POS_LIST` generation from the FTS build path and keep new aux rows at 4 columns
- reuse IK/BENG parser state across documents during build/tokenize hot paths
- add SQL `tokenize()` constant-result cache
- add BENG ASCII fast path and IK scratch-context reuse

Build command used:

```bash
bash build.sh debug --init --make -j48
```

Benchmark command used for each run:

```bash
DIAG=1 LABEL=<label> OUTPUT=<file> bash tools/benchmark/fts_large_bench.sh
python3 tools/benchmark/fts_large_bench_score.py <file> > <score_file>
```

## Environment

The local `DIAG=1` runs on 2026-07-17 reported:

- `cpu_count=96`
- `stack_size=8M`
- filesystem `/workspace` on `ext4`
- server version `5.7.25-OceanBase seekdb-v1.3.0.0`

This is materially different from the online CI environment previously observed on 2026-07-17:

- `cpu_count=32`
- `stack_size=256K`
- workspace filesystem `xfs`

So these numbers should be read as local A/B evidence first, not as a direct prediction of the online absolute score.

## Artifacts

Runs:

- `docs/summer-school/benchmark/fts_large_bench_reuse_poslist_diag1_20260717_run1.txt`
- `docs/summer-school/benchmark/fts_large_bench_reuse_poslist_diag1_20260717_run2.txt`
- `docs/summer-school/benchmark/fts_large_bench_reuse_poslist_diag1_20260717_run3.txt`

Scores:

- `docs/summer-school/benchmark/fts_large_bench_reuse_poslist_diag1_20260717_run1.score.txt`
- `docs/summer-school/benchmark/fts_large_bench_reuse_poslist_diag1_20260717_run2.score.txt`
- `docs/summer-school/benchmark/fts_large_bench_reuse_poslist_diag1_20260717_run3.score.txt`

Comparison baseline:

- `docs/summer-school/benchmark/fts_large_bench_poslist_opt_currscript_20260717_summary.md`

## Averaged Result

Average across the 3 new `DIAG=1` runs:

| Metric | Average |
| --- | ---: |
| `select1_avg_ms` | 0.1818 |
| `raw_load_sec` | 1.3393 |
| `build_ik_all_sec` | 24.5643 |
| `build_ik_content_sec` | 20.1207 |
| `build_beng_en_sec` | 9.7287 |
| `build_total_sec` | 54.4250 |
| `tokenize_ik_avg_ms` | 0.6453 |
| `tokenize_beng_avg_ms` | 0.3409 |
| `query_cn_avg_ms` | 14.1436 |
| `query_beng_avg_ms` | 20.4881 |
| `query_mixed_avg_ms` | 14.8287 |
| `query_limit_avg_ms` | 13.8319 |

Direct comparison against the previous local summary averages:

| Metric | Previous avg | Current avg | Delta | Relative |
| --- | ---: | ---: | ---: | ---: |
| `select1_avg_ms` | 0.1813 | 0.1818 | +0.0005 | -0.29% |
| `raw_load_sec` | 1.3303 | 1.3393 | +0.0090 | -0.68% |
| `build_ik_all_sec` | 24.7897 | 24.5643 | -0.2254 | +0.91% |
| `build_ik_content_sec` | 20.1567 | 20.1207 | -0.0360 | +0.18% |
| `build_beng_en_sec` | 9.8593 | 9.7287 | -0.1306 | +1.32% |
| `build_total_sec` | 54.8170 | 54.4250 | -0.3920 | +0.72% |
| `tokenize_ik_avg_ms` | 0.6488 | 0.6453 | -0.0035 | +0.54% |
| `tokenize_beng_avg_ms` | 0.3555 | 0.3409 | -0.0146 | +4.11% |
| `query_cn_avg_ms` | 14.1696 | 14.1436 | -0.0260 | +0.18% |
| `query_beng_avg_ms` | 20.5141 | 20.4881 | -0.0260 | +0.13% |
| `query_mixed_avg_ms` | 14.8428 | 14.8287 | -0.0141 | +0.10% |
| `query_limit_avg_ms` | 13.7861 | 13.8319 | +0.0458 | -0.33% |

## Score Summary

`fts_large_bench_score.py` averages:

| Metric | Avg | Stddev |
| --- | ---: | ---: |
| `score` | 42.6367 | 0.1815 |
| `mean_improvement` | 21.3200% | 0.0920 |

Per-run score:

| Run | Score |
| --- | ---: |
| `run1` | 42.38 |
| `run2` | 42.77 |
| `run3` | 42.76 |

Compared with the previous local score average `41.9967`, this round is `+0.6400` higher and also lower-variance.

## Verdict

This optimization set shows a **stable but modest local improvement**:

- `build_total_sec` improved from `54.8170s` to `54.4250s` on average
- all 3 build sub-phases improved
- `tokenize_beng_avg_ms` improved clearly; `tokenize_ik_avg_ms` improved slightly
- `query_cn`, `query_beng`, and `query_mixed` improved slightly but consistently
- only `query_limit_avg_ms` regressed slightly

Conclusion:

- the integrated `POS_LIST` 4-column build path + parser/tokenize hot-path optimization passes the local acceptance bar of a stable average improvement
- the gain is real but not large enough by itself to explain a future online `70+` score
- if the next goal is to raise the online score substantially, the next highest-leverage area is still query-side retrieval/merge hot-path optimization
