# Follow-up to the reported 16.59/50 platform result

The user reported mean improvement 0.1658692203600273 and raw score 33.17384407200546/100 after the dbdd681d submission, equivalent to 16.58692203600273/50. Key feedback metrics are saved in platform-dbdd681d-key-metrics.json. The candidate described here has no platform score yet.

## Changes

- Enable the existing encoded-key/radix sorter for FTS word/document auxiliary index builds with supported keys. Each key must be a signed/unsigned 64-bit integer or a VARCHAR with BINARY, UTF8MB4_BIN or UTF8MB4_GENERAL_CI collation. Other DDL and unsupported key layouts retain their existing sorter. This removes repeated Unicode comparisons of the same posting terms.
- Compare BINARY VARCHAR document IDs directly in the sparse merger, retaining null-first and byte/length ordering, including trailing spaces. Other types and collations retain the original comparator.
- For score-free natural-language unions with at most eight BINARY VARCHAR posting lists, select the smallest current heads directly and advance every equal head. Larger unions, Boolean/scored searches, function lookups and score filters retain the heap path. No document or query-result cache is used.
- Reuse validated default parser-property serialization in TOKENIZE. Eight thread-local entries are keyed by the complete versioned parser name; only successful default configurations of the built-in parsers are cached. These defaults contain compile-time constants. Custom configurations still undergo the original validation. Cached bytes are snapshotted before allocating, then copied into the invocation's allocator, so dictionary loading and reentrant execution cannot leave borrowed cache pointers.

- Fix the pre-existing binary single-NUL encoding special case: composite keys must preserve empty-string versus single-NUL ordering. Binary strings now use regular NUL escaping; nonbinary collation handling is unchanged.

## Validation

The complete observer build passed. Nine mysqltest suites were compared with the immutable dbdd681d control: score_elision, tokenize_config_reuse, count_limited_view, normalizer_ascii, dictionary_hotpath, posting_union, fts_build_sort tokenize_default_reuse and fts_sort_codec. The expanded posting regression also passed with Boolean mode, prepared queries crossing the eight/nine-term boundary, UPDATE, DELETE, INSERT and transaction rollback.

The codec regression compares six composite-key orderings (three collations, ascending/descending) with expected digests recorded using the original unencoded sorter. All six comparisons equal one, including empty strings, embedded NUL, trailing spaces, Unicode and binary document IDs.

Coverage includes 4096-row unions, overlapping/missing terms, sorted OFFSET pages, Unicode and accent-equivalent terms, CI/BIN collations, empty/null inputs, extreme signed/unsigned primary keys, default/custom parser configuration alternation, output-mode changes and rejected configurations after cache hits. Eight concurrent tokenizer clients matched the control's output. One hundred projected relevance scores, rounded to eight decimals, remained identical on the same physical index. Query hits remained 8001/11000/7332/20. The limited-count plan remains score-free and avoids index back.

No benchmark data, SQL, timing/scoring scripts or checked-in baseline was changed. All 24 modified C++ sources were checked against the VM build tree using revision21-source-manifest.json (UTF-8 with CRLF normalized to LF).

## Full local benchmark

The control is **dbdd681d26434d835ab1324bc5beee089f23c7ae**, the previously submitted version, rather than the official pristine baseline. These are incremental measurements on the same isolated VM with the original 20000-row, batch-500, 3000/200-round, three-sample, 30-warmup settings. Resource settings remain cpu_count=2, memory_budget=3G, datafile_size=2G and log_disk_size=2G.

| Metric | dbdd681d control | Candidate revision21 | Time reduction |
| --- | ---: | ---: | ---: |
| build_ik_all_sec | 17.0930 | 15.0020 | 12.23% |
| build_ik_content_sec | 14.1000 | 11.4610 | 18.72% |
| build_beng_en_sec | 6.9080 | 5.5310 | 19.93% |
| tokenize_ik_avg_ms | 0.5808 | 0.4796 | 17.42% |
| tokenize_beng_avg_ms | 0.4623 | 0.4276 | 7.51% |
| query_cn_avg_ms | 3.4066 | 2.9062 | 14.69% |
| query_beng_avg_ms | 5.1484 | 4.0152 | 22.01% |
| query_mixed_avg_ms | 4.0631 | 3.3028 | 18.71% |
| query_limit_avg_ms | 0.7365 | 0.7690 | -4.41% |

Build/tokenization/query category reductions relative to the control are 16.96%/12.47%/12.75%; their equal-weight mean is **14.06% additional local time reduction**. LIMIT was 4.41% slower in this final run; no stable additional LIMIT gain is claimed. Raw reports and calculations are revision21-control-dbdd681d.txt, revision21-candidate.txt and revision21-comparison.json. This is not a platform-score prediction and does not establish the 50% full-score threshold.

## Cross-checks

Because VM statement overhead varies, additional diagnostic runs alternated control/candidate/candidate/control. Each build pass loaded the same original 20000 documents and built all three real indexes. Average build reductions were 15.38%/15.49%/18.64%; both candidate passes were faster than both control passes on each index. Raw samples: revision18-build-abba.json. The final build path additionally fixes the single-NUL composite-key edge case.

The corresponding query comparison measured 21.65%/26.28%/24.05% reduction for Chinese/English/mixed queries. The LIMIT case varied from an 8.29% increase in this cross-check to a 15.49% reduction in the full run; no stable additional LIMIT improvement is claimed. Raw samples: revision19-query.json. The final parser-default change does not alter this MATCH-query path.

An auxiliary SQL BENCHMARK comparison of five 100000-call samples isolated the parser-default change: IK 74.72 to 71.38 microseconds per call (4.47% lower), BENG 18.14 to 16.04 microseconds (11.59% lower). These samples, in revision20-micro.json, supplement rather than replace the official metrics.

The VM build tree retains pristine HEAD with git_dirty=1 because the Windows worktree changes are applied to that isolated source tree. Source hashes identify the tested content. Official score remains pending.
