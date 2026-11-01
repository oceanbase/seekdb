# Follow-up to the reported 42.7955/50 platform result

Control: submitted 5c73c72d02dc2d098dbbe256a50eb7c8ca859193 (revision28). The latest user report gives mean improvement 0.42795535167339305 and 42.7955351673393/50; key metrics are archived in platform-5c73c72-20261101.json. This follow-up has no official score and does not establish full marks. Previous implementation and measurements are preserved in REVISION28.md.

## Changes

- IK character classification decodes the first code point once, retaining the original classification priority and UTF16 surrogate conditions. TokenizeContext computes its charset identity once; valid UTF8 ASCII bytes take an equivalent letter/digit/other path. Multibyte validation remains on the original charset implementation.
- The English whitespace tokenizer bypasses the generic character-length call only for ASCII in charsets with minimum length one. Normalization scratch storage is reset when conversion is actually needed; caller-owned lowercase ASCII views need no scratch storage. Uppercase and Unicode conversion remain charset-aware.
- Immutable dictionary character-map probes replace the repeated modulo-by-capacity with an equivalent single wrap subtraction (the existing minimum capacity is 101). Constant-size single-character comparisons avoid general memcmp calls. Hashes, probe order, table layout and dictionary contents remain unchanged.

## Correctness and scope

Complete observer build and 14 mysqltest suites passed against revision28. New differential cases cover every ASCII byte, NUL/control characters, full-width letters/numbers, Unicode classification boundaries, over 4000 deterministic random scalar values, and long alternations between borrowed and converted English tokens. Eight concurrent clients, 100 same-index projected scores and the original counts 8001/11000/7332/20 all matched. The 38 changed C++ sources/headers match the VM build tree after LF normalization. UTF16 classification conditions are preserved by construction; the new SQL differential cases use UTF8 and do not establish independent UTF16 coverage.

No benchmark data, SQL, rounds, timing, scoring or baseline was changed. Four original full benchmarks used fresh temporary instances in control/candidate/candidate/control order, CPU=2/memory=3G/datafile=2G/log_disk=2G, rows=20000, batch=500, rounds=3000, query_rounds=200, samples=3, warmup=30, SKIP_LOAD=0. Startup was outside timing with the same 20-second readiness wait as revision28. No compilation or profiler ran concurrently. Temporary instances were removed after termination.

| Metric | Control | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| build_ik_all_sec | 13.885500 | 13.430500 | 3.28% |
| build_ik_content_sec | 10.031000 | 11.099500 | -10.65% |
| build_beng_en_sec | 5.756000 | 5.438500 | 5.52% |
| tokenize_ik_avg_ms | 0.487200 | 0.481550 | 1.16% |
| tokenize_beng_avg_ms | 0.469250 | 0.383000 | 18.38% |
| query_cn_avg_ms | 2.014000 | 1.824700 | 9.40% |
| query_beng_avg_ms | 2.882400 | 2.460900 | 14.62% |
| query_mixed_avg_ms | 2.200050 | 2.073550 | 5.75% |
| query_limit_avg_ms | 0.707050 | 0.746250 | -5.54% |
| select1_avg_ms | 0.327500 | 0.325350 | 0.66% |

The equally weighted category reductions are build **-0.62%**, tokenization **9.77%**, query **6.06%**, for **5.07% additional local time reduction**. IK content-index construction regressed 10.65%, and LIMIT regressed 5.54%; these are retained in the table rather than omitted. The VM has considerable statement overhead and variation. These results cannot be converted into a predicted official score or added directly to the current 42.7955 points.

## Rejected experiment

A bounded thread-local cache of short CI collation encodings was tested with additional repeated/evicted sort-key regressions. Its full ABBA result was approximately -0.026% overall (revision32-comparison.json), so the entire cache change was removed before the final revision33 build and validation. The released implementation contains no such sort-key cache. Prototype character/tokenizer microbenchmarks are retained in revision29-micro.json and revision30-micro.json; they do not replace full benchmark metrics.
