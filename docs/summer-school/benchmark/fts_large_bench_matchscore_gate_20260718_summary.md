# FTS Match-Score Gate Summary

Date: Saturday, July 18, 2026

## Scope

This round tightens the optimizer-side no-score detection for full-text retrieval:

- make `check_need_calc_match_score()` distinguish predicate-only `MATCH` usage from score-consuming usage
- only keep `pushdown_match_filter_` on text-retrieval scans when the plan really needs relevance calculation

The retained code changes are in:

- [src/sql/optimizer/ob_log_plan.cpp](/workspace/dxy_data/seekdb_index/src/sql/optimizer/ob_log_plan.cpp)
- [src/sql/rewrite/ob_transform_utils.cpp](/workspace/dxy_data/seekdb_index/src/sql/rewrite/ob_transform_utils.cpp)
- [src/sql/rewrite/ob_transform_utils.h](/workspace/dxy_data/seekdb_index/src/sql/rewrite/ob_transform_utils.h)

## Runs

Current-state 3-run average was computed from:

- `matchscore_gate_20260718_smoke`
- `matchscore_gate_20260718_run2`
- `matchscore_gate_20260718_run3`

Raw reports:

- [fts_large_bench_matchscore_gate_20260718_smoke.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_matchscore_gate_20260718_smoke.txt)
- [fts_large_bench_matchscore_gate_20260718_run2.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_matchscore_gate_20260718_run2.txt)
- [fts_large_bench_matchscore_gate_20260718_run3.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_matchscore_gate_20260718_run3.txt)
- [fts_large_bench_matchscore_gate_20260718_avg.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_matchscore_gate_20260718_avg.txt)

## Average Score

Average report score from `fts_large_bench_score.py`:

- `99.82 / 100`

Single-run samples:

- `99.15`
- `99.71`
- `100.00`

## Average Metrics

| Metric | Average |
| --- | ---: |
| `select1_avg_ms` | 0.180733 |
| `build_ik_all_sec` | 19.467000 |
| `build_ik_content_sec` | 15.857333 |
| `build_beng_en_sec` | 8.325000 |
| `build_total_sec` | 43.661333 |
| `tokenize_ik_avg_ms` | 0.273267 |
| `tokenize_beng_avg_ms` | 0.237500 |
| `query_cn_avg_ms` | 6.038033 |
| `query_beng_avg_ms` | 8.855900 |
| `query_mixed_avg_ms` | 6.548267 |
| `query_limit_avg_ms` | 13.625567 |

## Delta Vs Current Accepted Local Baseline

Compared with the previously accepted local average in [fts_large_bench_daat_filter_local_lookup_20260718_summary.md](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_daat_filter_local_lookup_20260718_summary.md):

| Metric | Previous | Current | Improvement |
| --- | ---: | ---: | ---: |
| `build_total_sec` | 43.734000 | 43.661333 | +0.17% |
| `tokenize_ik_avg_ms` | 0.273600 | 0.273267 | +0.12% |
| `tokenize_beng_avg_ms` | 0.241267 | 0.237500 | +1.56% |
| `query_cn_avg_ms` | 6.524933 | 6.038033 | +7.46% |
| `query_beng_avg_ms` | 9.555600 | 8.855900 | +7.32% |
| `query_mixed_avg_ms` | 7.004700 | 6.548267 | +6.52% |
| `query_limit_avg_ms` | 13.653733 | 13.625567 | +0.21% |

## Verdict

This round is accepted locally.

Why:

- the 3-run average score improved from `98.17 / 100` to `99.82 / 100`
- the win is stable across all three query-side predicate-only COUNT workloads
- `query_cn`, `query_beng`, and `query_mixed` improved by about `6.5%` to `7.5%` versus the current accepted local baseline
- `query_limit` stayed effectively flat but still improved slightly on the 3-run mean
- build and tokenize stayed flat to slightly better overall, so the query-side gain was not paid for with build regressions

Net interpretation:

- the optimizer was still over-preserving score/filter work for predicate-only `MATCH` usage
- moving that no-score distinction earlier, before DAS/codegen consume the plan, delivers a real benchmark win even after the larger DaaT pruning round
- the next likely wins are no longer in predicate-only gating; they are in `query_limit` BMW bootstrap and deeper IK/build internals
