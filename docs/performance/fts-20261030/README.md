# Full-text retrieval performance validation

Base: official `5bce9e06d1281347ad61344e4b01fab9158c58a2`. Both binaries were tested sequentially on one Ubuntu VM (4 vCPU, 8 GB RAM), with cpu_count=2, memory_budget=3G, datafile_size=2G and log_disk_size=2G. No compiler or profiler ran during timing. `source-manifest.json` records normalized SHA256 hashes of the candidate source files, checked against the submitted checkout.

## Changes

- Elide BM25 only when natural-language index scans use MATCH for existence. Function lookups, Boolean search, score projections, nonzero thresholds and parameter constraints retain their semantics. Complete score-free initialization, estimation, skip-mask and TAAT paths.
- Use an existence-only DAAT union loop that avoids relevance collection, weighting and score filtering. Do not use it for Boolean or scored searches.
- Allow order simplification and projection pruning for pure full-text statements; other rewrite rules remain disabled. A count-only parent can discard a limited view's ordering unless FETCH WITH TIES is used. Value-sensitive aggregates and parent filters retain ordering. Removing unused view outputs avoids unnecessary row-key/data-table lookups. The included execution plan shows calc_relevance=false and is_index_back=false for the limited-count query.
- Reuse at most eight parsed built-in parser configurations per thread, keyed by parser/version and complete configuration bytes (up to 2048 bytes); preserve parser-independent fields. No document or query-result cache is used.
- Avoid allocating a lowercase copy for already-lowercase ASCII tokens. Uppercase, non-ASCII and multibyte encodings retain charset-aware conversion. Returned views obey the existing until-next-token lifetime.
- Avoid a temporary stopword allocator for matching charsets while preserving collation comparison. Byte-equality shortcuts fall back to the original collation comparator. DDL scheduling and build parallelism use the original implementation.

## Correctness

Full observer build passed. Existing score_elision regression and new count_limited_view, tokenize_config_reuse and normalizer_ascii suites passed against results recorded from the official binary. Coverage includes projected scores and ordering, prepared zero/nonzero thresholds, forced index/function paths, accent-equivalent grouping, stopwords, a 301-term query, configuration eviction, ASCII/Unicode alternation, limited counts with offset, empty matches, nullable/value-dependent counts, SUM and parent filters.

One hundred projected scores (rounded to eight decimals by the comparison query) on the same unchanged physical index were byte-identical between official and candidate binaries. The unchanged official benchmark returned exactly 8001, 11000, 7332 and 20 hits. No benchmark SQL, input data, timing logic, baseline or scoring code was changed.

## Timings

The unchanged official benchmark used ROWS=20000, BATCH=500, ROUNDS=3000, QUERY_ROUNDS=200, SAMPLES=3, WARMUP=30 and SKIP_LOAD=0. Raw reports are included. The baseline report's label is `submitted`; its executable was the pristine official binary. The candidate label is `revision10`; the pre-commit VM checkout records baseline HEAD and git_dirty=1.

| Metric | Official baseline | Candidate | Time reduction |
| --- | ---: | ---: | ---: |
| build_ik_all_sec | 14.6940 | 14.0670 | 4.27% |
| build_ik_content_sec | 11.5180 | 11.5140 | 0.03% |
| build_beng_en_sec | 5.6750 | 5.8720 | -3.47% |
| tokenize_ik_avg_ms | 0.4607 | 0.4877 | -5.86% |
| tokenize_beng_avg_ms | 0.3493 | 0.3730 | -6.78% |
| query_cn_avg_ms | 3.7076 | 2.4885 | 32.88% |
| query_beng_avg_ms | 5.1102 | 3.8727 | 24.22% |
| query_mixed_avg_ms | 3.9006 | 2.9519 | 24.32% |
| query_limit_avg_ms | 4.4409 | 0.6369 | 85.66% |

These are local VM measurements, not a claim of official points. Query latency improves, especially for limited counts; index-build timing is essentially unchanged and SQL-loop tokenization timing is slightly worse in this pair. VM/kernel and client overhead introduce variation.

To separate token execution from per-query overhead, an additional SQL BENCHMARK run evaluated each original benchmark text 10000 times per sample (three samples) in each binary, after warmup. IK: 75.32 versus 75.51 microseconds per call (essentially unchanged). BENG: 18.80 versus 16.01 microseconds per call (about 14.8% lower). This auxiliary run does not replace the official benchmark or its scoring; raw samples are in token-micro-comparison.json.

## Reproduction

See [LATEST.md](LATEST.md) for the current follow-up to the reported 16.59/50 result, encoded FTS build sorting, binary-ID merging, default-configuration reuse, eight-suite validation and incremental measurements against dbdd681d.

See [FOLLOWUP.md](FOLLOWUP.md) for the latest dictionary/map optimizations, sorted-page correctness fix, final regression coverage and subsequent measurements. The table above documents the earlier submission.

Build with `cmake --build build_release --target observer -j4`. Start an isolated instance with the resource settings above. Run mysqltest with the checked-in test/result pairs. Set MYSQL and MYSQL_VERBOSE to its obclient command and run tools/benchmark/fts_large_bench.sh with the exact environment values above. The script recreates fts_large_bench: use an isolated test database, never user data.
