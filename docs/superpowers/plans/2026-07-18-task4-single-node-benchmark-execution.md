# Task4 Single-Node Benchmark Execution Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve the single-node `fts_large_bench.sh` build, tokenize, and MATCH categories without changing its workload or regressing Task2/Task3 behavior.

**Architecture:** Retain the already-landed parser reuse path, complete its dictionary lookup fast path, then implement one minimal, testable single-node subfeature for each of the remaining five Task4 areas. The benchmark determines the priority and whether a local fast path remains enabled, but does not permit skipping an area; all distributed PX/GI/shuffle/DAG code is excluded.

**Tech Stack:** C++17, seekdb FTS/storage code, Google Test, CMake, existing mysqltest runner, Bash/Python benchmark scorer.

## Global Constraints

- Run and score exactly `tools/benchmark/fts_large_bench.sh`; do not change it, its baseline JSON, scorer, SQL workload, hit checks, or timing method.
- The score is the equal-weight average of build, tokenize, and query improvement. A 50% improvement is full score; negative improvement is zero; interpret repeated score differences within about two points as noise.
- Only single-node paths are in scope. Do not add PX, cross-node routing, GI distribution, cross-partition shuffle, or distributed DAG monitoring.
- OceanBase commit `81c822ca5cb2d88c3495192d21e6006d6785fbb4` is reference material, not a cherry-pick target.
- Preserve Task2 parser semantics and Task3 custom dictionary behavior. Do not edit their existing test files.
- New Task4-only tests must live under `unittest/task4/`; existing tests and both mysqltests are execution-only regression evidence.
- All production edits use `apply_patch`; public and concurrency/lifetime-sensitive interfaces receive Chinese comments.

---

## File Map

| Path | Responsibility |
| --- | --- |
| `src/storage/fts/dict/ob_ft_dat_dict.*` | Compact token-to-code table used by dictionary matching. |
| `src/storage/fts/dict/ob_ft_range_dict.*` | Ordered range selection before DAT lookup. |
| `src/storage/fts/dict/ob_ft_trie.*` and `ob_ft_dict_def.*` | Token representation and trie input for the compact dictionary. |
| `src/storage/fts/ob_fts_plugin_helper.*` | Parser cache/reuse and token-processing entry point used by `TOKENIZE` and index construction. |
| `src/storage/fts/ik/ob_fast_list.h` | Reusable IK token-node storage. |
| `unittest/task4/test_task4_fts_perf.cpp` | New isolated correctness tests for all new local fast-path helpers. |
| `unittest/task4/CMakeLists.txt`, `unittest/CMakeLists.txt` | Build registration for Task4-only test target. |
| `docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md` | Evidence and explicit exclusions for the six areas. |

### Task 1: Establish reproducible evidence and preserve the current hot path

**Files:**
- Modify: `docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md`
- Verify: existing Task2/Task3 tests and both mysqltests

**Interfaces:**
- Consumes: current `task4-fulltext-build-performance` worktree and `fts_large_bench_baseline.json`.
- Produces: one timestamped baseline record, command output, and fixed expected MATCH hit counts.

- [ ] **Step 1: Record branch and source-object facts**

Run:

```bash
git branch --show-current
git cat-file -t b786266ba3fc07b8437d07c8d1d177580e788cd0
git cat-file -t 81c822ca5cb2d88c3495192d21e6006d6785fbb4
git diff --shortstat b786266ba3fc07b8437d07c8d1d177580e788cd0 81c822ca5cb2d88c3495192d21e6006d6785fbb4
```

Expected: branch is `task4-fulltext-build-performance`; both objects are commits; upstream diff reports 283 files.

- [ ] **Step 2: Run Task2/Task3 regression binaries before further production edits**

Run:

```bash
bash build.sh debug --init
cmake --build build_debug --target test_ft_parser test_fts_plugin test_fts_property test_task3 test_fts_hotpath -j4
build_debug/unittest/storage/fts/test_ft_parser
build_debug/unittest/storage/test_fts_plugin
build_debug/unittest/storage/test_fts_property
build_debug/unittest/storage/test_task3
build_debug/unittest/storage/fts/test_fts_hotpath
```

Expected: all binaries exit zero. If any fails, record the complete failure before changing its owning code.

- [ ] **Step 3: Run the user-specified SQL regressions without modifying them**

Run:

```bash
tools/deploy/mysql_test/ob_test.sh -n ai_funcs -t ai_split_document
tools/deploy/mysql_test/ob_test.sh -n ai_funcs -t ik_custom_dict
```

Expected: both mysqltests pass and no `.result` file changes.

- [ ] **Step 4: Produce the baseline benchmark report**

Run:

```bash
cd tools/benchmark
OUTPUT=./task4_baseline_result.txt LABEL=task4-single-node-baseline bash fts_large_bench.sh
python3 fts_large_bench_score.py task4_baseline_result.txt --baseline fts_large_bench_baseline.json
```

Expected: hit counts are `8001`, `11000`, `7332`, and `20` in CN, BENG, mixed, and limit order; retain report output outside source changes.

- [ ] **Step 5: Commit evidence-only documentation**

```bash
git add docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "docs: record task4 single-node baseline"
```

### Task 2: Complete and verify the dictionary lookup hot path

**Files:**
- Modify: `src/storage/fts/dict/ob_ft_dat_dict.cpp`
- Modify: `src/storage/fts/dict/ob_ft_dat_dict.h`
- Modify: `src/storage/fts/dict/ob_ft_dict_def.cpp`
- Modify: `src/storage/fts/dict/ob_ft_dict_def.h`
- Modify: `src/storage/fts/dict/ob_ft_range_dict.cpp`
- Modify: `src/storage/fts/dict/ob_ft_range_dict.h`
- Modify: `src/storage/fts/dict/ob_ft_trie.cpp`
- Modify: `src/storage/fts/dict/ob_ft_trie.h`
- Create: `unittest/task4/test_task4_fts_perf.cpp`
- Create: `unittest/task4/CMakeLists.txt`
- Modify: `unittest/CMakeLists.txt`

**Interfaces:**
- Consumes: `ObFTSingleToken::set_token(const char *, int32_t)`, `ObArrayHashMap::find(const ObString &, ObFTTokenCode &)`, and Task3 dictionary descriptors.
- Produces: power-of-two dictionary lookup and range binary search that preserve all existing dictionary matches.

- [ ] **Step 1: Write the isolated failing lookup contract**

Create `unittest/task4/test_task4_fts_perf.cpp` with a test that inserts three UTF-8 tokens into `ObArrayHashMap`, finds every inserted token, and confirms a missing token returns `OB_ENTRY_NOT_EXIST`:

```cpp
TEST(Task4FtsPerf, CompactTokenMapFindsInsertedAndMissingTokens)
{
  ObArrayHashMap map;
  ASSERT_EQ(OB_SUCCESS, map.init(3));
  ASSERT_EQ(OB_SUCCESS, map.insert(ObString("alpha"), 7));
  ObFTTokenCode code = -1;
  ASSERT_EQ(OB_SUCCESS, map.find(ObString("alpha"), code));
  ASSERT_EQ(7, code);
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, map.find(ObString("missing"), code));
}
```

Register only `ob_unittest(test_task4_fts_perf)` in `unittest/task4/CMakeLists.txt` and add `add_subdirectory(task4)` to `unittest/CMakeLists.txt`.

- [ ] **Step 2: Run the test to prove the required interface or behavior is absent**

Run: `cmake --build build_debug --target test_task4_fts_perf -j4`

Expected: compilation or assertion failure before the lookup implementation is completed.

- [ ] **Step 3: Implement the minimal safe lookup fast path**

Use `ObFTTokenCode`, `ObFTSingleToken`, and a power-of-two `locator_`; retain linear probing by three slots and explicit bounds. The central lookup remains:

```cpp
uint64_t idx = token.hash() & header_.locator_;
while (header_.data[idx].used) {
  if (header_.data[idx].token.get_token() == token) {
    code = header_.data[idx].code;
    return OB_SUCCESS;
  }
  idx = (idx + 3) & header_.locator_;
}
return OB_ENTRY_NOT_EXIST;
```

Use a binary search over sorted `ObFTRange` boundaries in `find_first_char_range`; return `OB_ENTRY_NOT_EXIST` when no range contains the first token. Keep Task3 descriptor/cache ownership intact and annotate hash capacity, token ownership, and binary-search ordering in Chinese.

- [ ] **Step 4: Run isolated and full parser/dictionary regressions**

Run:

```bash
cmake --build build_debug --target test_task4_fts_perf test_fts_hotpath test_ft_parser test_fts_plugin test_task3 -j4
build_debug/unittest/task4/test_task4_fts_perf
build_debug/unittest/storage/fts/test_fts_hotpath
build_debug/unittest/storage/fts/test_ft_parser
build_debug/unittest/storage/test_fts_plugin
build_debug/unittest/storage/test_task3
tools/deploy/mysql_test/ob_test.sh -n ai_funcs -t ik_custom_dict
```

Expected: all commands pass and `ik_custom_dict` does not alter checked-in results.

- [ ] **Step 5: Commit the dictionary fast path**

```bash
git add src/storage/fts/dict unittest/task4 unittest/CMakeLists.txt docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "perf(fts): accelerate local dictionary lookup"
```

### Task 3: Implement all remaining five single-node Task4 areas

**Files:**
- Modify only after a failing Task4-only test: `src/storage/fts/ob_fts_plugin_helper.*`, `src/storage/fts/ob_fts_struct.*`, or one new focused helper under `src/storage/fts/`.
- Modify: `unittest/task4/test_task4_fts_perf.cpp`
- Modify: `docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md`

**Interfaces:**
- Consumes: parser cache `cached_builtin_parser_`, `ObFTTokenProcessor`, and `ObFTPositionListHolder`.
- Produces: five separately testable single-node subfeatures: local sort/buffer reuse, local build-pipeline reuse, encoded sort key or variable-int64 position list, local FTS plan ordering/range helper, and local build-stage counters.


- [ ] **Step 1: Add five Task4-only red tests, one per remaining area**

Add tests named `LocalSortKeepsCollationOrder`, `LocalBuildBufferReuseKeepsTokenOutput`, `PositionListRoundTripsVariableInts`, `LocalFtsPlanKeepsDocWordOrder`, and `BuildStageCountersAreMonotonic`. They respectively assert sorting `{ "b", "a", "c" }` yields `{ "a", "b", "c" }`, reused build buffers preserve token output, `{0, 1, 127, 128, 4096}` round-trips and corrupt magic fails, doc-word keys remain ordered by existing collation/doc-id semantics, and per-stage counters never decrease.

- [ ] **Step 2: Run each new test red**

Run: `cmake --build build_debug --target test_task4_fts_perf -j4 && build_debug/unittest/task4/test_task4_fts_perf --gtest_filter='Task4FtsPerf.Local*:Task4FtsPerf.PositionListRoundTripsVariableInts:Task4FtsPerf.BuildStageCountersAreMonotonic'`

Expected: every new case fails only because its single-node helper or behavior is absent.

- [ ] **Step 3: Implement the local sorting subfeature and rerun its green test**

Add a compact in-memory FTS key path that uses the existing collation comparator as fallback; do not introduce generic SQL partition/TopN sorting. Run `Task4FtsPerf.LocalSortKeepsCollationOrder`; expected: PASS.

- [ ] **Step 4: Implement the local pipeline-reuse subfeature and rerun its green test**

Reuse parser/token scratch state and build buffers within one local index-build task, with no PX, slice queue, or remote handoff. Run `Task4FtsPerf.LocalBuildBufferReuseKeepsTokenOutput`; expected: PASS.

- [ ] **Step 5: Implement encoded key or variable-int64 position-list subfeature and rerun its green test**

Use an order-preserving encoding only when the existing comparator order is preserved; otherwise use variable-int64 position-list encode/decode with magic/version validation. Run `Task4FtsPerf.PositionListRoundTripsVariableInts`; expected: PASS.

- [ ] **Step 6: Implement the local plan-ordering subfeature and rerun its green test**

Keep only the local FTS doc-word sort/range helper needed by the current DDL path; do not add GI, PX coordinator, or doc-id redistribution. Run `Task4FtsPerf.LocalFtsPlanKeepsDocWordOrder`; expected: PASS.

- [ ] **Step 7: Implement the low-overhead build-stage counter subfeature and rerun its green test**

Record local parse, sort, and write counters/timestamps in the owning build object; do not add tenant monitor maps, virtual tables, or TTL cleanup. Run `Task4FtsPerf.BuildStageCountersAreMonotonic`; expected: PASS.

- [ ] **Step 8: Measure each admitted subfeature and commit the complete single-node set**

After each subfeature, run the exact benchmark and scorer commands from Task 1. Preserve hit counts and record both the result and any score change. Keep the simple local implementation even when a score change is inside the noise band, unless it causes a regression outside that band or harms compatibility.

```bash
git add src/storage/fts src/sql unittest/task4 docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "perf(fts): optimize six single-node build paths"
```

```bash
git add src/storage/fts unittest/task4 docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "perf(fts): optimize single-node build path"
```

### Task 4: Final correctness and performance evidence

**Files:**
- Modify: `docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md`
- Verify: `tools/benchmark/fts_large_bench.sh`, Task2/Task3 binaries, `ai_split_document`, and `ik_custom_dict`.

**Interfaces:**
- Consumes: committed local FTS optimization tasks.
- Produces: verified hit counts, test output, benchmark score, and six-area audit decisions.

- [ ] **Step 1: Invoke verification-before-completion**

Read and follow `superpowers:verification-before-completion` before claiming any build, test, or performance result.

- [ ] **Step 2: Build and run all affected regressions**

Run the Task 2 Step 4 commands plus:

```bash
tools/deploy/mysql_test/ob_test.sh -n ai_funcs -t ai_split_document
```

Expected: every unit binary and both mysqltests exit zero.

- [ ] **Step 3: Run the benchmark twice and score both reports**

```bash
cd tools/benchmark
OUTPUT=./task4_final_a.txt LABEL=task4-single-node-final-a bash fts_large_bench.sh
python3 fts_large_bench_score.py task4_final_a.txt --baseline fts_large_bench_baseline.json
OUTPUT=./task4_final_b.txt LABEL=task4-single-node-final-b bash fts_large_bench.sh
python3 fts_large_bench_score.py task4_final_b.txt --baseline fts_large_bench_baseline.json
```

Expected: both reports preserve the four hit counts. Report the two scores, their mean, and whether their difference is within the two-point noise band.

- [ ] **Step 4: Close the audit and commit evidence**

For each of the six areas, set the audit state to `已移植`, `路径映射`, or `功能排除` with benchmark or compatibility evidence. Then run:

```bash
git diff --check
git status --short
git add docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "docs: record task4 single-node verification"
```

Expected: no whitespace error; the commit includes only the audit document; user-owned unrelated files remain unstaged.
