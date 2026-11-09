# Integrated SQL diagnostics before the formal comparison

These are individual smoke runs reported by the cloud operator. They remain
separate from the three-round formal median table. Original result directories
are awaiting the evidence export and will be linked before submission.

| Run | Base vectors | M / session ef_search | CPU Recall@10 | GPU Recall@10 | Status |
|---|---:|---|---:|---:|---|
| candidate108-smoke100k | 100000 | 16 / 512 | 0.762 | 0.863 | Diagnostic; below the chosen 0.95 acceptance setting |
| quality64-smoke100k | 100000 | 64 / 1000 | 1.000 | 1.000 | Single smoke; complete 100-query, 1000-target denominator |

The quality64 run used ef_construction=400, GPU graph degree=128, intermediate
degree=256 and NN-descent iterations=20. The CPU session parameter clamps above
1000, so 4096 is not presented as the shared effective setting.

| quality64 smoke component | CPU seconds | GPU seconds |
|---|---:|---:|
| DDL return | 204.259 | 11.391 |
| DDL start through first query return | 204.391 | 11.574 |

The same-round phase/log-offset proof passed for that smoke. The verified
completion contract was then enabled for quality64-formal100k. These component
times are not three-round medians and are not the final build-speedup claim.
The old M16 result is retained as the quality failure that motivated the
configuration change; it is excluded from the qualified comparison.
