# FTS Stream Rows Benchmark Summary

Date: Friday, July 17, 2026

## Scope

This round keeps the accepted optimization in the full-text DDL scan path:

- tokenize one document into a per-document `ObFTWordMap`
- stop materializing a full `ObDatumRow[]` for every tokenized document
- emit auxiliary full-text rows lazily from `ObFTIndexRowCache::get_next_row()`
- reuse one `ObDatumRow` and one copied `doc_id` datum while iterating the per-document word map

This change is restricted to the FTS DDL/index-build scan path. The DML/query-side `generate_fulltext_word_rows()` path stays in the original materialized mode.

## Baseline

Baseline before the change was collected from 3 local runs on Friday, July 17, 2026:

- `baseline_current_run1`
- `baseline_current_run2`
- `baseline_current_run3`

Baseline averages:

| Metric | Average |
| --- | ---: |
| `select1_avg_ms` | 0.183667 |
| `build_ik_all_sec` | 19.835000 |
| `build_ik_content_sec` | 16.024000 |
| `build_beng_en_sec` | 8.624000 |
| `build_total_sec` | 44.494333 |
| `tokenize_ik_avg_ms` | 0.272900 |
| `tokenize_beng_avg_ms` | 0.238600 |
| `query_cn_avg_ms` | 10.773833 |
| `query_beng_avg_ms` | 15.566467 |
| `query_mixed_avg_ms` | 11.091667 |
| `query_limit_avg_ms` | 13.656367 |

## Stream Rows Candidate

Candidate averages from 3 local runs on Friday, July 17, 2026:

- `stream_rows_run1`
- `stream_rows_run2`
- `stream_rows_run3`

Candidate averages:

| Metric | Average |
| --- | ---: |
| `select1_avg_ms` | 0.181367 |
| `build_ik_all_sec` | 19.282333 |
| `build_ik_content_sec` | 15.727000 |
| `build_beng_en_sec` | 8.153000 |
| `build_total_sec` | 43.174000 |
| `tokenize_ik_avg_ms` | 0.272533 |
| `tokenize_beng_avg_ms` | 0.234033 |
| `query_cn_avg_ms` | 10.773300 |
| `query_beng_avg_ms` | 15.744900 |
| `query_mixed_avg_ms` | 11.205433 |
| `query_limit_avg_ms` | 13.602467 |

## Delta

Positive `Improvement` means lower time.

| Metric | Delta | Improvement |
| --- | ---: | ---: |
| `select1_avg_ms` | -0.002300 | +1.25% |
| `build_ik_all_sec` | -0.552667 | +2.79% |
| `build_ik_content_sec` | -0.297000 | +1.85% |
| `build_beng_en_sec` | -0.471000 | +5.46% |
| `build_total_sec` | -1.320333 | +2.97% |
| `tokenize_ik_avg_ms` | -0.000367 | +0.13% |
| `tokenize_beng_avg_ms` | -0.004567 | +1.91% |
| `query_cn_avg_ms` | -0.000533 | +0.00% |
| `query_beng_avg_ms` | +0.178433 | -1.15% |
| `query_mixed_avg_ms` | +0.113766 | -1.03% |
| `query_limit_avg_ms` | -0.053900 | +0.39% |

## Verdict

This round is accepted locally.

Why:

- build-side improvement is stable across all 3 candidate runs
- `build_total_sec` improved from `44.494333` to `43.174000`, a `+2.97%` gain
- the biggest win is `build_beng_en_sec`, improving `+5.46%`
- tokenize cost stayed flat-to-better
- query regressions are small and limited to `query_beng_avg_ms` and `query_mixed_avg_ms`

Net interpretation:

- this is a real build-pipeline improvement
- it is not a broad whole-benchmark win, but it does move the target build stage in the right direction without destabilizing the rest of the benchmark

## Rejected Follow-up

After `stream_rows`, I tried a second DDL-only follow-up on Friday, July 17, 2026:

- keep the same `stream_rows` design
- additionally reuse the `ObFTWordMap` bucket array with `clear()` and only recreate on growth

3-run average of that follow-up:

| Metric | Average |
| --- | ---: |
| `select1_avg_ms` | 0.182333 |
| `build_ik_all_sec` | 19.270667 |
| `build_ik_content_sec` | 15.562667 |
| `build_beng_en_sec` | 8.277000 |
| `build_total_sec` | 43.122333 |
| `tokenize_ik_avg_ms` | 0.270800 |
| `tokenize_beng_avg_ms` | 0.237833 |
| `query_cn_avg_ms` | 10.954467 |
| `query_beng_avg_ms` | 16.110800 |
| `query_mixed_avg_ms` | 11.418233 |
| `query_limit_avg_ms` | 13.795633 |

Why it was rejected:

- `build_total_sec` only improved `+0.12%` over `stream_rows`
- `build_beng_en_sec` regressed `-1.52%`
- query regressions were stable on all 3 runs:
  - `query_cn_avg_ms` `-1.68%`
  - `query_beng_avg_ms` `-2.32%`
  - `query_mixed_avg_ms` `-1.90%`
  - `query_limit_avg_ms` `-1.42%`

That follow-up was reverted. The final kept code for this round is the `stream_rows` version only.

## Note

The raw benchmark logs show host-local timestamps around `2026-07-18`.
For task bookkeeping and report naming, this work is recorded under `Friday, July 17, 2026`.
