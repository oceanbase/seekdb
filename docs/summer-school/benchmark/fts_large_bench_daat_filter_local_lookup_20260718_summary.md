# FTS DaaT Filter + Local Lookup Summary

Date: Saturday, July 18, 2026

## Scope

This round keeps and validates the query-path optimizations around sparse full-text retrieval:

- restore the normal DaaT routing for non-boolean predicate-only MATCH queries
- stop forcing natural-language predicate-only DaaT paths to run the runtime `MATCH(...)` filter
- keep boolean-mode runtime filtering intact
- guard token-iter aggregate access so no-score paths can skip inverse-index aggregate iterators safely
- hook `DAS_OP_TABLE_LOOKUP` into the text-retrieval IR scan/sort path with local lookup instead of rejecting it

The retained code changes are in:

- [src/sql/das/iter/ob_das_iter_utils.cpp](/workspace/dxy_data/seekdb_index/src/sql/das/iter/ob_das_iter_utils.cpp)
- [src/sql/das/iter/sparse_retrieval/ob_das_tr_merge_iter.cpp](/workspace/dxy_data/seekdb_index/src/sql/das/iter/sparse_retrieval/ob_das_tr_merge_iter.cpp)
- [src/storage/retrieval/ob_i_sparse_retrieval_iter.h](/workspace/dxy_data/seekdb_index/src/storage/retrieval/ob_i_sparse_retrieval_iter.h)
- [src/storage/retrieval/ob_sparse_daat_iter.cpp](/workspace/dxy_data/seekdb_index/src/storage/retrieval/ob_sparse_daat_iter.cpp)
- [src/storage/retrieval/ob_sparse_daat_iter.h](/workspace/dxy_data/seekdb_index/src/storage/retrieval/ob_sparse_daat_iter.h)
- [src/storage/retrieval/ob_text_daat_iter.cpp](/workspace/dxy_data/seekdb_index/src/storage/retrieval/ob_text_daat_iter.cpp)
- [src/storage/retrieval/ob_text_retrieval_token_iter.cpp](/workspace/dxy_data/seekdb_index/src/storage/retrieval/ob_text_retrieval_token_iter.cpp)

## Runs

Current-state 3-run average was computed from:

- `daat_filter_local_lookup_20260718_trial2`
- `daat_filter_local_lookup_20260718_run2`
- `daat_filter_local_lookup_20260718_run3`

Raw reports:

- [fts_large_bench_daat_filter_local_lookup_20260718_trial2.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_daat_filter_local_lookup_20260718_trial2.txt)
- [fts_large_bench_daat_filter_local_lookup_20260718_run2.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_daat_filter_local_lookup_20260718_run2.txt)
- [fts_large_bench_daat_filter_local_lookup_20260718_run3.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_daat_filter_local_lookup_20260718_run3.txt)
- [fts_large_bench_daat_filter_local_lookup_20260718_avg.txt](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_daat_filter_local_lookup_20260718_avg.txt)

## Average Score

Average report score from `fts_large_bench_score.py`:

- `98.17 / 100`
- `mean_improvement: 49.08%`
- `build_improvement: 44.30%`
- `tokenize_improvement: 53.56%`
- `query_improvement: 49.38%`

## Average Metrics

| Metric | Average |
| --- | ---: |
| `select1_avg_ms` | 0.188233 |
| `build_ik_all_sec` | 19.758667 |
| `build_ik_content_sec` | 15.769667 |
| `build_beng_en_sec` | 8.193333 |
| `build_total_sec` | 43.734000 |
| `tokenize_ik_avg_ms` | 0.273600 |
| `tokenize_beng_avg_ms` | 0.241267 |
| `query_cn_avg_ms` | 6.524933 |
| `query_beng_avg_ms` | 9.555600 |
| `query_mixed_avg_ms` | 7.004700 |
| `query_limit_avg_ms` | 13.653733 |

## Delta Vs Accepted Local Baseline

Compared with the previously accepted local average in [fts_large_bench_stream_rows_20260717_summary.md](/workspace/dxy_data/seekdb_index/docs/summer-school/benchmark/fts_large_bench_stream_rows_20260717_summary.md):

| Metric | Previous | Current | Improvement |
| --- | ---: | ---: | ---: |
| `build_total_sec` | 43.174000 | 43.734000 | -1.30% |
| `tokenize_ik_avg_ms` | 0.272533 | 0.273600 | -0.39% |
| `tokenize_beng_avg_ms` | 0.234033 | 0.241267 | -3.09% |
| `query_cn_avg_ms` | 10.773300 | 6.524933 | +39.43% |
| `query_beng_avg_ms` | 15.744900 | 9.555600 | +39.31% |
| `query_mixed_avg_ms` | 11.205433 | 7.004700 | +37.49% |
| `query_limit_avg_ms` | 13.602467 | 13.653733 | -0.38% |

## Verdict

This round is accepted locally.

Why:

- the 3-run average score reached `98.17 / 100`, well above the previously accepted `86.75 / 100`
- the main benchmark win is query-side: `query_cn`, `query_beng`, and `query_mixed` improved by about `37%` to `39%` versus the previously accepted local baseline
- `query_limit` is effectively flat, with a small `-0.38%` regression versus the previously accepted local baseline
- build/tokenize are slightly worse than the previously accepted build-focused local baseline, but still score strongly against the benchmark's fixed baseline

Net interpretation:

- the DaaT routing and no-score filter pruning are real, stable whole-benchmark wins
- the local-lookup hookup is safe and slightly beneficial in the current local environment, but it is not the main contributor to the large score jump
- the next likely build-side gains, if needed, are inside IK token arbitration/container internals rather than the current DDL topology
