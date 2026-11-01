# Bounded local DDL concurrency and posting-list union

This candidate retains revision39 and adds revision41's score-free union optimization plus revision42's bounded local DDL concurrency. It has no new official score yet.

## Changes and evidence

Query-only CPU sampling of a real 20,000-row index identified posting-list merging as the main foreground cost. Revision41 uses contiguous fixed-array storage and a small local iterator-pointer array, and combines unlimited cardinality-only accounting once per batch. Boolean queries, scored retrieval, filtering and pagination retain their existing paths. Its complete fresh ABBA comparison against revision39 is in `revision41-comparison.json`: build -8.0593%, tokenization -0.1401%, query +18.7355%, overall +3.5120%. Construction variability is retained in that comparison.

In small-memory mode the local DDL builder previously ran one worker, serializing independent full-text auxiliary-index builds. Revision42 allows at most two workers when CPU and memory budgets permit. It reserves the minimum process budget and one GiB per worker; 3GiB/two CPUs permits two workers, while 2GiB/two CPUs and 3GiB/one CPU retain one. Normal-mode concurrency remains 16. Dependencies, snapshots, sorting and durable writes are unchanged. This resource policy applies to the shared local DDL builder, so its effect is broader than full-text DDL.

The three guard configurations were actually started and their `/proc/PID/task/*/comm` worker counts checked. All 16 fresh differential regression suites and live document batching passed. Coverage includes scored/Boolean/function retrieval, limits and offsets, prepared execution, parser configurations, Unicode, long LOBs, composite keys, DML, rebuilding, and 2048 unique terms crossing batch boundaries. All 44 changed C++ files matched the VM build by normalized SHA256.

Separate INFO diagnostics show `DdlBuild0` and `DdlBuild1` executing independent auxiliary-index SQL with overlapping intervals and successful completion. The diagnostic process's sampled peak RSS was 438,052 KiB. This is a synthetic workload observation, not a guarantee for other datasets. INFO diagnostic timings are excluded from performance comparisons.

## Complete local ABBA comparison

Fresh instances ran revision41/revision42/revision42/revision41, with two CPUs, 3GiB memory budget, 2GiB datafile and 2GiB redo budget. Each waited 20 seconds after readiness, outside timing. Official benchmark files and parameters were unchanged: 20,000 rows, batch 500, 3,000 tokenization rounds, 200 query rounds, three samples, warmup 30, and SKIP_LOAD=0. No compilation, profiling or archive compression overlapped measurement.

| Metric | Revision41 | Revision42 | Time reduction |
| --- | ---: | ---: | ---: |
| IK title/content build, seconds | 12.1855 | 7.1890 | 41.00% |
| IK content build, seconds | 10.0930 | 5.3635 | 46.86% |
| BENG build, seconds | 5.4420 | 2.7220 | 49.98% |
| IK tokenize, ms | 0.5625 | 0.5445 | 3.20% |
| BENG tokenize, ms | 0.45535 | 0.45355 | 0.40% |
| Chinese query, ms | 1.8249 | 1.85835 | -1.83% |
| English query, ms | 2.2078 | 2.35635 | -6.73% |
| Mixed query, ms | 1.8652 | 1.99935 | -7.19% |
| LIMIT query, ms | 0.76295 | 0.83590 | -9.56% |

Category reductions: build **45.9482%**, tokenization **1.7977%**, query **-6.3288%**. Overall additional local reduction: **13.8057%**. All query hit counts remained 8001/11000/7332/20. SELECT 1 improved 0.5450% and is not scored. Negative results have not been omitted.

These are local comparisons, not an official score or proof of full marks. They cannot be added to the user's previous points. Official baseline/report inconsistencies remain unresolved; benchmark SQL, data, timing, scoring and baseline files are unchanged.
