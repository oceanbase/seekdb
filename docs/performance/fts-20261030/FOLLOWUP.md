# 2026-10-31 follow-up

The previous submitted commit e0037010a12847ab85370116293be72203f4a660 scored 7.664/50 on the platform. This follow-up is not a full-score claim.

## Changes

- Dictionary-loader hits acquire a shared lock and take the handle reference before releasing it. Misses reacquire the writer and repeat the lookup before creation. The map owns loaders for its lifetime; handle references are atomic.
- IK range lookup compares bytes and lengths directly, matching the existing UTF8MB4_BIN `strnncoll` implementation. Range order and selection order are unchanged.
- Per-invocation term maps omit bucket locks. All five construction sites use stack-local maps and complete consumption before returning; the shared dictionary-loader map and stopword set retain their synchronization.
- Repeated terms update their existing hash node in place. Both frequency and the latest collation-equivalent spelling are preserved, including cafe/café; initial-count behavior is unchanged.
- Score elision now retains the original retrieval lifecycle for document-column references outside MATCH. A new regression exposed incorrect sorted OFFSET pages with a TOP-N lookup filter; the guard fixes this case while count-only retrieval remains score-free.
- An experimental replacement for the small posting-list merger showed no stable gain and was removed.

## Correctness

The complete observer build passed. Six mysqltest suites passed against results generated with the pristine official binary: score_elision, tokenize_config_reuse, count_limited_view, normalizer_ascii, dictionary_hotpath and posting_union.

New coverage includes multilingual dictionary ranges, quantifiers, smart/max-word modes, latest-spelling collation grouping, 4096-row posting unions, eight/nine-term boundaries, overlapping and missing terms, sorted OFFSET pages, value-dependent limited views, document predicates, and prepared-query reuse. Eight concurrent tokenizer clients produced byte-identical results to the official binary. One hundred projected relevance scores rounded to eight decimals were identical on the same physical index. Official query hits remained 8001/11000/7332/20. The count-only limited-view plan still has calc_relevance=false and is_index_back=false.

## Measurements and limits

The unchanged official benchmark was run sequentially on the same isolated VM and database settings. Raw final reports are revision16-baseline.txt and revision16-candidate.txt; revision16-comparison.json applies the documented three-group formula. Final group improvements were -0.73% build, -8.23% SQL-loop tokenization and 26.44% query, giving 5.83% overall locally. This does not meet the 50% reduction threshold.

The VM results varied considerably: an earlier intermediate pair, saved as revision13-baseline.txt and revision13-candidate.txt, measured 17.04% overall. In particular, per-statement overhead is much larger than the platform's reported SELECT 1 latency. These reports are retained to show the variation, not to substitute a selected run for the official score.

Auxiliary SQL BENCHMARK samples isolate tokenizer execution and do not replace the official metrics. Five samples of 100000 calls per parser compared the pristine binary with the final candidate: IK 110.06 to 78.14 microseconds (29.0% lower); BENG 25.01 to 18.51 microseconds (26.0% lower). Raw samples are in revision16-micro.json.

revision16-source-manifest.json checks all 20 modified C++ files against the VM sources, normalizing CRLF to LF. The VM checkout reports pristine HEAD with git_dirty=1 because the Windows worktree changes were applied to that isolated build tree. No document or query-result cache is introduced.
