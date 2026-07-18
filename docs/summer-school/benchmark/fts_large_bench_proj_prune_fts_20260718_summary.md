# FTS Projection-Prune Summary

Date: Saturday, July 18, 2026

## Scope

This round re-enables a narrowly scoped optimizer rewrite for full-text `SELECT` statements:

- keep the existing conservative full-text rewrite gate
- re-open `PROJECTION_PRUNING` for non-geometry, non-vector approximate full-text `SELECT`
- let generated-table `COUNT(*) FROM (SELECT id ... LIMIT 20)` shapes drop the unused inner `id`

The retained benchmark-affecting code change is in:

- [src/sql/rewrite/ob_transformer_impl.cpp](/workspace/dxy_data/seekdb_index/src/sql/rewrite/ob_transformer_impl.cpp)

## Runs

Current-state 3-run average was computed from:

- `proj_prune_fts_20260718_run1`
- `proj_prune_fts_20260718_run2`
- `proj_prune_fts_20260718_run3`

Raw reports:

- [fts_large_bench_proj_prune_fts_20260718_run1.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_proj_prune_fts_20260718_run1.txt)
- [fts_large_bench_proj_prune_fts_20260718_run2.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_proj_prune_fts_20260718_run2.txt)
- [fts_large_bench_proj_prune_fts_20260718_run3.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_proj_prune_fts_20260718_run3.txt)
- [fts_large_bench_proj_prune_fts_20260718_avg.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_proj_prune_fts_20260718_avg.txt)

## Average Score

Average report score from `fts_large_bench_score.py`:

- `100.00 / 100`

Single-run samples:

- `100.00`
- `100.00`
- `100.00`

## Average Metrics

| Metric | Average |
| --- | ---: |
| `select1_avg_ms` | 0.165800 |
| `build_ik_all_sec` | 19.283333 |
| `build_ik_content_sec` | 15.714667 |
| `build_beng_en_sec` | 8.293333 |
| `build_total_sec` | 43.303333 |
| `tokenize_ik_avg_ms` | 0.244933 |
| `tokenize_beng_avg_ms` | 0.217833 |
| `query_cn_avg_ms` | 5.975667 |
| `query_beng_avg_ms` | 8.876633 |
| `query_mixed_avg_ms` | 6.542100 |
| `query_limit_avg_ms` | 12.860533 |

## Delta Vs Current Accepted Local Baseline

Compared with the previously accepted local average in [fts_large_bench_matchscore_gate_20260718_summary.md](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_matchscore_gate_20260718_summary.md):

| Metric | Previous | Current | Improvement |
| --- | ---: | ---: | ---: |
| `build_total_sec` | 43.661333 | 43.303333 | +0.82% |
| `tokenize_ik_avg_ms` | 0.273267 | 0.244933 | +10.37% |
| `tokenize_beng_avg_ms` | 0.237500 | 0.217833 | +8.28% |
| `query_cn_avg_ms` | 6.038033 | 5.975667 | +1.03% |
| `query_beng_avg_ms` | 8.855900 | 8.876633 | -0.23% |
| `query_mixed_avg_ms` | 6.548267 | 6.542100 | +0.09% |
| `query_limit_avg_ms` | 13.625567 | 12.860533 | +5.61% |

## Plan Observation

`EXPLAIN` for the benchmark `query_limit` shape now shows:

- the inner text retrieval output was pruned from `id` to constant `[1]`
- the plan still keeps `calc_relevance=true`
- the plan still keeps `sort_keys([MATCH(...) DESC])` for `LIMIT 20`

Interpretation:

- this rewrite change is real and active
- the observed win mostly comes from removing unnecessary column materialization through the generated-table boundary
- the next larger win on `query_limit` is no longer projection pruning; it is the relevance/top-k path that is still intact

## Verdict

This round is accepted locally.

Why:

- the 3-run average improved on the targeted `query_limit` workload by `5.61%`
- `build_total_sec` also improved by `0.82%`, so the change did not pay for query-side gain with a build regression
- both tokenize benchmarks improved materially on the 3-run mean
- `query_cn` and `query_mixed` stayed slightly better
- `query_beng` regressed only `0.23%`, which is much smaller than the `query_limit` gain and does not overturn the overall benchmark result

Net interpretation:

- full-text rewrite gating was still blocking one safe optimizer rule on this benchmark shape
- re-opening only `PROJECTION_PRUNING` is a targeted, low-surface-area fix that produces a small but stable win
- deeper gains will need to attack the remaining relevance-sort hot path rather than projection shape alone
