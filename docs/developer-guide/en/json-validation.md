# JSON migration validation

This record covers local validation of the JSON domain migration on Linux x86-64 on November 3, 2026. The domain and ownership contracts are described in [JSON domain](json.md).

## Baseline and scope

The live baseline is revision `e8a305328a9a594069e07fef46a00fbda4e6ed39`. It already uses the Rust text parser and retains the former C++ JSON tree and domain implementations. The measurements compare domain implementations after that parser migration. They do not compare Rust parsing with RapidJSON.

Standard RFC 8259 text syntax is the intentional behavior change. SQL conversion policies, database scalar tags, stored binary bytes and mutation delta behavior retain their existing contracts. ARRAY and MAP conversion still accepts its SQL collection `NULL` literal.

The migration removes the C++ JSON mutable tree, parser event transport and aggregate serializer. SQL evaluation, LOB access, storage transactions, database scalar conversion and database error mapping remain in C++. Natural Rust interfaces own paths, values, comparison, formatting, hashing, schemas, construction, merging, editing and deltas.

## Product and correctness checks

The final production build includes `//src/observer:seekdb`, `//unittest/oblib:test_json_parser` and `//unittest/oblib:test_json_direct_benchmark`.

| Check | Result and coverage |
| --- | --- |
| Native Rust suite | 117 passed. Includes malformed inputs, ownership after root destruction, stale prepared edits, failed mutation state, scalar widths, 12,000-level traversal and destruction, and deep schema validation. |
| CXX integration | 14 passed. Includes 1,180 frozen hash results and 111 frozen mutation observations. |
| Parser differential | 68 cases, zero failures against frozen status, output length and FNV digests. |
| SQL differential | 31,421 statements, zero mismatches in exact SQL results and errors. |
| Established mysqltest suites | Six passed with their expected result files. |
| Local AI fixture | Eight checks, ten loopback HTTP requests, six temporary models cleaned up. |

The 111 mutation observations compare output length and digest, logical delta digest, exact physical ranges and update state. They are not 111 full-byte golden buffers. A separate delta wire fixture compares full bytes. Likewise, the parser's frozen FNV checks are distinct from the exact-byte comparisons used during independent review.

The SQL differential consists of the following drivers in `unittest/oblib`.

| Driver | Statements | Coverage |
| --- | ---: | --- |
| `json_domain_differential.py` | 22,540 | Queries, schemas, updates, merges, construction, JSON_TABLE and storage. |
| `json_read_differential.py` | 3,977 | Formatting, ordering, equality, database scalars and casts. |
| `json_construction_differential.py` | 1,357 | Constructors, aggregates, signed values and container width boundaries. |
| `json_hash_differential.py` | 1,507 | Grouping, distinct and joins. |
| `json_geometry_differential.py` | 2,040 | GeoJSON output and geometry error behavior. |

Expected SQL errors count as matched behavior, not successful query results. Storage coverage includes six retained partial-update results and rollback after a later failed cast. Geometry coverage includes 440 successful outputs and 1,600 matched errors. The test database lacks the SRS 4326 and 3857 dictionary entries, so those error cases do not establish successful coordinate transformation coverage.

The established suites are `expr.json_standard_syntax`, `array.array_ddl_mysql`, `array.array_arith_op_mysql`, `fts_index.tokenize_function`, `fts_index.parser_properties` and `ai_function.ai_prompt`. They cover strict syntax, recursive path locations, nested JSON_TABLE rescans, constructor widths, collection conversion, token options and consumer output. TOKENIZE also verifies length-bounded error text for non-terminated binary keys.

The local AI fixture exercises OpenAI and Dashscope completion and embedding plus Siliconflow and Dashscope reranking. Its rerank fixture splits 25 documents across two requests. It makes no external model calls. Ollama is not registered through SQL in this configuration and is outside this evidence.

## Independent checks and design decisions

The final focused reviews found no unresolved Major or Blocker defects. Reviewers used isolated source copies and did not author the reviewed patches.

| Decision | Reason and execution evidence |
| --- | --- |
| Complete edits use a flat owned arena. | Text transfers directly from parsing into editing, with one final encoding. The owned and sparse editors share SQL mutation rules. A cached byte-layout alternative required additional invalidation state. The chosen implementation passed 24,624 author comparisons and 2,400 independent exact old/new mutation sequences. |
| JSON_TABLE retains immutable parsed input. | Deferring whole-input binary encoding removes work before traversal. One path evaluator and one output policy serve parsed and binary values. Independent review matched 47,124 framed old/new outputs byte for byte, checked 22,171 nested selections and compared 84 bridge status and error-offset cases. |
| Scalar-only construction skips the traversal plan. | The fast route shares layout, header and scalar codecs with nested encoding. An independent comparison against the pre-change generic function matched exact bytes and errors for 500 generated documents and 6,000 corrupted-input attempts. Nested values retain the iterative cycle-safe planner. |
| Constructor scalars transfer owned buffers. | The shared C++ scalar converter returns either borrowed database bytes or a Rust-owned result. ARRAY and OBJECT transfer owned results into their builder while preserving conversion and construction error stages. |

Parsed object decoding transfers final member pairs into the arena and fills node IDs in place. The refinement passed 2,009 independent exact-byte comparisons against the preceding implementation, including nested duplicates, all merge modes and 12,000-level object chains.

Complete editor application consumes the owner, so failure cannot expose a partially edited result. Sparse editing keeps its existing session and delta rollback contract. Selection ownership and deep destruction were verified after dropping the root handle.

### Optimization scope and maintenance

Small native API timing differences do not justify more representations, cached state or duplicate algorithms. The retained changes remove complete conversions, buffer copies or intermediate collections. They keep one SQL mutation policy and shared binary layout and scalar codecs.

The object-transfer refinement removes the temporary key-only collection. Its native allocation counts fall from 50 to 46 for SET and 54 to 47 for MERGE on the benchmark document. These counts use the native harness's allocator and do not establish a SQL latency improvement under production jemalloc.

Two additional experiments were discarded. Flat child links saved only one allocation on the target document, did not improve its timing and increased cumulative allocation bytes. Avoiding a scalar-discard worklist allocation also had no measurable latency benefit. No further changes were made to chase small API timing gaps.

## SQL performance and process memory

The remote SQL measurements are mixed. Grouping and schema validation are faster on this fixture, while several scalar-oriented SQL paths remain slower. No universal performance parity or speedup is claimed.

The primary run uses `6.13.153.80`, an otherwise lightly loaded Linux host with eight online Xeon Platinum 8369B vCPUs, glibc 2.32 and a 1G budget per instance. The initial load average is 0.44. Both targets use all eight CPUs. A separate source instance holds the fixture. The candidate is commit `5a855b887`; deployed executables have only debug sections removed. The `.text`, `.rodata` and `.data` sections match the corresponding locally tested binaries, and remote executable hashes and loaded inodes were verified.

| Query | Baseline median [min, max], ms | Candidate median [min, max], ms | Median time change |
| --- | ---: | ---: | ---: |
| scan | 49.71 [47.99, 51.88] | 45.38 [45.04, 51.06] | -8.7% |
| valid | 71.94 [71.82, 77.99] | 76.72 [76.42, 84.50] | +6.6% |
| extract | 313.46 [288.23, 326.08] | 355.51 [350.34, 373.14] | +13.4% |
| schema | 499.78 [496.11, 516.74] | 405.40 [376.93, 437.96] | -18.9% |
| set | 478.39 [474.52, 503.60] | 529.94 [515.67, 579.98] | +10.8% |
| merge | 484.19 [461.83, 514.56] | 517.01 [515.72, 582.44] | +6.8% |
| construct | 164.61 [163.91, 176.86] | 183.83 [173.65, 236.86] | +11.7% |
| table | 316.80 [313.27, 343.33] | 344.13 [323.14, 375.40] | +8.6% |
| group | 221.25 [202.56, 226.07] | 164.29 [162.60, 183.57] | -25.7% |
| aggregate | 436.83 [402.40, 461.24] | 420.69 [394.71, 441.93] | -3.7% |

Negative time change means less elapsed time. These are medians of seven alternating measurements per side after two warmups. Query results match exactly and the error count is zero. The unchanged scan control is included. Overlapping ranges, especially for aggregates and constructors, limit conclusions about small differences.

EXTRACT and SET take about 0.42 and 0.52 microseconds more per input row on this fixture. MERGE, construction and JSON_TABLE differ by about 0.33, 0.19 and 0.27 microseconds per row. These costs remain visible for review. Following the ROI constraint, no additional native API special cases were added to close these gaps.

The excluded CPU profile has zero lost samples. Process CPU time is close to elapsed time for the timed SQL batches. The workload is CPU-bound within a SQL worker, with parsing, binary encoding and allocation among the sampled hot paths. The candidate profile attributes 6.30% self samples to the serde document visitor, 4.27% to owned encoding, 4.37% to copying, and about 6% to `je_malloc` and `je_free`. These mixed-workload shares do not isolate the cost of one SQL function.

Whole-process peak RSS is 276,488 KiB for the baseline and 313,528 KiB for the candidate, a difference of 36.2 MiB. RSS after timed trials is 235,780 versus 296,344 KiB. Both values are observations after the same SQL sequence, not JSON-only live allocation measurements. The higher candidate memory footprint remains a review-visible cost.

Remote correctness checks after timing repeat all 31,421 SQL comparisons and the eight local AI checks with zero mismatches. The complete runs, hardware and artifact identities, query plans, CPU samples and profiles are retained in the local evidence directory `/data/1/nijia.nj/test/json-domain-remote-artifacts-20261103/results`.

Earlier local runs showed intermittent near-doubling on both implementations despite low guest load. They are retained as noisy measurements rather than used to claim a regression or improvement. Fixing both task-owned test servers to CPU 15 reduced those phase changes, but did not identify their external cause. The separate remote run above is the primary evidence.

The benchmark uses `json_domain_benchmark.py`. It copies the same 100,000-row source fixture into a uniquely named temporary table on each fresh instance. Both instances use a 1G memory budget and only a `test` database as initialization. The driver checks the copied fixture SHA256, query plans, exact aggregate results and errors. Timed trials follow two warmups and alternate the order of seven runs per side. Profiling runs afterward and is excluded from timing.

Each fixture document is 101 to 106 bytes. The fixture contains 10,488,895 document bytes. Grouping processes 10,000 rows because the baseline hash distribution makes the larger grouping case disproportionately expensive. Every other query processes all 100,000 rows. The earlier local host has 16 online CPUs; the primary remote host has eight. Small differences within the observed ranges do not establish an improvement or regression.

Memory observations use whole-process `VmRSS`, `VmHWM` and `RssAnon`. They include database state, code, allocator retention and prior work. They do not isolate JSON heap allocation. A separate fresh-process parser probe produced identical checksums and zero errors over five measured trials after two warmups. Its peak RSS was about 0.7 to 0.84 MiB higher for the candidate. That observation also includes runtime and code memory.

## Reproduction

Build and run the product integration targets from a configured checkout.

```sh
python3 build.py build -j80 //src/observer:seekdb //unittest/oblib:test_json_parser //unittest/oblib:test_json_direct_benchmark
build/bin/unittest/oblib/test_json_parser
build/bin/unittest/oblib/test_json_direct_benchmark differential
```

The local native run used an isolated manifest copied from `rust/json/Cargo.toml`, with its `src` symlink pointing at the checkout's `rust/json/src`. It used offline cached dependencies and the configured registry mirror. This avoids resolving unrelated workspace dependencies. The repository's Bazel Rust test target currently inherits `-Cpanic=abort`, which prevents that test harness from building on this toolchain. The production panic policy was not changed for this migration.

Run the differential drivers against separate baseline and candidate instances. The storage driver creates task-owned fixture tables and tests mutations there.

```sh
python3 unittest/oblib/json_domain_differential.py --baseline-port 2883 --candidate-port 2884
python3 unittest/oblib/json_read_differential.py --baseline-port 2883 --candidate-port 2884
python3 unittest/oblib/json_construction_differential.py --baseline-port 2883 --candidate-port 2884
python3 unittest/oblib/json_hash_differential.py --baseline-port 2883 --candidate-port 2884
python3 unittest/oblib/json_geometry_differential.py --baseline-port 2883 --candidate-port 2884
python3 unittest/oblib/json_ai_local.py --port 2884
python3 tools/deploy/mysqltest.py run --base-dir /path/to/disposable-instance --work-dir build/mysqltest-json-domain --port 2890 --test-set fts_index.tokenize_function,fts_index.parser_properties,expr.json_standard_syntax,array.array_ddl_mysql,array.array_arith_op_mysql,ai_function.ai_prompt
```

The benchmark source instance must contain `test.json_rt(id, doc)` with exactly 100,000 rows. The two target instances are disposable fresh instances configured alike. Replace the PID arguments with their verified server PIDs. The driver drops only its uniquely named tables.

```sh
python3 unittest/oblib/json_domain_benchmark.py --source-port 2883 --baseline-port 2903 --candidate-port 2904 --baseline-pid BASELINE_PID --candidate-pid CANDIDATE_PID --rounds 7 --profile-dir /path/to/profile-output
```

## Platform coverage and limits

The dependency set remains `cxx`, `memchr`, `ryu`, `serde` and `serde_json` at the existing pinned versions. The domain adds no platform-specific runtime, threads or network dependency.

The native Linux product build and live tests pass. An offline `wasm32-unknown-emscripten` Rust check passes with the installed Emscripten 6.0.12 SDK. The generated CXX translation unit and schema pattern adapter also compile into Wasm objects with the real `em++` compiler.

These checks do not establish a full foreign-platform product link or runtime result. Windows checking stops at the absent MinGW C++ compiler. Apple and Android SDKs are not installed. Full Windows, macOS, iOS, Android and WebAssembly product validation remains a CI responsibility. No large SDK downloads were used for this local run.
