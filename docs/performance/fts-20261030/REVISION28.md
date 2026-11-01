# Follow-up to the reported 27.5965/50 result

Control: submitted commit `6aeef682f433e93baaee21986e06525d82442f7a` (revision21). The user reports 27.5965/50 on the platform. This follow-up has no official score; full marks are not established.

## Changes

- Share the three immutable built-in IK dictionaries, range wrappers and cache pins through the dictionary hub. Initialization is serialized by dictionary type; parsers hold owning references and immutable readers are shared. Failed initialization is not published. This is dictionary state, not a cache of input text or token results.
- Reuse validated character metadata across the four IK processors. BENG tokens borrow original-input slices only when the complete token is inside the caller-owned input; transient uppercase/Unicode normalization buffers still receive owning copies.
- Use one word-map lookup for insertion/update while preserving initial frequency and last equivalent spelling. An immutable ASCII-letter stopword table falls back to the original collation lookup for unsupported input, including accented letters and padding.
- For natural-language, score-free FTS scans whose consumer needs no output columns or remaining filters, preserve only cardinality rather than copying/projecting unused IDs and scores. Index-back, functional/ES/merged lookup, Boolean and scored paths retain the original behavior. The existing serialized flags byte carries the guard; no serialized layout changes. A debugger confirmed the original English count benchmark uses this path.
- Batch-decode string-diff hex runs through the existing pair unpacker. Rebuild common short-string bytes once per batch; retain the original buffer stride/padding and scalar reference. Specialize constant 16-byte BINARY document-ID comparisons, preserving the general byte/length/null fallback.

## Validation

The complete observer build passed. Twelve mysqltest suites passed against revision21 results: the nine existing suites plus stopword_ascii_lookup, beng_input_views and fts_cardinality_only. Coverage includes long mixed-lifetime token streams; all built-in stopwords and accent equivalence; cardinality-only, DISTINCT, value-dependent aggregates, joins, limits/offsets, prepared empty/missing terms and rollback. Eight concurrent tokenizer clients and 100 same-physical-index projected scores matched the control. All four benchmark counts remain 8001/11000/7332/20.

Eight clients simultaneously acquiring cold shared dictionaries also matched revision21. RSS after warmup and three sets of 20000 tokenizer calls was 374124/356816/322576/312432 KiB (diagnostic, not a general leak proof). The actual inline hex decoder and batch helpers passed 20000 randomized ASAN/UBSAN cases against independently packed bytes and the original scalar implementation, including odd/even runs, binary alphabets, short/long strings and output guards. This codec sanitizer test does not instrument the whole server. All 33 changed C++ sources/headers match the VM build tree after LF normalization; see revision28-source-manifest.json.

## Fresh-instance full benchmark

The original benchmark, data, SQL, timing/scoring logic and checked-in baseline are unchanged. Each of four runs starts with a new disposable test instance, in control/candidate/candidate/control order. Both binaries use CPU=2, memory=3G, datafile=2G, log_disk=2G; each run uses rows=20000, batch=500, tokenizer rounds=3000, query rounds=200, samples=3, warmup=30, SKIP_LOAD=0. Startup is outside timing, followed by a fixed 20-second readiness settling period. Every instance is removed after termination. No compiler or profiler competes with timing.

| Metric | Control mean | Candidate mean | Time reduction |
| --- | ---: | ---: | ---: |
| build_ik_all_sec | 14.1110 | 13.4880 | 4.41% |
| build_ik_content_sec | 10.8305 | 10.6345 | 1.81% |
| build_beng_en_sec | 5.1325 | 5.3400 | -4.04% |
| tokenize_ik_avg_ms | 0.50840 | 0.48395 | 4.81% |
| tokenize_beng_avg_ms | 0.38410 | 0.38720 | -0.81% |
| query_cn_avg_ms | 2.59985 | 1.95870 | 24.66% |
| query_beng_avg_ms | 3.74325 | 2.66805 | 28.72% |
| query_mixed_avg_ms | 2.81650 | 2.19490 | 22.07% |
| query_limit_avg_ms | 0.70070 | 0.80010 | -14.19% |

Category reductions are build 0.73%, tokenization 2.00%, query 15.32%, with an equal-weight mean of **6.02% additional local time reduction**. SELECT1 changed from 0.32695 to 0.32965 ms (0.83% slower). English build, English SQL-loop tokenization and LIMIT regressed in this comparison; these results are not omitted. These are incremental VM results against the previous submission, not against the official pristine baseline, and cannot be converted into a predicted platform score. The 50% full-score threshold is not established.

Raw fresh reports and calculation are revision28-clean-0-revision21.txt, revision28-clean-1-revision28.txt, revision28-clean-2-revision28.txt, revision28-clean-3-revision21.txt and revision28-comparison.json. Earlier reused-instance runs and prototype microbenchmarks are retained (revision24-micro.json, revision25/revision27 reports and revision26-query.json). Reused-instance measurements showed considerable startup/replay interference and did not establish an overall gain; they are not substituted for this fresh comparison.
