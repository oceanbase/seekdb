# Task4 全文索引构建性能优化：需求与设计

## 1. 文档范围与基线

本文档定义 Task4 的完整需求和移植设计。实现基线为 seekdb `vldb_2026` 分支提交 `a0ffb3c3`，功能来源为 OceanBase 上游提交 `81c822ca5cb2d88c3495192d21e6006d6785fbb4`（父提交 `b786266ba3fc07b8437d07c8d1d177580e788cd0`）。上游差异共涉及 283 个文件、23,713 行新增和 3,697 行删除。

本任务不是只针对 benchmark 的局部调优，而是将上游提交中的六大优化体系完整适配到当前分支：

1. 分词器和解析器热路径优化。
2. 模块化排序框架。
3. 分区本地 FTS 构建流水线和多阶段 DAG。
4. encoded sort key 与 position list 编解码。
5. FTS Granule Iterator、并行 DDL 和统计收集计划适配。
6. DDL DAG 监控与虚拟表。

Task2、Task3 已有能力必须保留。当前工作区中的未跟踪文件属于用户，不允许删除或覆盖。

## 2. 目标与非目标

### 2.1 功能目标

- 完整移植上游六类优化及其跨模块依赖，保持全文索引创建、查询和辅助表数据正确。
- 降低逐文档分词产生的分配、析构、字符分类和虚调用开销。
- 让 SQL 层和存储层共享可组合的排序策略、资源管理、chunk 构建和外部归并能力。
- 对适用表启用 tablet 内部的分区本地 FTS 构建，减少跨分区 shuffle。
- 通过 encoded sort key、定长键基数排序、sort-key/payload 分离和向量化 I/O 提高排序吞吐。
- 在辅助表中保存可校验、可演进的词位置列表。
- 提供 FTS 构建范围划分、采样、并行写入及任务进度监控能力。
- 所有新增或修改的关键接口具有中文说明注释；兼容合并及行为变化处具有中文修改说明。

### 2.2 非目标

- 不改变用户可见的全文检索语义、分词结果、MATCH 命中集合或排序结果。
- 不修改 `fts_large_bench.sh` 的工作负载、命中校验和计时口径来制造性能提升。
- 不启用上游仍预留的 delta + zigzag + PFor position-list 编码；当前使用 variable-int64 编码。
- 不进行与六类优化无关的通用重构。

## 3. 当前基线与主要缺口

| 分类 | `vldb_2026` 当前状态 | 完整移植后的状态 |
| --- | --- | --- |
| 分词容器 | IK 热路径依赖逐节点容器，解析器按文档构造 | 使用 `ObFastSegmentArray`/`ObFastList`，所有内置解析器支持复用 |
| token 处理 | `ObFTWord`、`ObAddWord` 和独立停用词逻辑 | `ObFTToken`、`ObFTTokenProcessor`、按 collation 缓存的停用 token checker |
| 排序 | `ObSortVecOpImpl` 承担多数职责 | 策略、资源、chunk、行存储和外归并独立，SQL/存储共用 |
| FTS DDL | 缺少题目所述分区本地多阶段流水线 | 采样、边界持久化、并行归并和 macro-block 写入形成 DAG |
| 编码 | 无 DDL encoded sortkey 工具和位置列表存储 | 保序编码、定长基数排序、带 magic/version/checksum 的位置列表 |
| SQL 计划 | 无 FTS 专用 GI 范围与 slice 计算 | FTS 范围划分、doc-id 重分布和采样统计接入计划 |
| 可观测性 | 无 DDL DAG 任务级虚拟表 | 租户级监控管理器、节点、信息记录和虚拟表 |

## 4. 方案选择与移植原则

采用“以上游提交为功能真值，按依赖顺序移植并做当前分支兼容合并”的方案，不整体盲目 cherry-pick，也不根据任务描述重新发明实现。

移植顺序为：

1. 基础数据结构、token 模型、解析器复用。
2. 排序策略、资源管理、chunk 和存储层向量排序。
3. sort key 与 position list 编码。
4. DDL 排序提供者、采样/写入流水线和 DAG 任务图。
5. SQL 计划、schema、RPC、内部表及表达式接线。
6. DAG 监控管理、任务埋点和虚拟表。
7. Task2/Task3 兼容回归、完整构建与 benchmark。

冲突处理遵循以下优先级：

- 上游 Task4 的数据结构、接口契约和调用链必须完整保留。
- 当前分支 Task2/Task3 的新增功能不得被上游旧版本覆盖。
- 同一接口同时被 Task3 和 Task4 修改时，合并两侧状态与生命周期，不复制两套平行实现。
- 生成文件必须通过项目既有生成链路或与源定义同步更新，不能只修改生成产物。

## 5. 分类功能需求

### 5.1 分词器与解析器热路径

1. `ObFastSegmentArray` 使用 2 的幂次分块，通过位移和掩码定位元素；`reuse()` 只重置逻辑长度，`reset()` 才释放块。
2. `ObFastList` 使用 `ObFastSegmentArray` 作为节点池，保留 IK token 链所需的插入、删除、遍历和排序语义。
3. `TokenizeContext` 在初始化时缓存字符集的 `well_formed_len` 函数，单次取得当前字符与分类，避免同一字符重复解码。
4. IK、ngram、ngram2、beng 和 whitespace 解析器统一实现 `ObIFTParser::reuse_parser()`；复用不得改变 token 文本、字符偏移、字符数和顺序。
5. 元数据对象使用长生命周期 allocator，逐文档字符串、token 和临时容器使用 scratch allocator；复用前必须保证上一文档的输出不再引用 scratch 内存。
6. IK arbitrator 与内部 hashmap 作为持久成员，通过 `reuse()` 清空逻辑状态。
7. `ObFTWord`/`ObFTWordMap` 迁移为 `ObFTToken`/`ObFTTokenMap`，token 缓存 hash 值和比较函数。
8. `ObFTTokenProcessor` 统一完成长度过滤、停用 token 检查、大小写转换、分组、词频和位置列表收集，并支持无须继续处理时提前返回。
9. `ObStopTokenCheckerGen` 延迟创建按 collation 分组的只读 hash 表；初始化完成后的查询路径无锁且使用预计算 hash。

### 5.2 排序框架

1. `ObISortStrategy` 只定义内存数据排序契约；完整排序、分区排序和分区 TopN 分别由独立策略实现。
2. `ObFullSortStrategy` 对可编码的定长 2–18 字节 sort key 使用 `FixedKeySort`，其他 encoded key 使用自适应快速排序，无法编码时回退到比较器排序。
3. `ObSortResourceManager` 负责内存边界、使用量更新、dump 前处理和归并路数计算；SQL 与存储层派生类只承担各自额外资源核算。
4. `ObSortChunkBuilder` 以 Slicer 决定单 slice 或多 slice 输出，支持构建、dump、复用和释放 chunk。
5. `ObExternalMergeSorter` 使用静态二叉堆执行最多 256 路归并，返回 sort key 和可选 addon row。
6. `ObSortRowStoreMgr` 分离 sort key 与 addon 列；归并阶段不提前物化 payload。
7. `ObStorageVecSortImpl` 提供存储层批量写入、内排、外排、chunk 合并和 `ObIVector` 批量输出。
8. `NormalDumpStrategy`、`IMMSDumpStrategy`、`PartitionTopnDumpStrategy` 作为模板函子接入，不为热路径增加虚调用。

### 5.3 FTS 构建流水线

1. 当目标表不需要 doc-id 列且不是广播/复制表时选择分区本地构建，采样、分词、排序和写入均在 tablet 内完成。
2. `ObColumnClusteredDag::generate_partition_local_fixed_tasks()` 创建采样、doc-word 写入、归并准备、word-doc 写入和最终归并的依赖图。
3. `ObFtsForwardInvertSampleOperator` 收集 PX 范围键、转换为列式向量、调用 `ObStorageVecSortImpl` 排序，并以等深方式生成正排和倒排边界。
4. `ObFtsWriteInnerTableOperator` 幂等地持久化边界；恢复时优先读取已有边界。
5. PX 扫描通过 `wait_sample_finish()` 等待，采样成功或失败后由 `notify_sample_finished()` 唤醒，避免永久阻塞。
6. `ObDDLMergeSortTask` 原子领取同一 slice 的 chunk 子集，归并后推回；未达到最终路数时返回 `OB_DAG_TASK_IS_SUSPENDED` 以重新调度。
7. `ObFullTextIndexWritePipeline` 从并发队列消费 chunk，将 `(doc_id, word)` 重排为 `(word, doc_id)`，再交给 macro-block writer 构建 SSTable。
8. `ObDDLSortProvider` 管理线程级及 slice 级句柄，完成的 slice 句柄进入互斥保护的复用队列；最终归并路数按内存预算计算且不小于 2。

### 5.4 Sort key 与位置列表

1. `ObDDLEncodeSortkeyUtils` 将多列 rowkey 编码为单个保持排序关系的二进制串，支持 schema 到编码元信息的转换和逐行编码。
2. `ObFtsSegmentSort` 对 token map 中的 token 编码并排序，保持原 collation 下的顺序语义。
3. FTS 倒排和 doc-word 辅助表支持带 position list 的五列布局，schema、DML 参数、row cache 和读取路径必须同步。
4. `ObFTSPositionListStore` 使用 `0xFACE` magic、版本、长度、校验和、元素数和 payload 的自描述格式。
5. 解码必须校验 magic、版本、长度、元素数和 checksum；损坏数据返回明确错误，不能输出部分位置列表。
6. `T_FUN_SYS_POS_LIST`/`ObExprPosList` 从 token 处理结果生成辅助表位置列。

### 5.5 SQL 执行计划与并行 DDL

1. `ObGranuleFtsUtil` 从执行上下文取得正排范围，并根据 GI task 计算当前 slice 和总 slice 数。
2. Granule Iterator、pump、sub coordinator 和 table scan 传递 FTS 专用范围及 slice 信息。
3. `ObDelUpdLogPlan` 对 FTS doc-word 辅助表构建倒排 sort key，并为 PX coordinator 生成采样排序键。
4. `DocidCompare` 只比较 doc-id datum，用于范围重分布，确保 PX worker 写入的 doc-id 范围互不重叠。
5. `ObStatCollectorOp` 与逻辑统计节点接入 FTS 范围采样，保持普通统计路径不变。

### 5.6 DDL DAG 监控

1. `ObDDLDagMonitorMgr` 作为租户级 MTL 单例，使用有上限的 FIFO allocator、节点 hashmap、TTL 和周期清理任务。
2. `ObDDLDagMonitorNode` 以 `(dag_ptr, trace_id)` 为键，记录 DAG 创建/完成时间并维护任务信息链表；并发访问使用引用计数。
3. `ObDDLDagMonitorInfo` 记录任务类型、创建/完成时间、调度次数、累计执行时间和返回码，允许 FTS 任务派生扩展字段。
4. 独立 DAG 和被监控任务必须在创建、调度、完成、失败及销毁路径正确登记和释放。
5. `ObAllVirtualDDLDagMonitor` 通过 `__all_virtual_ddl_dag_monitor` 输出仍存活及 TTL 内已完成任务的信息。

## 6. 新增与修改接口目录

本节按功能分类列出实施必须创建或修改的关键接口。上游模板类的完整实例化签名以源提交为准；当前分支适配不得另建同职责接口。

### 6.1 分词与 token 接口

| 接口 | 类型 | 位置 | 功能 |
| --- | --- | --- | --- |
| `ObFastSegmentArray<T>::push_back/alloc/reuse/reset` | 新增 | `src/storage/fts/ik/ob_fast_segment_array.h` | 分块分配 token/节点；区分逻辑复用与物理释放 |
| `ObFastList<T>::push_front/push_back/insert/pop_front/pop_back/reuse/reset` | 新增 | `src/storage/fts/ik/ob_fast_list.h` | 在连续节点池上提供 IK 所需双向链表和批量复用语义 |
| `ObIFTParser::reuse_parser(const char *, int64_t)` | 新增抽象接口 | `src/storage/fts/ob_i_ft_parser.h` | 为下一文档重置解析器输入和临时状态 |
| `TokenizeContext::reuse_context(...)` | 新增 | `src/storage/fts/ik/ob_ik_processor.h/.cpp` | 复用字符处理上下文并替换全文输入 |
| `TokenizeContext::current_char_and_type(...)` | 新增 | 同上 | 一次解码并返回字符地址、长度和类型 |
| `TokenizeContext::reset_resource()` | 新增 | 同上 | 清理上下文持有的逐文档资源 |
| `ObIKProcessor::reuse()` | 修改为统一契约 | `src/storage/fts/ik/ob_ik_*_processor.*` | 清空各类 IK processor 的逐文档状态 |
| `ObIKFTParser::reuse_parser(...)` | 新增实现 | `src/storage/fts/ob_ik_ft_parser.*` | 复用 IK context、segmenter 和 arbitrator |
| `ObNgramFTParser::reuse_parser(...)` | 新增实现 | `src/storage/fts/ob_ngram_ft_parser.*` | 复用 ngram 解析器 |
| `ObNgram2FTParser::reuse_parser(...)` | 新增实现 | `src/storage/fts/ob_ngram2_ft_parser.*` | 复用 ngram2 解析器 |
| `ObBEngFTParser::reuse_parser(...)` | 新增实现 | `src/storage/fts/ob_beng_ft_parser.*` | 复用 basic English 解析器 |
| `ObSpaceFTParser::reuse_parser(...)` | 新增实现 | `src/storage/fts/ob_whitespace_ft_parser.*` | 复用 whitespace 解析器 |
| `ObFTTokenProcessor::init/reuse/process_token` | 新增 | `src/storage/fts/ob_ft_token_processor.*` | 完成过滤、大小写、停用词、分组、词频和位置收集 |
| `ObStopTokenChecker::check_is_stop_token(...)` | 新增 | `src/storage/fts/ob_fts_stop_token_check.*` | 以 token 缓存 hash 查询指定 collation 的停用词表 |
| `ObStopTokenCheckerGen::get_stop_token_checker_by_coll(...)` | 新增 | 同上 | 延迟生成并返回进程级只读 checker |
| `ObFTParser::segment/reuse_parser` 调用链 | 修改现有 | `src/storage/fts/ob_fts_plugin_helper.*`、plugin adaptor/interface | 缓存 parser iterator，在文档间优先复用 |

### 6.2 排序接口

| 接口 | 类型 | 位置 | 功能 |
| --- | --- | --- | --- |
| `ObISortStrategy::sort_inmem_data(...)` | 新增抽象接口 | `src/sql/engine/sort/ob_sort_vec_strategy.h` | 定义内存排序策略统一入口 |
| `ObFullSortStrategy::sort_inmem_data/do_fixed_key_sort` | 新增 | 同上 | encoded key 快排、定长基数排序及比较器回退 |
| `ObPartitionSortStrategy::sort_inmem_data(...)` | 新增 | 同上 | 按分区 hash 聚集后在分区内部排序 |
| `ObPartitionTopNSortStrategy::init/add_batch/do_sort/next_stored_row` | 新增 | 同上 | 分区与 TopN 联合优化 |
| `ObSortResourceManager::preprocess_dump/calc_merge_ways` | 新增 | `ob_sort_resource_manager.h` | 内存扩展、dump 判断和归并扇入计算 |
| `ObSQLSortResourceManager::need_dump/get_total_used_size` | 新增 | `ob_sql_sort_resource_manager.h` | 核算 SQL 分区 hash 与 TopN 资源 |
| `ObStorageSortResourceManager::need_dump/get_total_used_size` | 新增 | `ob_storage_sort_resource_manager.h` | 提供存储层轻量资源核算 |
| `ObSortChunkBuilder::get_write_chunk/dump_chunk/reuse` | 新增 | `ob_sort_chunk_builder.h` | 通过单/多 slicer 构建和落盘 chunk |
| `ObExternalMergeSorter::init/get_next_row` | 新增 | `ob_external_merge_sorter.h` | 静态堆驱动的 k 路外部归并 |
| `ObSortRowStoreMgr::init/add_batch` | 新增 | `ob_sort_row_store_mgr.h` | 独立存储 sort key 与 addon row |
| `ObStorageVecSortImpl::init/add_batch/sort/get_next_batch` | 新增 | `ob_storage_sort_vec_impl.h` | 存储层向量排序主入口 |
| `ObStorageVecSortImpl::get_sort_chunks/add_sort_chunks/merge_sort_chunks` | 新增 | 同上 | 暴露 DDL 并行归并需要的 chunk 操作 |
| `NormalDumpStrategy/IMMSDumpStrategy/PartitionTopnDumpStrategy` | 新增函子 | `ob_sort_vec_dump_strategy.h` | 编译期选择 dump 决策 |
| `ObSortVecOpImpl` 初始化和执行接口 | 修改现有 | `ob_sort_vec_op_impl.h/.ipp` | 委托给策略、资源管理和 chunk 组件 |

### 6.3 DDL 流水线、排序编码和位置列表接口

| 接口 | 类型 | 位置 | 功能 |
| --- | --- | --- | --- |
| `ObColumnClusteredDag::generate_partition_local_fixed_tasks(...)` | 新增 | `src/storage/ddl/ob_column_clustered_dag.*` | 生成分区本地 FTS DAG 依赖图 |
| `ObColumnClusteredDag::wait_sample_finish()` | 新增 | 同上 | 阻塞扫描 worker 直至采样结束 |
| `ObColumnClusteredDag::notify_sample_finished()` | 新增 | 同上 | 广播采样完成或失败状态 |
| `ObFtsSamplePipeline::init/get_next_chunk/postprocess` | 新增 | `ob_fts_sample_pipeline.*` | 组织采样排序、边界持久化和完成通知 |
| `ObFtsForwardInvertSampleOperator::execute/build_final_samples` | 新增 | 同上 | 产生 forward/inverted 范围边界 |
| `ObFtsWriteInnerTableOperator::execute/build_ddl_slice_info` | 新增 | 同上 | 幂等写入或恢复内部范围元数据 |
| `ObDDLSortProvider::init/get_sort_impl/finish_sort_impl` | 新增 | `ob_ddl_sort_provider.*` | 管理线程级、slice 级排序句柄及复用池 |
| `ObDDLSortProvider::get_final_merge_ways(...)` | 新增 | 同上 | 依据预算计算最终归并路数 |
| `ObDDLMergeSortTask::process()` | 新增 | `ob_ddl_merge_sort_task.*` | 自挂起地迭代归并同一 slice 的 chunk |
| `ObMergeSortPrepareTask::process()` | 新增 | `ob_merge_sort_prepare_task.*` | 在 word-doc 写入前准备并行归并状态 |
| `ObFullTextIndexWritePipeline::init/get_next_chunk/finish_chunk/postprocess` | 新增 | `ob_full_text_index_write_task.*` | 消费排序 chunk 并串接 sort-flush 与 macro 写入 |
| `ObDAGFtsMacroBlockWriteOp::init/execute/try_execute_finish` | 新增 | `ob_fts_macro_block_write_op.*` | 使用 CG macro writer 生成 FTS SSTable |
| `ObDDLEncodeSortkeyUtils::fill_encode_sortkey_column_item/prepare_encode_param` | 新增 | `ob_ddl_encode_sortkey_utils.*` | 根据 schema 构建 encoded sort key 列及编码参数 |
| `ObDDLEncodeSortkeyUtils::encode_row/encode_batch` | 新增 | 同上 | 逐行或批量生成保序复合 sort key |
| `ObFTSPositionListStore::encode_and_serialize(...)` | 新增 | `src/share/ob_fts_pos_list_codec.*` | 编码位置数组并写入自描述存储格式 |
| `ObFTSPositionListStore::deserialize_and_decode(...)` | 新增 | 同上 | 校验并恢复位置数组 |
| `ObFTSPositionListStore::encode_pos_list/decode_pos_list` | 新增 | 同上 | 提供编码算法级入口 |
| `ObExprPosList` 与 `T_FUN_SYS_POS_LIST` | 新增 | `src/sql/engine/expr/ob_expr_pos_list.*`、item type | 生成 FTS 辅助表位置列表列 |

### 6.4 SQL 计划和监控接口

| 接口 | 类型 | 位置 | 功能 |
| --- | --- | --- | --- |
| `ObGranuleFtsUtil::get_fts_forward_range(...)` | 新增 | `src/sql/engine/px/ob_granule_fts_util.*` | 从执行上下文取得 FTS 正排范围 |
| `ObGranuleFtsUtil::calculate_fts_slice_idx_for_task(...)` | 新增 | 同上 | 为 GI task 计算 slice 编号和总数 |
| `ObDelUpdLogPlan::prepare_inverted_sort_keys(...)` | 新增 | `src/sql/optimizer/ob_del_upd_log_plan.*` | 为 doc-word 辅助表构造倒排排序键 |
| `ObDelUpdLogPlan::gen_px_coord_sampling_sort_keys(...)` | 新增 | 同上 | 为 PX coordinator 构造采样 sort key |
| `DocidCompare::operator()` | 新增 | `src/sql/engine/pdml/static/ob_px_sstable_insert_op.h` | 仅按 doc-id datum 比较范围边界 |
| `ObDDLDagMonitorMgr::mtl_init/init/destroy` | 新增 | `src/storage/ddl/ob_ddl_dag_monitor_mgr.*` | 管理租户级监控生命周期 |
| `ObDDLDagMonitorMgr::register_node/clean_nodes/get_all_nodes` | 新增 | 同上 | 登记、清理并枚举 DAG 监控节点 |
| `ObDDLDagMonitorNode::init/mark_finished/inc_ref/dec_ref` | 新增 | `ob_ddl_dag_monitor_node.*` | 管理 DAG 生命周期和并发引用 |
| `ObDDLDagMonitorNode::alloc_monitor_info/get_all_infos/clean_infos` | 新增 | 同上 | 分配、枚举和清理任务信息链表 |
| `ObDDLDagMonitorInfo::record_execute_stat/mark_finished/convert_to_monitor_entry` | 新增 | 同上 | 累积调度次数、执行时间和返回码并生成虚拟表记录 |
| `ObAllVirtualDDLDagMonitor::inner_get_next_row` | 新增 | `src/observer/virtual_table/ob_all_virtual_ddl_dag_monitor.*` | 将监控节点映射为虚拟表行 |
| 独立 DAG/任务 monitor hook | 修改现有 | `src/share/scheduler/*`、`src/storage/ddl/*` | 在任务创建、调度和完成路径维护监控状态 |

## 7. 数据流与生命周期

```text
原表/PX 扫描
  -> ObColumnClusteredDag 等待采样边界
  -> 可复用 ObIFTParser 分词
  -> ObFTTokenProcessor 聚合 token、词频和 position list
  -> ObDDLEncodeSortkeyUtils 编码 rowkey
  -> ObDDLSortProvider / ObStorageVecSortImpl 内排或外排
  -> ObFtsSamplePipeline 生成并持久化 forward/inverted 范围
  -> ObDDLMergeSortTask 并行归约 chunk
  -> ObFullTextIndexWritePipeline 调整列序并写 macro block
  -> 正排/倒排辅助 SSTable
```

解析器对象和字典元数据属于长生命周期；文档输入、临时 token 及位置数组属于 scratch 生命周期。排序句柄可在线程和 slice 间复用，但只有 `in_use_ == false` 的句柄可进入复用队列。DAG monitor node 在 DAG 完成后保留到 TTL 到期，任务信息在引用计数归零后才可回收。

## 8. 错误处理与并发约束

- 所有接口沿用 `OB_SUCCESS`/`OB_*` 返回码和 `OB_FAIL` 传播方式，不将编码、排序、I/O 或调度失败降级为成功。
- encoded key 不适用属于可预期回退，必须切换到原比较器；编码数据损坏不允许回退后继续读取。
- sample pipeline 无论成功或失败都必须通知等待线程；等待方读取统一完成状态并返回原始错误。
- 边界写入使用可重试、幂等语义；恢复不得重复追加同一批边界。
- chunk 领取和归还使用原子状态；复用队列使用互斥锁；监控 map 使用读写锁；监控信息使用引用计数。
- allocator 达到上限时返回内存错误并触发既有清理/dump 路径，不能越过租户内存预算。

## 9. 中文注释规范

1. 每个新增类、公开接口和关键内部接口，在声明处用中文说明职责、输入、输出、所有权和复用条件。
2. 修改现有接口时，在声明或实现的修改块前说明修改原因、原行为和新行为。
3. 并发队列、原子状态、条件变量、引用计数、allocator 生命周期和 fallback 分支必须写中文安全性说明。
4. 与 Task2/Task3 的冲突合并处必须说明保留了哪一侧能力以及组合方式。
5. 注释解释设计意图和约束，不逐行复述 C++ 语句；已有准确的版权和协议注释不翻译。

## 10. 测试与验收

### 10.1 TDD 单元测试

- `ObFastSegmentArray`：跨块索引、扩容、`reuse()` 地址复用、`reset()` 释放和越界行为。
- `ObFastList`：头尾插入、排序插入、删除、清空和节点池复用。
- 五种解析器：首次解析和 `reuse_parser()` 后解析结果完全一致，包含 UTF-8、多字节字符、空文本和长文本。
- token processor：长度边界、大小写、停用 token、词频和重复 token 的 position list。
- sort strategy：与原比较器排序结果一致；2–18 字节定长键走 fixed-key 路径；不可编码值正确回退。
- position list：空/单个/大量位置 round-trip，错误 magic、版本、长度和 checksum 均拒绝。
- DDL sort provider：线程句柄、slice 句柄、复用队列和归并路数。
- DAG monitor：登记、调度计数、完成、并发读取、TTL 清理和 allocator 上限。

### 10.2 集成与回归测试

- 上游新增的 storage DDL、FTS sample pipeline、merge sort、storage vector sort 和 FTS encoding 测试适配并通过。
- 当前仓库已有 FTS parser/plugin、全文索引创建、查询和 Task3 自定义词典测试通过。
- 编译受影响的 `src/share`、`src/storage`、`src/sql`、`src/observer` 和 `src/rootserver` 目标，随后执行完整可用构建检查。
- 查询命中数必须与 benchmark 基线一致：中文、beng、mixed 和 limit 场景不得改变结果集合。

### 10.3 性能验收

在 `tools/benchmark` 运行：

```bash
bash fts_large_bench.sh
```

使用 `fts_large_bench_score.py` 和 `fts_large_bench_baseline.json` 计算结果。验收要求：脚本完整成功、命中数不变、build/tokenize/query 三类综合改善为正。硬件噪声通过脚本既有 warmup、三次采样、中位数/均值和标准差记录体现，不修改计分公式。

## 11. 完成判定

只有同时满足以下条件才能声明完成：

1. 六类需求均可映射到已移植接口和调用路径。
2. 上游 283 文件差异经过逐项审计：已移植、因当前分支已有等价实现而合并，或有明确且不影响六类目标的排除理由。
3. 所有新增/修改接口和关键修改块符合中文注释规范。
4. 目标单元测试、集成测试和构建命令具有当次运行的成功证据。
5. Task2/Task3 相关测试无回归。
6. benchmark 正确完成、命中数不变且综合性能改善为正。
