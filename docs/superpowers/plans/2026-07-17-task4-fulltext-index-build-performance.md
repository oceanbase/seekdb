# Task4 Fulltext Index Build Performance Implementation Plan

> **2026-07-18 当前执行范围**：尽量完成上游六类优化在 seekdb 单机环境中可适配的功能，以 `tools/benchmark/fts_large_bench.sh` 的实际建索引、TOKENIZE 和查询结果为优先验收。上游 283 文件差异用于寻找实现参考，不按路径数量验收；PX、跨分区 DAG、GI 等分布式专有部分只保留其可带来单机收益的等价实现。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 seekdb `vldb_2026` 上实现直接影响单机全文索引构建的 Task4 功能，保持 Task2/Task3 能力和检索结果不变，并以 `fts_large_bench.sh` 的正向实测改善为主要验收。

**Architecture:** 以上游提交作为性能实现参考而非逐文件移植目标。按“热路径基础与真实调用链 → Release 服务端 → `fts_large_bench.sh` 基线/优化对比 → 瓶颈驱动的最小后续改动”推进；每项保留中文注释、必要的功能验证和可复现的基准结果。

**Tech Stack:** C++17、OceanBase/seekdb allocator 与 MTL、Google Test、CMake/Make、DDL DAG scheduler、PX、CG macro-block writer、Bash/Python benchmark。

## Global Constraints

- 实现基线固定为 seekdb `vldb_2026` 分支；设计文档基线提交为 `a0ffb3c3`，设计提交为 `1e2eeef63`。
- 功能来源固定为上游 `81c822ca5cb2d88c3495192d21e6006d6785fbb4`，差异父提交固定为 `b786266ba3fc07b8437d07c8d1d177580e788cd0`。
- 覆盖分词热路径、解析器复用、token/停止词、排序、构建流水线、编码/位置列表、单机 SQL 计划适配和 DDL 可观测性；按 `fts_large_bench.sh` 的瓶颈和可验证性排序实施。
- 不改变用户可见的分词结果、MATCH 命中集合、排序结果和 `fts_large_bench.sh` 计时口径。
- Task2、Task3 已有能力必须保留；当前工作区中的未跟踪文件属于用户，不允许删除或覆盖。
- 不启用 delta + zigzag + PFor position-list 编码；当前只启用 variable-int64。
- 每个新增类、公开接口、关键内部接口以及行为修改块必须写中文职责/修改说明注释。
- 所有实现文件只能通过 `apply_patch` 修改；格式化和生成器命令只用于机械生成其声明的产物。
- 不创建、修改或提交测试文件；功能验证只运行已有单元测试和 `fts_large_bench.sh`，避免与该基准无关的大范围移植。

## 当前执行任务（替代后文历史 Task 3–8）

1. 完成并接入分词容器、五种解析器复用、token/停止词快路径，修复真实建索引调用链中的生命周期问题。
2. 针对 benchmark 建索引阶段依次评估并适配排序、FTS 构建流水线、sort key/位置列表、单机 SQL 计划和 DDL DAG 可观测性中的有效子项。
3. 构建并启动隔离的 Release seekdb 实例，运行固定配置的 `fts_large_bench.sh`，记录 build、TOKENIZE、MATCH 指标与命中数。
4. 每项改动后复跑同配置基准；只保留具备正确性证据且带来收益、或解决基准阻塞问题的实现。
5. 最终提交只包含实际采用的功能、中文接口/修改注释和基准报告；不修改或提交测试文件，历史 283 行审计保留作参考。

---

## Upstream 差异事实与目录映射

上游差异审计命令：

```bash
git diff --name-status \
  b786266ba3fc07b8437d07c8d1d177580e788cd0 \
  81c822ca5cb2d88c3495192d21e6006d6785fbb4
```

当前基线上的精确分类为：194 个修改文件已存在但内容分叉，28 个上游修改路径在 seekdb 中不存在，59 个新增文件可创建，2 个删除文件仍存在。不存在可直接无冲突套用的修改文件，因此禁止把整个上游提交一次性 cherry-pick 到工作区。

上游与 seekdb 的已知路径映射：

| 上游路径 | seekdb 路径/处理 |
| --- | --- |
| `deps/oblib/src/lib/mysqlclient/ob_mysql_proxy.h` | `deps/oblib/src/common/mysqlclient/ob_mysql_proxy.h` |
| `src/share/scheduler/ob_independent_dag.*` | `src/observer/scheduler/ob_independent_dag.*` |
| `src/share/scheduler/ob_tenant_dag_scheduler.*` | `src/observer/scheduler/ob_tenant_dag_scheduler.*` |
| `src/share/ob_order_perserving_encoder.*` | `src/storage/ob_order_perserving_encoder.*` |
| `src/share/ob_fts_index_builder_util.*` | `src/sql/resolver/ddl/ob_fts_index_builder_util.*` |
| `src/objit/CMakeLists.txt` | 不创建；只更新现有 `src/objit/include/objit/common/ob_item_type.h`，构建由顶层目标接入 |
| `mittest/simple_server/*` | 测试适配到 `unittest/storage/ddl/*` 和 `unittest/storage/ddl/CMakeLists.txt` |
| `src/share/inner_table/ob_inner_table_schema.*` | 修改 `ob_inner_table_schema_def.py` 后运行 seekdb 的 `generate_inner_table_schema.py`；生成物保持未跟踪，不纳入提交 |
| `src/observer/table/fts/*` | seekdb 不含 Table API FTS CG service；将其中 SQL/DDL 共用行为接入 `src/storage/ddl/*`，审计表记录该路径为“目录裁剪、功能已映射” |

每次查看上游完整实现都对本任务 `Files` 中列出的路径执行同样的只读检查。以下命令以首个热路径容器为精确示例，随后通过 `apply_patch` 将相关声明、实现和中文注释合入当前文件：

```bash
git diff \
  b786266ba3fc07b8437d07c8d1d177580e788cd0 \
  81c822ca5cb2d88c3495192d21e6006d6785fbb4 -- \
  src/storage/fts/ik/ob_fast_segment_array.h
git show 81c822ca5cb2d88c3495192d21e6006d6785fbb4:src/storage/fts/ik/ob_fast_segment_array.h
```

---

### Task 1: 建立隔离工作区、基线证据和 283 文件审计表

**Files:**
- Create: `docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md`
- Read: `docs/superpowers/specs/2026-07-17-task4-fulltext-index-build-performance-design.md`
- Read: `tools/benchmark/fts_large_bench_baseline.json`
- Read: `tools/benchmark/fts_large_bench.sh`

**Interfaces:**
- Consumes: Git objects `b786266...` and `81c822c...`; branch `vldb_2026` at/after `1e2eeef63`.
- Produces: isolated implementation worktree; a 283-row audit keyed by exact upstream path with state `未移植/已移植/路径映射/功能排除` and evidence column.

- [ ] **Step 1: Invoke worktree isolation**

Read and follow `superpowers:using-git-worktrees` before any implementation edit. Create an isolated branch named `task4-fulltext-build-performance` from current `vldb_2026`; do not move, remove, or stage files in the user's original worktree.

- [ ] **Step 2: Verify source objects and branch**

Run:

```bash
git branch --show-current
git cat-file -t b786266ba3fc07b8437d07c8d1d177580e788cd0
git cat-file -t 81c822ca5cb2d88c3495192d21e6006d6785fbb4
git diff --shortstat b786266ba3fc07b8437d07c8d1d177580e788cd0 81c822ca5cb2d88c3495192d21e6006d6785fbb4
```

Expected: implementation branch is `task4-fulltext-build-performance`; both object types are `commit`; diff reports `283 files changed, 23713 insertions(+), 3697 deletions(-)`.

- [ ] **Step 3: Record baseline tests before production changes**

Run from the worktree root:

```bash
bash build.sh debug --init
cmake --build build_debug --target test_ft_parser test_fts_plugin test_fts_property test_task3 -j4
build_debug/unittest/storage/fts/test_ft_parser
build_debug/unittest/storage/test_fts_plugin
build_debug/unittest/storage/test_fts_property
build_debug/unittest/storage/test_task3
```

Expected: debug configuration succeeds and all four existing binaries exit 0. Record exact counts and any pre-existing warnings in the audit document; a baseline failure stops implementation and is reported as pre-existing evidence.

- [ ] **Step 4: Create the exact upstream audit table**

Use `git diff --name-status` from the section above. Add one Markdown row per path with upstream status, owning Task 2–7, seekdb target path, current state, and verification evidence. The document header must contain these fixed totals:

```markdown
| 上游状态 | 数量 |
| --- | ---: |
| 修改且当前路径分叉 | 194 |
| 修改但当前路径不存在 | 28 |
| 新增且无同名冲突 | 59 |
| 删除且当前路径存在 | 2 |
| 合计 | 283 |
```

- [ ] **Step 5: Commit the audit scaffold**

```bash
git add docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "docs: add task4 upstream port audit"
```

Expected: commit contains only the audit document and all 283 entries start as `未移植`.

---

### Task 2: 分词容器、token 模型和五种解析器复用

**Files:**
- Create: `src/storage/fts/ik/ob_fast_segment_array.h`
- Create: `src/storage/fts/ik/ob_fast_list.h`
- Create: `src/storage/fts/ik/ob_ik_char_util.cpp`
- Create: `src/storage/fts/ob_i_ft_parser.h`
- Create: `src/storage/fts/ob_ft_token_processor.h`
- Create: `src/storage/fts/ob_ft_token_processor.cpp`
- Create: `src/storage/fts/ob_fts_stop_token_check.h`
- Create: `src/storage/fts/ob_fts_stop_token_check.cpp`
- Create: `unittest/storage/fts/test_fts_hotpath.cpp`
- Modify: `unittest/storage/fts/CMakeLists.txt`
- Modify: `src/plugin/adaptor/ob_plugin_ftparser_adaptor.h`
- Modify: `src/plugin/adaptor/ob_plugin_ftparser_adaptor.cpp`
- Modify: `src/plugin/interface/ob_plugin_ftparser_intf.h`
- Modify: `src/plugin/sys/ob_plugin_helper.cpp`
- Modify: `src/share/text_analysis/ob_text_analyzer.h`
- Modify: `src/share/text_analysis/ob_text_analyzer.cpp`
- Modify: `src/storage/fts/ik/ob_ik_arbitrator.h`
- Modify: `src/storage/fts/ik/ob_ik_arbitrator.cpp`
- Modify: `src/storage/fts/ik/ob_ik_char_util.h`
- Modify: `src/storage/fts/ik/ob_ik_cjk_processor.h`
- Modify: `src/storage/fts/ik/ob_ik_letter_processor.h`
- Modify: `src/storage/fts/ik/ob_ik_processor.h`
- Modify: `src/storage/fts/ik/ob_ik_processor.cpp`
- Modify: `src/storage/fts/ik/ob_ik_quantifier_processor.h`
- Modify: `src/storage/fts/ik/ob_ik_surrogate_processor.h`
- Modify: `src/storage/fts/ik/ob_ik_token.h`
- Modify: `src/storage/fts/ik/ob_ik_token.cpp`
- Modify: `src/storage/fts/ob_beng_ft_parser.h`
- Modify: `src/storage/fts/ob_beng_ft_parser.cpp`
- Modify: `src/storage/fts/ob_fts_doc_word_iterator.h`
- Modify: `src/storage/fts/ob_fts_doc_word_iterator.cpp`
- Modify: `src/storage/fts/ob_fts_plugin_helper.h`
- Modify: `src/storage/fts/ob_fts_plugin_helper.cpp`
- Modify: `src/storage/fts/ob_fts_struct.h`
- Modify: `src/storage/fts/ob_fts_struct.cpp`
- Modify: `src/storage/fts/ob_ik_ft_parser.h`
- Modify: `src/storage/fts/ob_ik_ft_parser.cpp`
- Modify: `src/storage/fts/ob_ngram_ft_parser.h`
- Modify: `src/storage/fts/ob_ngram_ft_parser.cpp`
- Modify: `src/storage/fts/ob_ngram2_ft_parser.h`
- Modify: `src/storage/fts/ob_ngram2_ft_parser.cpp`
- Modify: `src/storage/fts/ob_whitespace_ft_parser.h`
- Modify: `src/storage/fts/ob_whitespace_ft_parser.cpp`
- Modify: `src/storage/fts/utils/ob_ft_ngram_impl.h`
- Modify: `src/storage/fts/utils/ob_ft_ngram_impl.cpp`
- Modify: `src/storage/fts/dict/ob_ft_dat_dict.*`
- Modify: `src/storage/fts/dict/ob_ft_dict.*`
- Modify: `src/storage/fts/dict/ob_ft_dict_def.*`
- Modify: `src/storage/fts/dict/ob_ft_range_dict.*`
- Modify: `src/storage/fts/dict/ob_ft_trie.*`
- Modify: `src/storage/fts/dict/ob_gen_dic_loader.*`
- Delete: `src/storage/fts/ob_fts_stop_word.h`
- Delete: `src/storage/fts/ob_fts_stop_word.cpp`
- Test: `unittest/storage/fts/test_ft_parser.cpp`
- Test: `unittest/storage/test_fts_plugin.cpp`
- Test: `unittest/storage/test_task3.cpp`

**Interfaces:**
- Consumes: existing `ObIAllocator`, collation functions, Task3 custom dictionary descriptors and parser properties.
- Produces: `ObFastSegmentArray<T>`, `ObFastList<T>`, `ObIFTParser::reuse_parser(const char *, int64_t)`, `TokenizeContext::reuse_context/current_char_and_type/reset_resource`, `ObFTToken`, `ObFTTokenProcessor::process_token`, `ObStopTokenCheckerGen::get_stop_token_checker_by_coll`.

- [ ] **Step 1: Write container and reuse contract tests**

Create `test_fts_hotpath.cpp` with Google Test coverage for cross-block access, address reuse, list ordering, and abstract parser reuse. The core assertions are:

```cpp
TEST(FTSHotPath, FastSegmentArrayReuseKeepsAllocatedBlocks)
{
  ObArenaAllocator allocator("FtsHotPath");
  ObFastSegmentArray<int64_t, 4> values(allocator);
  for (int64_t i = 0; i < 9; ++i) {
    ASSERT_EQ(OB_SUCCESS, values.push_back(i));
  }
  int64_t *first = &values.at(0);
  values.reuse();
  ASSERT_EQ(0, values.count());
  ASSERT_EQ(OB_SUCCESS, values.push_back(42));
  ASSERT_EQ(first, &values.at(0));
  ASSERT_EQ(42, values.at(0));
}

TEST(FTSHotPath, FastListPreservesBidirectionalOrderAfterReuse)
{
  ObArenaAllocator allocator("FtsHotPath");
  ObFastList<int64_t, 4> values(allocator);
  ASSERT_EQ(OB_SUCCESS, values.push_back(2));
  ASSERT_EQ(OB_SUCCESS, values.push_front(1));
  ASSERT_EQ(OB_SUCCESS, values.push_back(3));
  ASSERT_EQ(1, values.get_first());
  ASSERT_EQ(3, values.get_last());
  values.reuse();
  ASSERT_TRUE(values.empty());
  ASSERT_EQ(OB_SUCCESS, values.push_back(7));
  ASSERT_EQ(7, values.get_first());
}
```

Add `ob_unittest(test_fts_hotpath)` to `unittest/storage/fts/CMakeLists.txt`.

- [ ] **Step 2: Run RED tests**

```bash
cmake --build build_debug --target test_fts_hotpath -j4
```

Expected: compilation fails because `ob_fast_segment_array.h`, `ob_fast_list.h`, and `ObIFTParser::reuse_parser` do not exist.

- [ ] **Step 3: Port the exact hot-path implementation**

Read the upstream diff for every production path listed in this task. Use `apply_patch` to add/merge the implementation. Preserve Task3 dictionary identity/cache fields in `ob_ft_dict_*`, and replace `ObAddWord` only after every caller uses `ObFTTokenProcessor`. Add Chinese comments to allocator ownership, parser reuse preconditions, cached charset function pointer, hash precomputation, and lock-free checker reads.

Required declarations after the patch include:

```cpp
class ObIFTParser : public plugin::ObITokenIterator
{
public:
  // 中文注释：复用解析器处理下一篇文档，只重置逐文档状态，不销毁长生命周期字典和元数据。
  virtual int reuse_parser(const char *fulltext, const int64_t fulltext_len) = 0;
};
```

- [ ] **Step 4: Run GREEN and regression tests**

```bash
cmake --build build_debug --target test_fts_hotpath test_ft_parser test_fts_plugin test_task3 -j4
build_debug/unittest/storage/fts/test_fts_hotpath
build_debug/unittest/storage/fts/test_ft_parser
build_debug/unittest/storage/test_fts_plugin
build_debug/unittest/storage/test_task3
```

Expected: all binaries exit 0; reuse and first-use token sequences are identical; Task3 custom dictionary tests remain green.

- [ ] **Step 5: Update audit and commit**

Mark all Task 2 upstream paths as `已移植` or `路径映射`, attach the four test commands, then commit:

```bash
git add src/plugin src/share/text_analysis src/storage/fts unittest/storage/fts docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "feat(fts): optimize parser hot path and reuse"
```

---

### Task 3: 模块化排序策略和存储层向量排序

**Files:**
- Create: `src/sql/engine/sort/ob_external_merge_sorter.h`
- Create: `src/sql/engine/sort/ob_sort_chunk_builder.h`
- Create: `src/sql/engine/sort/ob_sort_resource_manager.h`
- Create: `src/sql/engine/sort/ob_sort_row_store_mgr.h`
- Create: `src/sql/engine/sort/ob_sort_vec_dump_strategy.h`
- Create: `src/sql/engine/sort/ob_sort_vec_strategy.h`
- Create: `src/sql/engine/sort/ob_sql_sort_resource_manager.h`
- Create: `src/sql/engine/sort/ob_storage_sort_resource_manager.h`
- Create: `src/sql/engine/sort/ob_storage_sort_vec_impl.h`
- Create: `unittest/storage/ddl/test_storage_sort_vec_impl.cpp`
- Modify: `unittest/storage/ddl/CMakeLists.txt`
- Modify: `src/sql/engine/basic/ob_compact_row.h`
- Modify: `src/sql/engine/basic/ob_compact_row.cpp`
- Modify: `src/sql/engine/ob_sql_mem_mgr_processor.h`
- Modify: `src/sql/engine/ob_tenant_sql_memory_manager.h`
- Modify: `src/sql/engine/sort/ob_i_sort_vec_op_impl.h`
- Modify: `src/sql/engine/sort/ob_prefix_sort_vec_op_impl.h`
- Modify: `src/sql/engine/sort/ob_prefix_sort_vec_op_impl.ipp`
- Modify: `src/sql/engine/sort/ob_sort_compare_vec_op.h`
- Modify: `src/sql/engine/sort/ob_sort_compare_vec_op.ipp`
- Modify: `src/sql/engine/sort/ob_sort_vec_op_chunk.h`
- Modify: `src/sql/engine/sort/ob_sort_vec_op_impl.h`
- Modify: `src/sql/engine/sort/ob_sort_vec_op_impl.ipp`
- Modify: `src/sql/engine/sort/ob_sort_vec_op_store_row_factory.h`
- Modify: `unittest/storage/blocksstable/cs_encoding/ob_row_vector_converter.h`

**Interfaces:**
- Consumes: `RowMeta`, `ObSortKeyStore`, `ObIVector`, temp row store and SQL memory manager.
- Produces: `ObISortStrategy::sort_inmem_data`, three concrete sort strategies, `ObSortResourceManager`, `ObSortChunkBuilder`, `ObExternalMergeSorter`, `ObSortRowStoreMgr`, `ObStorageVecSortImpl::add_batch/sort/get_next_batch/merge_sort_chunks`.

- [ ] **Step 1: Port only the upstream storage-sort test into unittest**

Use `mittest/simple_server/sort/test_ob_storage_sort_vec_impl.cpp` at the target commit as the exact test source. Adapt includes to existing unittest helpers and register:

```cmake
storage_unittest(test_storage_sort_vec_impl)
```

The test must assert in-memory ordering, forced dump ordering, multi-chunk merge ordering, addon-row association, and encoded-key fallback.

- [ ] **Step 2: Run RED test**

```bash
cmake --build build_debug --target test_storage_sort_vec_impl -j4
```

Expected: compilation fails on missing `ob_storage_sort_vec_impl.h` and strategy/resource headers.

- [ ] **Step 3: Port sorting production code**

Apply upstream declarations and implementations for every path listed above. Preserve seekdb's current sort operators while moving responsibilities into the new components. Add Chinese comments to fixed-key length dispatch (2–18), comparator fallback, sort-key/addon lifetime, dump thresholds, maximum 256-way merge, and slice ownership.

- [ ] **Step 4: Run GREEN tests and compile SQL sort users**

```bash
cmake --build build_debug --target test_storage_sort_vec_impl observer -j4
build_debug/unittest/storage/ddl/test_storage_sort_vec_impl
```

Expected: test exits 0; `observer` target links without duplicate sort symbols or unresolved template instantiations.

- [ ] **Step 5: Update audit and commit**

```bash
git add src/sql/engine/basic src/sql/engine/sort src/sql/engine/ob_sql_mem_mgr_processor.h src/sql/engine/ob_tenant_sql_memory_manager.h unittest/storage/ddl unittest/storage/blocksstable/cs_encoding docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "refactor(sql): modularize vector sort framework"
```

---

### Task 4: Encoded sort key、position list 和五列辅助表 schema

**Files:**
- Create: `src/storage/ddl/ob_ddl_encode_sortkey_utils.h`
- Create: `src/storage/ddl/ob_ddl_encode_sortkey_utils.cpp`
- Create: `src/share/ob_fts_pos_list_codec.h`
- Create: `src/share/ob_fts_pos_list_codec.cpp`
- Create: `src/sql/engine/expr/ob_expr_pos_list.h`
- Create: `src/sql/engine/expr/ob_expr_pos_list.cpp`
- Create: `src/share/schema/ob_schema_struct_fts.cpp`
- Create: `src/share/schema/ob_table_schema_fts_index.cpp`
- Create: `unittest/storage/test_fts_encoding.cpp`
- Modify: `src/storage/ob_order_perserving_encoder.h`
- Modify: `src/storage/ob_order_perserving_encoder.cpp`
- Modify: `src/share/schema/ob_column_schema.h`
- Modify: `src/share/schema/ob_schema_struct.h`
- Modify: `src/share/schema/ob_schema_struct.cpp`
- Modify: `src/share/schema/ob_schema_struct_fts.h`
- Modify: `src/share/schema/ob_schema_utils.h`
- Modify: `src/share/schema/ob_schema_utils.cpp`
- Modify: `src/share/schema/ob_table_schema.h`
- Modify: `src/share/schema/ob_table_dml_param.h`
- Modify: `src/share/schema/ob_table_dml_param.cpp`
- Modify: `src/share/schema/ob_table_param.h`
- Modify: `src/share/schema/ob_table_param.cpp`
- Modify: `src/share/schema/ob_table_sql_service.cpp`
- Modify: `src/sql/code_generator/ob_static_engine_cg.cpp`
- Modify: `src/sql/das/ob_das_domain_utils.h`
- Modify: `src/sql/das/ob_das_domain_utils.cpp`
- Modify: `src/sql/engine/expr/ob_expr_eval_functions.cpp`
- Modify: `src/sql/engine/expr/ob_expr_operator_factory.cpp`
- Modify: `src/objit/include/objit/common/ob_item_type.h`
- Modify: `src/share/CMakeLists.txt`
- Modify: `src/sql/CMakeLists.txt`
- Modify: `src/storage/CMakeLists.txt`
- Modify: `unittest/storage/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 `ObFTToken`; Task 3 `RowMeta`, encoded-key sort support; current schema generation model.
- Produces: `ObDDLEncodeSortkeyUtils::prepare_encode_param/encode_row/encode_batch`, `ObFtsSegmentSort`, `ObFTSPositionListStore::encode_and_serialize/deserialize_and_decode`, `ObExprPosList`, `T_FUN_SYS_POS_LIST`, five-column FTS auxiliary schema.

- [ ] **Step 1: Add the upstream encoding tests first**

Create `unittest/storage/test_fts_encoding.cpp` from the target commit and register it with:

```cmake
storage_fts_unittest(test_fts_encoding test_fts_encoding.cpp)
```

Retain round-trip, corrupted magic, corrupted checksum, encoded order, null datum and batch encoding cases.

- [ ] **Step 2: Run RED test**

```bash
cmake --build build_debug --target test_fts_encoding -j4
```

Expected: compilation fails because `ob_fts_pos_list_codec.h` and `ob_ddl_encode_sortkey_utils.h` are missing.

- [ ] **Step 3: Port codec, expression and schema code**

Apply upstream code to the listed files using the seekdb path mapping for order-preserving encoding. Keep the on-disk constants exactly `0xFACE` and version `1`; enable variable-int64 only. Add Chinese comments to serialization ownership, checksum coverage, decode validation order, encoded-key fallback and five-column layout compatibility.

- [ ] **Step 4: Run GREEN and schema regressions**

```bash
cmake --build build_debug --target test_fts_encoding test_fts_property observer -j4
build_debug/unittest/storage/test_fts_encoding
build_debug/unittest/storage/test_fts_property
```

Expected: both tests exit 0; observer links with `T_FUN_SYS_POS_LIST`; existing parser-property schema behavior remains green.

- [ ] **Step 5: Update audit and commit**

```bash
git add src/objit/include/objit/common/ob_item_type.h src/share src/sql src/storage unittest/storage docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "feat(fts): add encoded sort keys and position lists"
```

Before committing, inspect the staged list and unstage paths owned by later tasks; this commit may contain only files explicitly listed in Task 4.

---

### Task 5: 分区本地 FTS 构建、采样、归并和写入流水线

**Files:**
- Create: `src/storage/ddl/ob_ddl_merge_sort_task.*`
- Create: `src/storage/ddl/ob_ddl_sort_provider.*`
- Create: `src/storage/ddl/ob_final_merge_sort_write_task.*`
- Create: `src/storage/ddl/ob_fts_macro_block_write_op.*`
- Create: `src/storage/ddl/ob_fts_sample_pipeline.*`
- Create: `src/storage/ddl/ob_fts_sample_task.*`
- Create: `src/storage/ddl/ob_full_text_index_write_task.*`
- Create: `src/storage/ddl/ob_merge_sort_prepare_task.*`
- Create: `unittest/storage/ddl/test_ddl_pipeline_base.h`
- Create: `unittest/storage/ddl/test_fts_sample_pipeline.cpp`
- Create: `unittest/storage/ddl/test_merge_sort_op.cpp`
- Modify: `src/storage/blocksstable/ob_dag_macro_block_writer.h`
- Modify: `src/storage/blocksstable/ob_macro_block_writer.h`
- Modify: `src/storage/blocksstable/ob_macro_block_writer.cpp`
- Modify: `src/storage/ddl/ob_cg_macro_block_write_op.cpp`
- Modify: `src/storage/ddl/ob_cg_macro_block_write_task.h`
- Modify: `src/storage/ddl/ob_cg_macro_block_write_task.cpp`
- Modify: `src/storage/ddl/ob_cg_macro_block_writer.h`
- Modify: `src/storage/ddl/ob_cg_macro_block_writer.cpp`
- Modify: `src/storage/ddl/ob_cg_micro_block_write_op.cpp`
- Modify: `src/storage/ddl/ob_column_clustered_dag.h`
- Modify: `src/storage/ddl/ob_column_clustered_dag.cpp`
- Modify: `src/storage/ddl/ob_complement_data_task.h`
- Modify: `src/storage/ddl/ob_complement_data_task.cpp`
- Modify: `src/storage/ddl/ob_ddl_independent_dag.h`
- Modify: `src/storage/ddl/ob_ddl_independent_dag.cpp`
- Modify: `src/storage/ddl/ob_ddl_merge_task_v2.h`
- Modify: `src/storage/ddl/ob_ddl_pipeline.h`
- Modify: `src/storage/ddl/ob_ddl_pipeline.cpp`
- Modify: `src/storage/ddl/ob_ddl_struct.h`
- Modify: `src/storage/ddl/ob_ddl_struct.cpp`
- Modify: `src/storage/ddl/ob_ddl_tablet_context.h`
- Modify: `src/storage/ddl/ob_ddl_tablet_context.cpp`
- Modify: `src/storage/ddl/ob_direct_load_type.h`
- Modify: `src/storage/ddl/ob_group_write_macro_block_task.h`
- Modify: `src/storage/ddl/ob_group_write_macro_block_task.cpp`
- Modify: `src/storage/ddl/ob_pipeline.h`
- Modify: `src/storage/ddl/ob_pipeline.cpp`
- Modify: `src/storage/ddl/ob_tablet_slice_writer.h`
- Modify: `src/storage/ddl/ob_tablet_slice_writer.cpp`
- Modify: `src/storage/ddl/ob_writer_args_struct.cpp`
- Modify: `src/storage/direct_load/ob_direct_load_dag_insert_table_row_writer.cpp`
- Modify: `src/storage/direct_load/ob_direct_load_dag_lob_builder.cpp`
- Modify: `src/storage/tablet/ob_tablet_ddl_complete_mds_helper.h`
- Modify: `src/storage/tablet/ob_tablet_ddl_complete_mds_helper.cpp`
- Modify: `src/observer/table_load/dag/ob_table_load_dag_insert_sstable_task.cpp`
- Modify: `src/observer/table_load/dag/ob_table_load_empty_insert_dag.cpp`
- Modify: `unittest/storage/ddl/test_batch_rows_generater.h`
- Modify: `unittest/storage/ddl/test_pipeline_and_op.cpp`
- Modify: `unittest/storage/ddl/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 `ObStorageVecSortImpl`; Task 4 encoded row meta and position list; existing DDL slice/tablet context.
- Produces: `ObColumnClusteredDag::generate_partition_local_fixed_tasks/wait_sample_finish/notify_sample_finished`, `ObFtsSamplePipeline`, two sample operators, `ObDDLSortProvider`, `ObDDLMergeSortTask`, `ObFullTextIndexWritePipeline`, `ObDAGFtsMacroBlockWriteOp`.

- [ ] **Step 1: Adapt upstream pipeline tests before production code**

Port `mittest/simple_server/test_fts_sample_pipeline.cpp` and `test_merge_sort_op.cpp` into the unittest paths listed above. Replace simple-server bootstrap with existing storage DDL test fixtures; preserve assertions for equal-depth boundaries, persisted-range recovery, chunk count reduction, suspended rescheduling and `(word, doc_id)` output order. Register:

```cmake
storage_unittest(test_fts_sample_pipeline)
storage_unittest(test_merge_sort_op)
```

- [ ] **Step 2: Run RED tests**

```bash
cmake --build build_debug --target test_fts_sample_pipeline test_merge_sort_op -j4
```

Expected: compilation fails on missing sample pipeline, merge task and sort provider headers.

- [ ] **Step 3: Port pipeline production code**

Apply upstream code for the exact production paths listed above. Preserve seekdb's existing DDL state fields. Add Chinese comments to condition-variable completion, failure notification, persisted-range idempotency, LightyQueue ownership, atomic chunk claiming, suspended task return, handle reuse locking and column reorder.

- [ ] **Step 4: Run GREEN pipeline tests**

```bash
cmake --build build_debug --target test_fts_sample_pipeline test_merge_sort_op test_pipeline_and_op observer -j4
build_debug/unittest/storage/ddl/test_fts_sample_pipeline
build_debug/unittest/storage/ddl/test_merge_sort_op
build_debug/unittest/storage/ddl/test_pipeline_and_op
```

Expected: all tests exit 0; observer links; no wait path remains blocked after an injected sample failure.

- [ ] **Step 5: Update audit and commit**

```bash
git add src/storage/ddl src/storage/blocksstable src/storage/direct_load src/storage/tablet src/observer/table_load unittest/storage/ddl docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "feat(fts): add partition-local build pipeline"
```

---

### Task 6: FTS Granule Iterator、并行 DDL 计划和统计采样

**Files:**
- Create: `src/sql/engine/px/ob_granule_fts_util.h`
- Create: `src/sql/engine/px/ob_granule_fts_util.cpp`
- Modify: `src/sql/engine/basic/ob_stat_collector_op.h`
- Modify: `src/sql/engine/basic/ob_stat_collector_op.cpp`
- Modify: `src/sql/engine/pdml/static/ob_px_sstable_insert_op.h`
- Modify: `src/sql/engine/pdml/static/ob_px_sstable_insert_op.cpp`
- Modify: `src/sql/engine/px/datahub/components/ob_dh_sample.cpp`
- Modify: `src/sql/engine/px/ob_granule_iterator_op.h`
- Modify: `src/sql/engine/px/ob_granule_iterator_op.cpp`
- Modify: `src/sql/engine/px/ob_granule_pump.h`
- Modify: `src/sql/engine/px/ob_granule_pump.cpp`
- Modify: `src/sql/engine/px/ob_granule_util.h`
- Modify: `src/sql/engine/px/ob_granule_util.cpp`
- Modify: `src/sql/engine/px/ob_px_sub_coord.cpp`
- Modify: `src/sql/engine/table/ob_ddl_block_sample_scan_op.h`
- Modify: `src/sql/engine/table/ob_ddl_block_sample_scan_op.cpp`
- Modify: `src/sql/engine/table/ob_table_scan_op.h`
- Modify: `src/sql/engine/table/ob_table_scan_op.cpp`
- Modify: `src/sql/executor/ob_task_info.h`
- Modify: `src/sql/executor/ob_task_info.cpp`
- Modify: `src/sql/optimizer/ob_del_upd_log_plan.h`
- Modify: `src/sql/optimizer/ob_del_upd_log_plan.cpp`
- Modify: `src/sql/optimizer/ob_join_order.cpp`
- Modify: `src/sql/optimizer/ob_log_del_upd.cpp`
- Modify: `src/sql/optimizer/ob_log_granule_iterator.h`
- Modify: `src/sql/optimizer/ob_log_granule_iterator.cpp`
- Modify: `src/sql/optimizer/ob_log_stat_collector.h`
- Modify: `src/sql/optimizer/ob_log_stat_collector.cpp`
- Modify: `src/sql/optimizer/ob_log_subplan_scan.cpp`
- Modify: `src/sql/optimizer/ob_log_table_scan.cpp`
- Modify: `src/sql/resolver/ddl/ob_alter_table_resolver.cpp`
- Modify: `src/sql/resolver/ddl/ob_create_index_resolver.cpp`
- Modify: `src/sql/resolver/ddl/ob_create_table_resolver.cpp`
- Modify: `src/sql/resolver/ddl/ob_ddl_resolver.h`
- Modify: `src/sql/resolver/ddl/ob_ddl_resolver.cpp`
- Modify: `src/sql/resolver/dml/ob_dml_resolver.h`
- Modify: `src/sql/resolver/dml/ob_dml_resolver.cpp`
- Modify: `src/sql/resolver/expr/ob_raw_expr.h`
- Modify: `src/sql/parser/sql_parser_mysql_mode.y`
- Modify: `src/sql/parser/non_reserved_keywords_mysql_mode.c`
- Test: `tools/deploy/mysql_test/test_suite/ai_funcs/t/ik_custom_dict.test`
- Test: `tools/benchmark/fts_large_bench_setup.sql`

**Interfaces:**
- Consumes: Task 5 DDL DAG sample ranges and slice metadata.
- Produces: `ObGranuleFtsUtil::get_fts_forward_range/calculate_fts_slice_idx_for_task`, `DocidCompare::operator()`, `ObDelUpdLogPlan::prepare_inverted_sort_keys/gen_px_coord_sampling_sort_keys`, FTS stat collector path.

- [ ] **Step 1: Add plan-level failing assertions**

Extend the existing optimizer/DDL test path with assertions that an FTS doc-word auxiliary table produces inverted sort keys and that two doc-id boundaries map to non-overlapping PX ranges. The central comparator assertion is:

```cpp
DocidCompare compare(cmp_func);
ASSERT_TRUE(compare(&docid_1_row, &docid_2_row));
ASSERT_EQ(OB_SUCCESS, compare.ret_);
```

Add a GI utility case that expects slice index and total slice count from a populated `ObGranuleTaskInfo`.

- [ ] **Step 2: Run RED compile/test**

```bash
cmake --build build_debug --target test_pipeline_and_op observer -j4
```

Expected: compilation fails on missing `ObGranuleFtsUtil` or missing FTS sort-key methods.

- [ ] **Step 3: Port SQL plan and execution changes**

Apply upstream hunks for the exact paths above. In `ob_dml_resolver.cpp`, merge rather than overwrite Task2/Task3 changes. Add Chinese comments to FTS-only plan selection, doc-id-only compare, slice math, stat sample propagation and ordinary-query fallback.

- [ ] **Step 4: Run GREEN build and mysqltest regression**

```bash
cmake --build build_debug --target observer test_pipeline_and_op -j4
build_debug/unittest/storage/ddl/test_pipeline_and_op
tools/deploy/mysql_test/ob_test.sh -n ai_funcs -t ik_custom_dict
```

Expected: observer and test target build; unit test exits 0; `ik_custom_dict` result matches its checked-in result without edits.

- [ ] **Step 5: Update audit and commit**

```bash
git add src/sql docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "feat(sql): plan partition-local fulltext builds"
```

---

### Task 7: DDL DAG 监控、虚拟表和租户生命周期

**Files:**
- Create: `src/storage/ddl/ob_ddl_dag_monitor_entry.h`
- Create: `src/storage/ddl/ob_ddl_dag_monitor_entry.cpp`
- Create: `src/storage/ddl/ob_ddl_dag_monitor_mgr.h`
- Create: `src/storage/ddl/ob_ddl_dag_monitor_mgr.cpp`
- Create: `src/storage/ddl/ob_ddl_dag_monitor_node.h`
- Create: `src/storage/ddl/ob_ddl_dag_monitor_node.cpp`
- Create: `src/observer/virtual_table/ob_all_virtual_ddl_dag_monitor.h`
- Create: `src/observer/virtual_table/ob_all_virtual_ddl_dag_monitor.cpp`
- Create: `unittest/storage/ddl/test_ddl_dag_monitor.cpp`
- Modify: `src/observer/omt/ob_multi_tenant.cpp`
- Modify: `src/observer/scheduler/ob_independent_dag.h`
- Modify: `src/observer/scheduler/ob_independent_dag.cpp`
- Modify: `src/observer/scheduler/ob_tenant_dag_scheduler.h`
- Modify: `src/observer/scheduler/ob_tenant_dag_scheduler.cpp`
- Modify: `src/observer/virtual_table/ob_virtual_table_iterator_factory.cpp`
- Modify: `src/share/rc/ob_tenant_base.h`
- Modify: `deps/oblib/src/lib/stat/ob_latch_define.h`
- Modify: `src/share/inner_table/ob_inner_table_schema_def.py`
- Modify: `src/share/inner_table/ob_load_inner_table_schema.cpp`
- Modify: `unittest/storage/ddl/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 5 independent DDL DAG/task types and monitor hooks.
- Produces: `ObDDLDagMonitorMgr::register_node/clean_nodes/get_all_nodes`, `ObDDLDagMonitorNode::alloc_monitor_info/inc_ref/dec_ref`, `ObDDLDagMonitorInfo::record_execute_stat/mark_finished`, `ObAllVirtualDDLDagMonitor::inner_get_next_row`, `__all_virtual_ddl_dag_monitor` schema.

- [ ] **Step 1: Write monitor lifecycle tests**

Create a Google Test that initializes the manager, registers a node, allocates an info object, records two executions, marks task/DAG finished, enumerates the node, cleans finished information and verifies reference-safe destruction. Register:

```cmake
storage_unittest(test_ddl_dag_monitor)
```

The core state assertions are:

```cpp
ASSERT_EQ(2, info->get_schedule_count());
ASSERT_EQ(30, info->get_exec_time_us());
ASSERT_EQ(OB_SUCCESS, info->get_ret_code());
ASSERT_TRUE(node->is_finished());
ASSERT_EQ(1, mgr.get_node_count());
```

- [ ] **Step 2: Run RED test**

```bash
cmake --build build_debug --target test_ddl_dag_monitor -j4
```

Expected: compilation fails because monitor manager/node headers do not exist.

- [ ] **Step 3: Port monitor and virtual-table implementation**

Apply upstream monitor code using the scheduler path mapping. Register MTL lifecycle and virtual table factory. Modify `ob_inner_table_schema_def.py`, then run:

```bash
python3 src/share/inner_table/generate_inner_table_schema.py
```

Use generated files only to validate schema output; do not stage the user's pre-existing untracked generated files. Add Chinese comments to ref-count ownership, RW-lock boundaries, allocator limit, TTL cleanup, finished-info cleanup and virtual-table snapshot behavior.

- [ ] **Step 4: Run GREEN and generated-schema checks**

```bash
cmake --build build_debug --target test_ddl_dag_monitor observer -j4
build_debug/unittest/storage/ddl/test_ddl_dag_monitor
rg -n '__all_virtual_ddl_dag_monitor' src/share/inner_table src/observer/virtual_table
```

Expected: test exits 0; observer links; search finds the schema definition, generated constants and virtual-table iterator registration.

- [ ] **Step 5: Update audit and commit tracked sources only**

```bash
git add deps/oblib/src/lib/stat/ob_latch_define.h src/observer/omt src/observer/scheduler src/observer/virtual_table src/share/rc/ob_tenant_base.h src/share/inner_table/ob_inner_table_schema_def.py src/share/inner_table/ob_load_inner_table_schema.cpp src/storage/ddl/ob_ddl_dag_monitor_entry.* src/storage/ddl/ob_ddl_dag_monitor_mgr.* src/storage/ddl/ob_ddl_dag_monitor_node.* unittest/storage/ddl docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "feat(ddl): expose dag build monitoring"
```

Expected: no generated `ob_inner_table_schema.*` file is staged unless it was already tracked before Task 7.

---

### Task 8: Rootserver、schema/RPC 接线与 283 文件兼容收口

**Files:**
- Modify: `deps/oblib/src/common/mysqlclient/ob_mysql_proxy.h`
- Modify: `src/rootserver/ddl_task/ob_build_mview_task.cpp`
- Modify: `src/rootserver/ddl_task/ob_constraint_task.cpp`
- Modify: `src/rootserver/ddl_task/ob_ddl_redefinition_task.*`
- Modify: `src/rootserver/ddl_task/ob_ddl_retry_task.cpp`
- Modify: `src/rootserver/ddl_task/ob_ddl_scheduler.*`
- Modify: `src/rootserver/ddl_task/ob_ddl_tablet_scheduler.*`
- Modify: `src/rootserver/ddl_task/ob_ddl_task.*`
- Modify: `src/rootserver/ddl_task/ob_fts_index_build_task.*`
- Modify: `src/rootserver/ddl_task/ob_index_build_task.*`
- Modify: `src/rootserver/ddl_task/ob_modify_autoinc_task.cpp`
- Modify: `src/rootserver/ddl_task/ob_partition_split_task.cpp`
- Modify: `src/rootserver/ddl_task/ob_vec_index_build_task.cpp`
- Modify: `src/rootserver/ddl_task/ob_vec_ivf_index_build_task.cpp`
- Modify: `src/rootserver/ob_ddl_service.cpp`
- Modify: `src/rootserver/ob_index_builder.*`
- Modify: `src/rootserver/parallel_ddl/ob_create_index_helper.cpp`
- Modify: `src/share/ob_ddl_common.*`
- Modify: `src/share/ob_rpc_struct.*`
- Modify: `src/sql/resolver/ddl/ob_fts_index_builder_util.*`
- Modify: `src/storage/retrieval/ob_text_retrieval_token_iter.cpp`
- Modify: `docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md`
- Test: `unittest/storage/test_task3.cpp`
- Test: `unittest/storage/test_fts_property.cpp`
- Test: `tools/deploy/mysql_test/test_suite/ai_funcs/t/ik_custom_dict.test`

**Interfaces:**
- Consumes: Tasks 2–7 all public contracts.
- Produces: rootserver scheduling/RPC integration, partition-local DDL flag propagation, complete 283-path disposition, preserved Task2/Task3 behavior.

- [ ] **Step 1: Run pre-integration regression tests**

```bash
cmake --build build_debug --target test_task3 test_fts_property observer -j4
build_debug/unittest/storage/test_task3
build_debug/unittest/storage/test_fts_property
```

Expected: all commands exit 0 before rootserver/schema merge.

- [ ] **Step 2: Port remaining upstream integration hunks**

Apply the upstream changes for every exact production path listed above, using the mysql proxy and FTS builder mappings. Preserve Task2 AI resolver behavior in `ob_dml_resolver.cpp` already merged in Task 6 and preserve Task3 dictionary properties/cache identity in all FTS structs. Add Chinese comments at every conflict resolution explaining both retained behaviors.

- [ ] **Step 3: Close the 283-row audit**

Run the upstream `git diff --name-status` command and inspect every audit row. No row may remain `未移植`. Permitted `功能排除` rows are build/test infrastructure absent from seekdb (`mittest/simple_server`, `src/objit/CMakeLists.txt`) and Table API-only adapters; each such exclusion row must point to the seekdb test or SQL/DDL implementation that covers the same six-system requirement. In addition, an incidental hunk may be excluded only when all three conditions hold: it has no runtime-behavior relationship to the six optimization systems, seekdb has not introduced the corresponding module, and the upstream hunk is limited to a compile dependency or syntax cleanup. This exception must be justified with per-hunk evidence and must not be generalized to adjacent files or directories. Audit row 020 (`src/observer/virtual_table/ob_all_virtual_logservice_cluster_info.cpp`) is one candidate because its only upstream hunk adds the compile-only `src/logservice/ipalf/ipalf_env.h` include and seekdb has no `ObAllVirtualLogServiceClusterInfo` module; only Task 8 may update its final state to `功能排除` after confirming that evidence.

- [ ] **Step 4: Audit Chinese comments and forbidden legacy symbols**

```bash
rg -n 'class ObFastSegmentArray|class ObIFTParser|class ObISortStrategy|class ObFtsSamplePipeline|class ObDDLDagMonitorMgr' src
rg -n 'ObFTWord|ObAddWord|ob_fts_stop_word' src/storage/fts src/sql/engine/expr
git diff --check HEAD~7..HEAD
```

Expected: each new interface declaration is immediately preceded by a Chinese explanation; legacy symbol search has no production call sites; diff check prints nothing.

- [ ] **Step 5: Run integration GREEN tests**

```bash
cmake --build build_debug --target observer test_ft_parser test_fts_plugin test_fts_property test_task3 test_fts_hotpath test_storage_sort_vec_impl test_fts_encoding test_fts_sample_pipeline test_merge_sort_op test_ddl_dag_monitor -j4
build_debug/unittest/storage/fts/test_ft_parser
build_debug/unittest/storage/test_fts_plugin
build_debug/unittest/storage/test_fts_property
build_debug/unittest/storage/test_task3
```

Expected: build exits 0 and every listed test binary exits 0.

- [ ] **Step 6: Commit compatibility integration**

```bash
git add deps/oblib/src/common/mysqlclient/ob_mysql_proxy.h src/rootserver src/share/ob_ddl_common.* src/share/ob_rpc_struct.* src/sql/resolver/ddl/ob_fts_index_builder_util.* src/storage/retrieval/ob_text_retrieval_token_iter.cpp docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git commit -m "feat(fts): integrate optimized ddl build lifecycle"
```

---

### Task 9: 完整验证、benchmark 和完成证据

**Files:**
- Verify: all files recorded in `docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md`
- Verify: `tools/benchmark/fts_large_bench.sh`
- Verify: `tools/benchmark/fts_large_bench_score.py`
- Verify: `tools/benchmark/fts_large_bench_baseline.json`
- Modify only when evidence reveals a defect: the owning Task 2–8 source/test files, followed by a regression test first.

**Interfaces:**
- Consumes: complete Tasks 2–8 implementation.
- Produces: fresh build/test/benchmark evidence meeting design Section 11.

- [ ] **Step 1: Invoke verification-before-completion**

Read and follow `superpowers:verification-before-completion`. Do not claim success based on earlier task runs.

- [ ] **Step 2: Build the full debug target and all unit tests**

```bash
bash build.sh debug --make -j4
cmake --build build_debug --target test_ft_parser test_fts_plugin test_fts_property test_task3 test_fts_hotpath test_storage_sort_vec_impl test_fts_encoding test_fts_sample_pipeline test_merge_sort_op test_ddl_dag_monitor -j4
```

Expected: both commands exit 0 with no compile/link error.

- [ ] **Step 3: Run all targeted unit and integration binaries**

```bash
build_debug/unittest/storage/fts/test_ft_parser
build_debug/unittest/storage/test_fts_plugin
build_debug/unittest/storage/test_fts_property
build_debug/unittest/storage/test_task3
build_debug/unittest/storage/fts/test_fts_hotpath
build_debug/unittest/storage/ddl/test_storage_sort_vec_impl
build_debug/unittest/storage/test_fts_encoding
build_debug/unittest/storage/ddl/test_fts_sample_pipeline
build_debug/unittest/storage/ddl/test_merge_sort_op
build_debug/unittest/storage/ddl/test_ddl_dag_monitor
```

Expected: every binary exits 0; report exact test counts in the final handoff.

- [ ] **Step 4: Run Task3 mysqltest regression**

```bash
tools/deploy/mysql_test/ob_test.sh -n ai_funcs -t ik_custom_dict
```

Expected: PASS with no result-file modification.

- [ ] **Step 5: Run the exact Task4 benchmark workload**

From `tools/benchmark`:

```bash
OUTPUT=./task4_bench_result.txt LABEL=task4-full-port bash fts_large_bench.sh
```

Expected: script reaches the final report and appends it to `tools/benchmark/task4_bench_result.txt`; hit counts equal `query_cn=8001`, `query_beng=11000`, `query_mixed=7332`, `query_limit=20` for the checked-in workload.

- [ ] **Step 6: Score the generated report**

```bash
python3 fts_large_bench_score.py task4_bench_result.txt --baseline fts_large_bench_baseline.json
```

Expected: configuration matches the baseline; `mean_improvement` is greater than `0.00%`; score is greater than `0.00 / 100`.

- [ ] **Step 7: Final requirement and repository audit**

```bash
git diff --check origin/vldb_2026...HEAD
git status --short
rg -n '未移植' docs/superpowers/plans/2026-07-17-task4-upstream-port-audit.md
git log --oneline origin/vldb_2026..HEAD
```

Expected: diff check prints nothing; status contains no task-owned unstaged file; audit search prints nothing; log shows the design plus Task 1–8 commits. User-owned untracked files may remain and must be reported without modification.

- [ ] **Step 8: Commit only evidence-driven fixes, then re-run Steps 2–7**

For each discovered defect, add a failing regression test, observe RED, patch the owning implementation, observe GREEN, and commit with a scoped message. The final verification evidence must come from the run after the last fix.

---

## Plan Self-Review Traceability

| Spec requirement | Implementing task | Primary verification |
| --- | --- | --- |
| 分词热路径、解析器复用、token/停用词重构 | Task 2 | `test_fts_hotpath`, `test_ft_parser`, `test_fts_plugin`, `test_task3` |
| 模块化排序和存储向量排序 | Task 3 | `test_storage_sort_vec_impl`, observer link |
| encoded sort key、position list、五列 schema | Task 4 | `test_fts_encoding`, schema/property regression |
| 分区本地采样、归并、写入 DAG | Task 5 | `test_fts_sample_pipeline`, `test_merge_sort_op`, `test_pipeline_and_op` |
| GI、PX 重分布、并行 DDL 计划、统计采样 | Task 6 | plan assertions, observer link, Task3 mysqltest |
| DAG monitor、TTL、虚拟表 | Task 7 | `test_ddl_dag_monitor`, generated schema search |
| rootserver/RPC、283 文件闭环、Task2/Task3 兼容 | Task 8 | audit zero open rows, integration tests |
| 完整正确性和正向性能提升 | Task 9 | full build, targeted tests, mysqltest, benchmark score |
