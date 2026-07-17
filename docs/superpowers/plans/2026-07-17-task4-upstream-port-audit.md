# Task4 上游移植审计表

## 审计基线

- seekdb 实现分支：`task4-fulltext-build-performance`
- seekdb 基线提交：`2e4f0d66f3a5519e2a6b4f51f94d4b000529f375`
- 上游父提交：`b786266ba3fc07b8437d07c8d1d177580e788cd0`
- 上游目标提交：`81c822ca5cb2d88c3495192d21e6006d6785fbb4`
- 审计命令：`git diff --name-status b786266ba3fc07b8437d07c8d1d177580e788cd0 81c822ca5cb2d88c3495192d21e6006d6785fbb4`
- 差异证据：`283 files changed, 23713 insertions(+), 3697 deletions(-)`
- 分类口径：以 seekdb 基线提交中的跟踪路径为准；`build.sh debug --init` 产生的未跟踪 inner-table/virtual-table 生成文件不改变分类，也不纳入本任务提交。

| 上游状态 | 数量 |
| --- | ---: |
| 修改且当前路径分叉 | 194 |
| 修改但当前路径不存在 | 28 |
| 新增且无同名冲突 | 59 |
| 删除且当前路径存在 | 2 |
| 合计 | 283 |

## 基线构建与测试证据

控制器已在生产代码修改前执行 `bash build.sh debug --init`，并以 `cmake --build build_debug --target test_ft_parser test_fts_plugin test_fts_property test_task3 -j4` 成功构建四个目标。本审计创建前又直接运行四个测试二进制，结果如下：

| 二进制 | 结果 |
| --- | --- |
| `build_debug/unittest/storage/fts/test_ft_parser` | exit 0；7 tests passed；既有提示：1 disabled test |
| `build_debug/unittest/storage/test_fts_plugin` | exit 0；8 tests passed |
| `build_debug/unittest/storage/test_fts_property` | exit 0；4 tests passed |
| `build_debug/unittest/storage/test_task3` | exit 0；23 tests passed |
| 合计 | 42 tests passed；0 failed；另有 1 个既有 disabled test |

## 单机 Release 性能证据（2026-07-18）

所有记录均在同一机器、默认配置、未修改的 `bash tools/benchmark/fts_large_bench.sh` 下取得；临时 seekdb 实例在每次运行后均已停止。

| 二进制来源 | build / tokenize / query 平均改善（历史 CI 基线） | 综合改善 | 命中数（cn/beng/mixed/limit） | 结论 |
| --- | --- | ---: | --- | --- |
| 原始源码 `6299b0ee4` 的 Release 二进制 | 21.63% / 57.99% / 81.84% | 53.82% | 8001 / 11000 / 7332 / 20 | 原版对照完整成功 |
| 当前无 map 投影候选的 Release 二进制 | 19.96% / 58.89% / 81.88% | 53.58% | 8001 / 11000 / 7332 / 20 | 相比原版 -0.24 个百分点，低于约 2 分波动带；不作为新增收益保留 |

- `ObFTTokenMap -> ObFTWordMap` 投影消除曾作为本地构建流水线候选实测，建索引阶段显著回退，已用补丁撤回。
- benchmark 正常建索引路径的 `need_position_list=false`，所以 position-list 编码不影响该 workload；不为目录覆盖率接入未被使用的编码。
- 单机范围内排除 PX/GI、跨分区 shuffle、分布式 DDL DAG、虚拟表/TTL 监控和通用 SQL 分区/TopN 排序；它们不在本 benchmark 的单机调用链。
- 后续候选项必须在同机同 Release、正确命中数下相对紧邻前态提升超过约 2 个评分点，否则撤回并在本节追加证据。

| 六类单机范围 | 当前判定 | 代码/运行时证据 |
| --- | --- | --- |
| 5.1 分词器与解析器热路径 | 保留既有 Task4/Task2 兼容实现 | 五类内置解析器复用、token/停止词路径已在 `src/storage/fts`；原版与当前均通过完整 benchmark 且命中数一致。该路径的单次综合差异仅 -0.24 分，不能把它宣称为本轮新增性能收益，但不得破坏既有 Task2/Task3 能力。 |
| 5.2 本地排序 | 功能排除 | 当前 FTS DML 在 `ObDASDomainUtils::generate_fulltext_word_rows` 生成局部行；没有 FTS 专用 sort 调用点。上游实现要求通用 SQL/storage sort、chunk 与外归并框架，超出单机最小调用链；不能证明 >2 分收益。 |
| 5.3 本地构建流水线 | 候选撤回 | 实测过 token-map 到 legacy word-map 的直接消费候选，build 明显回退，已撤回。上游 sample/merge/write pipeline 依赖 seekdb 未接入的 DDL pipeline 和跨 slice 调度。 |
| 5.4 sort key 与位置列表 | 功能排除 | benchmark 路径传入 `need_position_list=false`；`ObFTSPositionListStore`/五列辅助表改变当前兼容布局且没有运行时覆盖，不能产生可测收益。 |
| 5.5 本地计划/范围接线 | 功能排除 | 上游可见实现以 `ObGranuleFtsUtil`、GI 和 PX 范围分发为中心；单机 benchmark 不进入这些分布式路径，且本任务明确排除。 |
| 5.6 轻量阶段统计 | 功能排除 | 新计数/计时只能增加观测开销，不能缩短固定 workload；不引入 DAG monitor、虚拟表、TTL 或无收益计数。 |

## 路径与状态约定

- 责任任务严格按实施计划的 Task 2–7 功能边界归属；rootserver/RPC 等最终接线按其依赖归入 Task 6。
- 已知目录映射采用实施计划定义的 seekdb 位置：mysql proxy 映射到 `deps/oblib/src/common/mysqlclient`，scheduler 映射到 `src/observer/scheduler`，order-preserving encoder 映射到 `src/storage`，FTS builder util 映射到 `src/sql/resolver/ddl`。
- `mittest/simple_server` 测试适配到 `unittest/storage/ddl`；inner-table 生成物以 `ob_inner_table_schema_def.py` 为受控源；Table API FTS 行为映射到 SQL/DDL 公共实现。
- 本表是移植前的审计脚手架，所以 283 个条目的当前状态统一为 `未移植`。后续任务只能将其更新为 `已移植`、`路径映射` 或有证据的 `功能排除`。

## 逐文件审计

| # | 上游状态 | 基线分类 | 上游路径 | 责任任务 | seekdb 目标路径 | 当前状态 | 验证证据 |
| ---: | :---: | --- | --- | :---: | --- | :---: | --- |
| 001 | M | 修改但当前路径不存在 | `deps/oblib/src/lib/mysqlclient/ob_mysql_proxy.h` | Task 6 | `deps/oblib/src/common/mysqlclient/ob_mysql_proxy.h` | 未移植 | HEAD 无同名跟踪路径；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 002 | M | 修改且当前路径分叉 | `deps/oblib/src/lib/stat/ob_latch_define.h` | Task 7 | `deps/oblib/src/lib/stat/ob_latch_define.h` | 未移植 | HEAD 同名路径存在；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 003 | M | 修改但当前路径不存在 | `mittest/simple_server/CMakeLists.txt` | Task 5 | `unittest/storage/ddl/CMakeLists.txt` | 未移植 | HEAD 无同名跟踪路径；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 004 | A | 新增且无同名冲突 | `mittest/simple_server/ddl_pipeline_simple_helper.h` | Task 5 | `unittest/storage/ddl/test_ddl_pipeline_base.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 005 | A | 新增且无同名冲突 | `mittest/simple_server/sort/CMakeLists.txt` | Task 3 | `unittest/storage/ddl/CMakeLists.txt` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 006 | A | 新增且无同名冲突 | `mittest/simple_server/sort/test_ob_storage_sort_vec_impl.cpp` | Task 3 | `unittest/storage/ddl/test_storage_sort_vec_impl.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 007 | A | 新增且无同名冲突 | `mittest/simple_server/test_ddl_pipeline_base.h` | Task 5 | `unittest/storage/ddl/test_ddl_pipeline_base.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 008 | A | 新增且无同名冲突 | `mittest/simple_server/test_fts_sample_pipeline.cpp` | Task 5 | `unittest/storage/ddl/test_fts_sample_pipeline.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 009 | A | 新增且无同名冲突 | `mittest/simple_server/test_fulltext_index_create.cpp` | Task 5 | `unittest/storage/ddl/test_fts_sample_pipeline.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 010 | A | 新增且无同名冲突 | `mittest/simple_server/test_merge_sort_op.cpp` | Task 5 | `unittest/storage/ddl/test_merge_sort_op.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 011 | M | 修改但当前路径不存在 | `src/objit/CMakeLists.txt` | Task 4 | `src/objit/include/objit/common/ob_item_type.h` | 未移植 | HEAD 无同名跟踪路径；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 012 | M | 修改且当前路径分叉 | `src/observer/CMakeLists.txt` | Task 7 | `src/observer/CMakeLists.txt` | 未移植 | HEAD 同名路径存在；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 013 | M | 修改且当前路径分叉 | `src/observer/omt/ob_multi_tenant.cpp` | Task 7 | `src/observer/omt/ob_multi_tenant.cpp` | 未移植 | HEAD 同名路径存在；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 014 | M | 修改但当前路径不存在 | `src/observer/table/fts/ob_table_fts_cg_service.cpp` | Task 5 | `src/storage/ddl/ob_full_text_index_write_task.cpp` | 未移植 | HEAD 无同名跟踪路径；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 015 | M | 修改但当前路径不存在 | `src/observer/table/ob_table_schema_cache.h` | Task 4 | `src/share/schema/ob_schema_utils.h` | 未移植 | HEAD 无同名跟踪路径；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 016 | M | 修改且当前路径分叉 | `src/observer/table_load/dag/ob_table_load_dag_insert_sstable_task.cpp` | Task 5 | `src/observer/table_load/dag/ob_table_load_dag_insert_sstable_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 017 | M | 修改且当前路径分叉 | `src/observer/table_load/dag/ob_table_load_empty_insert_dag.cpp` | Task 5 | `src/observer/table_load/dag/ob_table_load_empty_insert_dag.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 018 | A | 新增且无同名冲突 | `src/observer/virtual_table/ob_all_virtual_ddl_dag_monitor.cpp` | Task 7 | `src/observer/virtual_table/ob_all_virtual_ddl_dag_monitor.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 019 | A | 新增且无同名冲突 | `src/observer/virtual_table/ob_all_virtual_ddl_dag_monitor.h` | Task 7 | `src/observer/virtual_table/ob_all_virtual_ddl_dag_monitor.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 020 | M | 修改但当前路径不存在 | `src/observer/virtual_table/ob_all_virtual_logservice_cluster_info.cpp` | Task 7 | 无对应目标（seekdb 未引入 `ObAllVirtualLogServiceClusterInfo`） | 未移植 | 上游唯一 hunk 仅新增 `src/logservice/ipalf/ipalf_env.h`（compile-only include）；seekdb 基线无 `ObAllVirtualLogServiceClusterInfo` 或同职责虚拟表，且不承载六大优化的运行时行为；Task 8 将按 incidental-hunk 规则以逐项 hunk 证据标记为 `功能排除` |
| 021 | M | 修改且当前路径分叉 | `src/observer/virtual_table/ob_virtual_table_iterator_factory.cpp` | Task 7 | `src/observer/virtual_table/ob_virtual_table_iterator_factory.cpp` | 未移植 | HEAD 同名路径存在；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 022 | M | 修改且当前路径分叉 | `src/plugin/adaptor/ob_plugin_ftparser_adaptor.cpp` | Task 2 | `src/plugin/adaptor/ob_plugin_ftparser_adaptor.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 023 | M | 修改且当前路径分叉 | `src/plugin/adaptor/ob_plugin_ftparser_adaptor.h` | Task 2 | `src/plugin/adaptor/ob_plugin_ftparser_adaptor.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 024 | M | 修改且当前路径分叉 | `src/plugin/interface/ob_plugin_ftparser_intf.h` | Task 2 | `src/plugin/interface/ob_plugin_ftparser_intf.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 025 | M | 修改且当前路径分叉 | `src/plugin/sys/ob_plugin_helper.cpp` | Task 2 | `src/plugin/sys/ob_plugin_helper.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 026 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_build_mview_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_build_mview_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 027 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_constraint_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_constraint_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 028 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_ddl_redefinition_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_ddl_redefinition_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 029 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_ddl_retry_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_ddl_retry_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 030 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_ddl_scheduler.cpp` | Task 6 | `src/rootserver/ddl_task/ob_ddl_scheduler.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 031 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_ddl_scheduler.h` | Task 6 | `src/rootserver/ddl_task/ob_ddl_scheduler.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 032 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_ddl_tablet_scheduler.cpp` | Task 6 | `src/rootserver/ddl_task/ob_ddl_tablet_scheduler.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 033 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_ddl_tablet_scheduler.h` | Task 6 | `src/rootserver/ddl_task/ob_ddl_tablet_scheduler.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 034 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_ddl_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_ddl_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 035 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_ddl_task.h` | Task 6 | `src/rootserver/ddl_task/ob_ddl_task.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 036 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_fts_index_build_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_fts_index_build_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 037 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_fts_index_build_task.h` | Task 6 | `src/rootserver/ddl_task/ob_fts_index_build_task.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 038 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_index_build_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_index_build_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 039 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_index_build_task.h` | Task 6 | `src/rootserver/ddl_task/ob_index_build_task.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 040 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_modify_autoinc_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_modify_autoinc_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 041 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_partition_split_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_partition_split_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 042 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_vec_index_build_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_vec_index_build_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 043 | M | 修改且当前路径分叉 | `src/rootserver/ddl_task/ob_vec_ivf_index_build_task.cpp` | Task 6 | `src/rootserver/ddl_task/ob_vec_ivf_index_build_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 044 | M | 修改且当前路径分叉 | `src/rootserver/ob_ddl_service.cpp` | Task 6 | `src/rootserver/ob_ddl_service.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 045 | M | 修改且当前路径分叉 | `src/rootserver/ob_index_builder.cpp` | Task 6 | `src/rootserver/ob_index_builder.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 046 | M | 修改且当前路径分叉 | `src/rootserver/ob_index_builder.h` | Task 6 | `src/rootserver/ob_index_builder.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 047 | M | 修改且当前路径分叉 | `src/rootserver/parallel_ddl/ob_create_index_helper.cpp` | Task 6 | `src/rootserver/parallel_ddl/ob_create_index_helper.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 048 | M | 修改且当前路径分叉 | `src/share/CMakeLists.txt` | Task 4 | `src/share/CMakeLists.txt` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 049 | M | 修改但当前路径不存在 | `src/share/inner_table/ob_inner_table_schema.12551_12600.cpp` | Task 7 | `src/share/inner_table/ob_inner_table_schema_def.py` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 050 | M | 修改但当前路径不存在 | `src/share/inner_table/ob_inner_table_schema.h` | Task 7 | `src/share/inner_table/ob_inner_table_schema_def.py` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 051 | M | 修改但当前路径不存在 | `src/share/inner_table/ob_inner_table_schema_constants.h` | Task 7 | `src/share/inner_table/ob_inner_table_schema_def.py` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 052 | M | 修改且当前路径分叉 | `src/share/inner_table/ob_inner_table_schema_def.py` | Task 7 | `src/share/inner_table/ob_inner_table_schema_def.py` | 未移植 | HEAD 同名路径存在；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 053 | M | 修改但当前路径不存在 | `src/share/inner_table/ob_inner_table_schema_misc.ipp` | Task 7 | `src/share/inner_table/ob_inner_table_schema_def.py` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 054 | M | 修改且当前路径分叉 | `src/share/inner_table/ob_load_inner_table_schema.cpp` | Task 7 | `src/share/inner_table/ob_load_inner_table_schema.cpp` | 未移植 | HEAD 同名路径存在；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 055 | M | 修改但当前路径不存在 | `src/share/inner_table/table_id_to_name` | Task 7 | `src/share/inner_table/ob_inner_table_schema_def.py` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 056 | M | 修改且当前路径分叉 | `src/share/ob_ddl_common.cpp` | Task 6 | `src/share/ob_ddl_common.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 057 | M | 修改且当前路径分叉 | `src/share/ob_ddl_common.h` | Task 6 | `src/share/ob_ddl_common.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 058 | M | 修改但当前路径不存在 | `src/share/ob_fts_index_builder_util.cpp` | Task 6 | `src/sql/resolver/ddl/ob_fts_index_builder_util.cpp` | 未移植 | HEAD 无同名跟踪路径；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 059 | M | 修改但当前路径不存在 | `src/share/ob_fts_index_builder_util.h` | Task 6 | `src/sql/resolver/ddl/ob_fts_index_builder_util.h` | 未移植 | HEAD 无同名跟踪路径；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 060 | A | 新增且无同名冲突 | `src/share/ob_fts_pos_list_codec.cpp` | Task 4 | `src/share/ob_fts_pos_list_codec.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 061 | A | 新增且无同名冲突 | `src/share/ob_fts_pos_list_codec.h` | Task 4 | `src/share/ob_fts_pos_list_codec.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 062 | M | 修改但当前路径不存在 | `src/share/ob_order_perserving_encoder.cpp` | Task 4 | `src/storage/ob_order_perserving_encoder.cpp` | 未移植 | HEAD 无同名跟踪路径；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 063 | M | 修改但当前路径不存在 | `src/share/ob_order_perserving_encoder.h` | Task 4 | `src/storage/ob_order_perserving_encoder.h` | 未移植 | HEAD 无同名跟踪路径；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 064 | M | 修改且当前路径分叉 | `src/share/ob_rpc_struct.cpp` | Task 6 | `src/share/ob_rpc_struct.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 065 | M | 修改且当前路径分叉 | `src/share/ob_rpc_struct.h` | Task 6 | `src/share/ob_rpc_struct.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 066 | M | 修改且当前路径分叉 | `src/share/rc/ob_tenant_base.h` | Task 7 | `src/share/rc/ob_tenant_base.h` | 未移植 | HEAD 同名路径存在；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 067 | M | 修改但当前路径不存在 | `src/share/scheduler/ob_independent_dag.cpp` | Task 7 | `src/observer/scheduler/ob_independent_dag.cpp` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 068 | M | 修改但当前路径不存在 | `src/share/scheduler/ob_independent_dag.h` | Task 7 | `src/observer/scheduler/ob_independent_dag.h` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 069 | M | 修改但当前路径不存在 | `src/share/scheduler/ob_tenant_dag_scheduler.cpp` | Task 7 | `src/observer/scheduler/ob_tenant_dag_scheduler.cpp` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 070 | M | 修改但当前路径不存在 | `src/share/scheduler/ob_tenant_dag_scheduler.h` | Task 7 | `src/observer/scheduler/ob_tenant_dag_scheduler.h` | 未移植 | HEAD 无同名跟踪路径；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 071 | M | 修改且当前路径分叉 | `src/share/schema/ob_column_schema.h` | Task 4 | `src/share/schema/ob_column_schema.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 072 | M | 修改但当前路径不存在 | `src/share/schema/ob_schema_printer.cpp` | Task 4 | `src/sql/printer/ob_schema_printer.cpp` | 未移植 | HEAD 上游路径不存在；目标路径存在同职责 `ObSchemaPrinter` 及 FTS `print_fts_parser_info` 打印调用点；待补 FTS index params 打印并由 `test_fts_encoding`、`test_fts_property`、`observer` 链接验证 |
| 073 | M | 修改且当前路径分叉 | `src/share/schema/ob_schema_struct.cpp` | Task 4 | `src/share/schema/ob_schema_struct.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 074 | M | 修改且当前路径分叉 | `src/share/schema/ob_schema_struct.h` | Task 4 | `src/share/schema/ob_schema_struct.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 075 | A | 新增且无同名冲突 | `src/share/schema/ob_schema_struct_fts.cpp` | Task 4 | `src/share/schema/ob_schema_struct_fts.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 076 | M | 修改但当前路径不存在 | `src/share/schema/ob_schema_struct_fts.h` | Task 4 | `src/share/schema/ob_schema_struct_fts.h` | 未移植 | HEAD 无同名跟踪路径；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 077 | M | 修改且当前路径分叉 | `src/share/schema/ob_schema_utils.cpp` | Task 4 | `src/share/schema/ob_schema_utils.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 078 | M | 修改且当前路径分叉 | `src/share/schema/ob_schema_utils.h` | Task 4 | `src/share/schema/ob_schema_utils.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 079 | M | 修改但当前路径不存在 | `src/share/schema/ob_table_dml_param.cpp` | Task 4 | `src/storage/ob_table_dml_param.cpp` | 未移植 | HEAD 上游路径不存在；目标路径存在 `ObTableSchemaParam::convert` 及 FTS parser name/property 转换接口；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接验证 |
| 080 | M | 修改但当前路径不存在 | `src/share/schema/ob_table_dml_param.h` | Task 4 | `src/storage/ob_table_dml_param.h` | 未移植 | HEAD 上游路径不存在；目标路径声明 `ObTableSchemaParam::convert` 与 FTS parser name/property 访问接口；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接验证 |
| 081 | M | 修改但当前路径不存在 | `src/share/schema/ob_table_param.cpp` | Task 4 | `src/storage/access/ob_table_param.cpp` | 未移植 | HEAD 上游路径不存在；目标路径实现 `ObTableParam::convert_fulltext_index_info`；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接验证 |
| 082 | M | 修改但当前路径不存在 | `src/share/schema/ob_table_param.h` | Task 4 | `src/storage/access/ob_table_param.h` | 未移植 | HEAD 上游路径不存在；目标路径声明 `ObTableParam::convert_fulltext_index_info`；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接验证 |
| 083 | M | 修改且当前路径分叉 | `src/share/schema/ob_table_schema.h` | Task 4 | `src/share/schema/ob_table_schema.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 084 | A | 新增且无同名冲突 | `src/share/schema/ob_table_schema_fts_index.cpp` | Task 4 | `src/share/schema/ob_table_schema_fts_index.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 085 | M | 修改且当前路径分叉 | `src/share/schema/ob_table_sql_service.cpp` | Task 4 | `src/share/schema/ob_table_sql_service.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 086 | M | 修改且当前路径分叉 | `src/share/text_analysis/ob_text_analyzer.cpp` | Task 2 | `src/share/text_analysis/ob_text_analyzer.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 087 | M | 修改且当前路径分叉 | `src/share/text_analysis/ob_text_analyzer.h` | Task 2 | `src/share/text_analysis/ob_text_analyzer.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 088 | M | 修改但当前路径不存在 | `src/share/vector_index/ob_vector_index_util.cpp` | Task 6 | `src/observer/vector_index/ob_vector_index_util.cpp` | 未移植 | HEAD 上游路径不存在；目标路径存在 `ObVectorIndexUtil::generate_index_schema_from_exist_table`，其中 `set_index_params` 待补错误传播调用；待 `test_pipeline_and_op`、`observer` 链接验证 |
| 089 | M | 修改且当前路径分叉 | `src/sql/CMakeLists.txt` | Task 4 | `src/sql/CMakeLists.txt` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 090 | M | 修改且当前路径分叉 | `src/sql/code_generator/ob_static_engine_cg.cpp` | Task 4 | `src/sql/code_generator/ob_static_engine_cg.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 091 | M | 修改且当前路径分叉 | `src/sql/das/iter/ob_das_text_retrieval_merge_iter.cpp` | Task 4 | `src/sql/das/iter/ob_das_text_retrieval_merge_iter.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 092 | M | 修改且当前路径分叉 | `src/sql/das/iter/sparse_retrieval/ob_das_tr_merge_iter.cpp` | Task 4 | `src/sql/das/iter/sparse_retrieval/ob_das_tr_merge_iter.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 093 | M | 修改且当前路径分叉 | `src/sql/das/ob_das_domain_utils.cpp` | Task 4 | `src/sql/das/ob_das_domain_utils.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 094 | M | 修改且当前路径分叉 | `src/sql/das/ob_das_domain_utils.h` | Task 4 | `src/sql/das/ob_das_domain_utils.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 095 | M | 修改且当前路径分叉 | `src/sql/engine/basic/ob_compact_row.cpp` | Task 3 | `src/sql/engine/basic/ob_compact_row.cpp` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 096 | M | 修改且当前路径分叉 | `src/sql/engine/basic/ob_compact_row.h` | Task 3 | `src/sql/engine/basic/ob_compact_row.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 097 | M | 修改且当前路径分叉 | `src/sql/engine/basic/ob_stat_collector_op.cpp` | Task 6 | `src/sql/engine/basic/ob_stat_collector_op.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 098 | M | 修改且当前路径分叉 | `src/sql/engine/basic/ob_stat_collector_op.h` | Task 6 | `src/sql/engine/basic/ob_stat_collector_op.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 099 | M | 修改且当前路径分叉 | `src/sql/engine/expr/ob_expr_eval_functions.cpp` | Task 4 | `src/sql/engine/expr/ob_expr_eval_functions.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 100 | M | 修改且当前路径分叉 | `src/sql/engine/expr/ob_expr_operator_factory.cpp` | Task 4 | `src/sql/engine/expr/ob_expr_operator_factory.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 101 | A | 新增且无同名冲突 | `src/sql/engine/expr/ob_expr_pos_list.cpp` | Task 4 | `src/sql/engine/expr/ob_expr_pos_list.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 102 | A | 新增且无同名冲突 | `src/sql/engine/expr/ob_expr_pos_list.h` | Task 4 | `src/sql/engine/expr/ob_expr_pos_list.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 103 | M | 修改且当前路径分叉 | `src/sql/engine/expr/ob_expr_tokenize.cpp` | Task 2 | `src/sql/engine/expr/ob_expr_tokenize.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 104 | M | 修改且当前路径分叉 | `src/sql/engine/expr/ob_expr_tokenize.h` | Task 2 | `src/sql/engine/expr/ob_expr_tokenize.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 105 | M | 修改且当前路径分叉 | `src/sql/engine/ob_sql_mem_mgr_processor.h` | Task 3 | `src/sql/engine/ob_sql_mem_mgr_processor.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 106 | M | 修改且当前路径分叉 | `src/sql/engine/ob_tenant_sql_memory_manager.h` | Task 3 | `src/sql/engine/ob_tenant_sql_memory_manager.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 107 | M | 修改且当前路径分叉 | `src/sql/engine/pdml/static/ob_px_sstable_insert_op.cpp` | Task 6 | `src/sql/engine/pdml/static/ob_px_sstable_insert_op.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 108 | M | 修改且当前路径分叉 | `src/sql/engine/pdml/static/ob_px_sstable_insert_op.h` | Task 6 | `src/sql/engine/pdml/static/ob_px_sstable_insert_op.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 109 | M | 修改且当前路径分叉 | `src/sql/engine/px/datahub/components/ob_dh_sample.cpp` | Task 6 | `src/sql/engine/px/datahub/components/ob_dh_sample.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 110 | A | 新增且无同名冲突 | `src/sql/engine/px/ob_granule_fts_util.cpp` | Task 6 | `src/sql/engine/px/ob_granule_fts_util.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 111 | A | 新增且无同名冲突 | `src/sql/engine/px/ob_granule_fts_util.h` | Task 6 | `src/sql/engine/px/ob_granule_fts_util.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 112 | M | 修改且当前路径分叉 | `src/sql/engine/px/ob_granule_iterator_op.cpp` | Task 6 | `src/sql/engine/px/ob_granule_iterator_op.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 113 | M | 修改且当前路径分叉 | `src/sql/engine/px/ob_granule_iterator_op.h` | Task 6 | `src/sql/engine/px/ob_granule_iterator_op.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 114 | M | 修改且当前路径分叉 | `src/sql/engine/px/ob_granule_pump.cpp` | Task 6 | `src/sql/engine/px/ob_granule_pump.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 115 | M | 修改且当前路径分叉 | `src/sql/engine/px/ob_granule_pump.h` | Task 6 | `src/sql/engine/px/ob_granule_pump.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 116 | M | 修改且当前路径分叉 | `src/sql/engine/px/ob_granule_util.cpp` | Task 6 | `src/sql/engine/px/ob_granule_util.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 117 | M | 修改且当前路径分叉 | `src/sql/engine/px/ob_granule_util.h` | Task 6 | `src/sql/engine/px/ob_granule_util.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 118 | M | 修改且当前路径分叉 | `src/sql/engine/px/ob_px_sub_coord.cpp` | Task 6 | `src/sql/engine/px/ob_px_sub_coord.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 119 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_external_merge_sorter.h` | Task 3 | `src/sql/engine/sort/ob_external_merge_sorter.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 120 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_i_sort_vec_op_impl.h` | Task 3 | `src/sql/engine/sort/ob_i_sort_vec_op_impl.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 121 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_prefix_sort_vec_op_impl.h` | Task 3 | `src/sql/engine/sort/ob_prefix_sort_vec_op_impl.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 122 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_prefix_sort_vec_op_impl.ipp` | Task 3 | `src/sql/engine/sort/ob_prefix_sort_vec_op_impl.ipp` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 123 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_sort_chunk_builder.h` | Task 3 | `src/sql/engine/sort/ob_sort_chunk_builder.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 124 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_sort_compare_vec_op.h` | Task 3 | `src/sql/engine/sort/ob_sort_compare_vec_op.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 125 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_sort_compare_vec_op.ipp` | Task 3 | `src/sql/engine/sort/ob_sort_compare_vec_op.ipp` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 126 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_sort_resource_manager.h` | Task 3 | `src/sql/engine/sort/ob_sort_resource_manager.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 127 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_sort_row_store_mgr.h` | Task 3 | `src/sql/engine/sort/ob_sort_row_store_mgr.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 128 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_sort_vec_dump_strategy.h` | Task 3 | `src/sql/engine/sort/ob_sort_vec_dump_strategy.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 129 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_sort_vec_op_chunk.h` | Task 3 | `src/sql/engine/sort/ob_sort_vec_op_chunk.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 130 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_sort_vec_op_impl.h` | Task 3 | `src/sql/engine/sort/ob_sort_vec_op_impl.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 131 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_sort_vec_op_impl.ipp` | Task 3 | `src/sql/engine/sort/ob_sort_vec_op_impl.ipp` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 132 | M | 修改且当前路径分叉 | `src/sql/engine/sort/ob_sort_vec_op_store_row_factory.h` | Task 3 | `src/sql/engine/sort/ob_sort_vec_op_store_row_factory.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 133 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_sort_vec_strategy.h` | Task 3 | `src/sql/engine/sort/ob_sort_vec_strategy.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 134 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_sql_sort_resource_manager.h` | Task 3 | `src/sql/engine/sort/ob_sql_sort_resource_manager.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 135 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_storage_sort_resource_manager.h` | Task 3 | `src/sql/engine/sort/ob_storage_sort_resource_manager.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 136 | A | 新增且无同名冲突 | `src/sql/engine/sort/ob_storage_sort_vec_impl.h` | Task 3 | `src/sql/engine/sort/ob_storage_sort_vec_impl.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 137 | M | 修改且当前路径分叉 | `src/sql/engine/table/ob_ddl_block_sample_scan_op.cpp` | Task 6 | `src/sql/engine/table/ob_ddl_block_sample_scan_op.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 138 | M | 修改且当前路径分叉 | `src/sql/engine/table/ob_ddl_block_sample_scan_op.h` | Task 6 | `src/sql/engine/table/ob_ddl_block_sample_scan_op.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 139 | M | 修改且当前路径分叉 | `src/sql/engine/table/ob_table_scan_op.cpp` | Task 6 | `src/sql/engine/table/ob_table_scan_op.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 140 | M | 修改且当前路径分叉 | `src/sql/engine/table/ob_table_scan_op.h` | Task 6 | `src/sql/engine/table/ob_table_scan_op.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 141 | M | 修改且当前路径分叉 | `src/sql/executor/ob_task_info.cpp` | Task 6 | `src/sql/executor/ob_task_info.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 142 | M | 修改且当前路径分叉 | `src/sql/executor/ob_task_info.h` | Task 6 | `src/sql/executor/ob_task_info.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 143 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_del_upd_log_plan.cpp` | Task 6 | `src/sql/optimizer/ob_del_upd_log_plan.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 144 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_del_upd_log_plan.h` | Task 6 | `src/sql/optimizer/ob_del_upd_log_plan.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 145 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_join_order.cpp` | Task 6 | `src/sql/optimizer/ob_join_order.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 146 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_log_del_upd.cpp` | Task 6 | `src/sql/optimizer/ob_log_del_upd.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 147 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_log_granule_iterator.cpp` | Task 6 | `src/sql/optimizer/ob_log_granule_iterator.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 148 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_log_granule_iterator.h` | Task 6 | `src/sql/optimizer/ob_log_granule_iterator.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 149 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_log_stat_collector.cpp` | Task 6 | `src/sql/optimizer/ob_log_stat_collector.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 150 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_log_stat_collector.h` | Task 6 | `src/sql/optimizer/ob_log_stat_collector.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 151 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_log_subplan_scan.cpp` | Task 6 | `src/sql/optimizer/ob_log_subplan_scan.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 152 | M | 修改且当前路径分叉 | `src/sql/optimizer/ob_log_table_scan.cpp` | Task 6 | `src/sql/optimizer/ob_log_table_scan.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 153 | M | 修改且当前路径分叉 | `src/sql/parser/non_reserved_keywords_mysql_mode.c` | Task 6 | `src/sql/parser/non_reserved_keywords_mysql_mode.c` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 154 | M | 修改且当前路径分叉 | `src/sql/parser/sql_parser_mysql_mode.y` | Task 6 | `src/sql/parser/sql_parser_mysql_mode.y` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 155 | M | 修改且当前路径分叉 | `src/sql/resolver/ddl/ob_alter_table_resolver.cpp` | Task 6 | `src/sql/resolver/ddl/ob_alter_table_resolver.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 156 | M | 修改且当前路径分叉 | `src/sql/resolver/ddl/ob_create_index_resolver.cpp` | Task 6 | `src/sql/resolver/ddl/ob_create_index_resolver.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 157 | M | 修改且当前路径分叉 | `src/sql/resolver/ddl/ob_create_table_resolver.cpp` | Task 6 | `src/sql/resolver/ddl/ob_create_table_resolver.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 158 | M | 修改且当前路径分叉 | `src/sql/resolver/ddl/ob_ddl_resolver.cpp` | Task 6 | `src/sql/resolver/ddl/ob_ddl_resolver.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 159 | M | 修改且当前路径分叉 | `src/sql/resolver/ddl/ob_ddl_resolver.h` | Task 6 | `src/sql/resolver/ddl/ob_ddl_resolver.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 160 | M | 修改且当前路径分叉 | `src/sql/resolver/dml/ob_dml_resolver.cpp` | Task 6 | `src/sql/resolver/dml/ob_dml_resolver.cpp` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 161 | M | 修改且当前路径分叉 | `src/sql/resolver/dml/ob_dml_resolver.h` | Task 6 | `src/sql/resolver/dml/ob_dml_resolver.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 162 | M | 修改且当前路径分叉 | `src/sql/resolver/expr/ob_raw_expr.h` | Task 6 | `src/sql/resolver/expr/ob_raw_expr.h` | 未移植 | HEAD 同名路径存在；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 163 | M | 修改且当前路径分叉 | `src/storage/CMakeLists.txt` | Task 4 | `src/storage/CMakeLists.txt` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 164 | M | 修改且当前路径分叉 | `src/storage/blocksstable/ob_dag_macro_block_writer.h` | Task 5 | `src/storage/blocksstable/ob_dag_macro_block_writer.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 165 | M | 修改且当前路径分叉 | `src/storage/blocksstable/ob_macro_block_writer.cpp` | Task 5 | `src/storage/blocksstable/ob_macro_block_writer.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 166 | M | 修改且当前路径分叉 | `src/storage/blocksstable/ob_macro_block_writer.h` | Task 5 | `src/storage/blocksstable/ob_macro_block_writer.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 167 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_cg_macro_block_write_op.cpp` | Task 5 | `src/storage/ddl/ob_cg_macro_block_write_op.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 168 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_cg_macro_block_write_task.cpp` | Task 5 | `src/storage/ddl/ob_cg_macro_block_write_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 169 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_cg_macro_block_write_task.h` | Task 5 | `src/storage/ddl/ob_cg_macro_block_write_task.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 170 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_cg_macro_block_writer.cpp` | Task 5 | `src/storage/ddl/ob_cg_macro_block_writer.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 171 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_cg_macro_block_writer.h` | Task 5 | `src/storage/ddl/ob_cg_macro_block_writer.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 172 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_cg_micro_block_write_op.cpp` | Task 5 | `src/storage/ddl/ob_cg_micro_block_write_op.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 173 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_column_clustered_dag.cpp` | Task 5 | `src/storage/ddl/ob_column_clustered_dag.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 174 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_column_clustered_dag.h` | Task 5 | `src/storage/ddl/ob_column_clustered_dag.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 175 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_complement_data_task.cpp` | Task 5 | `src/storage/ddl/ob_complement_data_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 176 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_complement_data_task.h` | Task 5 | `src/storage/ddl/ob_complement_data_task.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 177 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_dag_monitor_entry.cpp` | Task 7 | `src/storage/ddl/ob_ddl_dag_monitor_entry.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 178 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_dag_monitor_entry.h` | Task 7 | `src/storage/ddl/ob_ddl_dag_monitor_entry.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 179 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_dag_monitor_mgr.cpp` | Task 7 | `src/storage/ddl/ob_ddl_dag_monitor_mgr.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 180 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_dag_monitor_mgr.h` | Task 7 | `src/storage/ddl/ob_ddl_dag_monitor_mgr.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 181 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_dag_monitor_node.cpp` | Task 7 | `src/storage/ddl/ob_ddl_dag_monitor_node.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 182 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_dag_monitor_node.h` | Task 7 | `src/storage/ddl/ob_ddl_dag_monitor_node.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_ddl_dag_monitor`、`observer` 链接、schema 生成检索 |
| 183 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_encode_sortkey_utils.cpp` | Task 4 | `src/storage/ddl/ob_ddl_encode_sortkey_utils.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 184 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_encode_sortkey_utils.h` | Task 4 | `src/storage/ddl/ob_ddl_encode_sortkey_utils.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 185 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_independent_dag.cpp` | Task 5 | `src/storage/ddl/ob_ddl_independent_dag.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 186 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_independent_dag.h` | Task 5 | `src/storage/ddl/ob_ddl_independent_dag.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 187 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_merge_sort_task.cpp` | Task 5 | `src/storage/ddl/ob_ddl_merge_sort_task.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 188 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_merge_sort_task.h` | Task 5 | `src/storage/ddl/ob_ddl_merge_sort_task.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 189 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_merge_task_v2.h` | Task 5 | `src/storage/ddl/ob_ddl_merge_task_v2.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 190 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_pipeline.cpp` | Task 5 | `src/storage/ddl/ob_ddl_pipeline.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 191 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_pipeline.h` | Task 5 | `src/storage/ddl/ob_ddl_pipeline.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 192 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_sort_provider.cpp` | Task 5 | `src/storage/ddl/ob_ddl_sort_provider.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 193 | A | 新增且无同名冲突 | `src/storage/ddl/ob_ddl_sort_provider.h` | Task 5 | `src/storage/ddl/ob_ddl_sort_provider.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 194 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_struct.cpp` | Task 5 | `src/storage/ddl/ob_ddl_struct.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 195 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_struct.h` | Task 5 | `src/storage/ddl/ob_ddl_struct.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 196 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_tablet_context.cpp` | Task 5 | `src/storage/ddl/ob_ddl_tablet_context.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 197 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_ddl_tablet_context.h` | Task 5 | `src/storage/ddl/ob_ddl_tablet_context.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 198 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_direct_load_type.h` | Task 5 | `src/storage/ddl/ob_direct_load_type.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 199 | A | 新增且无同名冲突 | `src/storage/ddl/ob_final_merge_sort_write_task.cpp` | Task 5 | `src/storage/ddl/ob_final_merge_sort_write_task.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 200 | A | 新增且无同名冲突 | `src/storage/ddl/ob_final_merge_sort_write_task.h` | Task 5 | `src/storage/ddl/ob_final_merge_sort_write_task.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 201 | A | 新增且无同名冲突 | `src/storage/ddl/ob_fts_macro_block_write_op.cpp` | Task 5 | `src/storage/ddl/ob_fts_macro_block_write_op.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 202 | A | 新增且无同名冲突 | `src/storage/ddl/ob_fts_macro_block_write_op.h` | Task 5 | `src/storage/ddl/ob_fts_macro_block_write_op.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 203 | A | 新增且无同名冲突 | `src/storage/ddl/ob_fts_sample_pipeline.cpp` | Task 5 | `src/storage/ddl/ob_fts_sample_pipeline.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 204 | A | 新增且无同名冲突 | `src/storage/ddl/ob_fts_sample_pipeline.h` | Task 5 | `src/storage/ddl/ob_fts_sample_pipeline.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 205 | A | 新增且无同名冲突 | `src/storage/ddl/ob_fts_sample_task.cpp` | Task 5 | `src/storage/ddl/ob_fts_sample_task.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 206 | A | 新增且无同名冲突 | `src/storage/ddl/ob_fts_sample_task.h` | Task 5 | `src/storage/ddl/ob_fts_sample_task.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 207 | A | 新增且无同名冲突 | `src/storage/ddl/ob_full_text_index_write_task.cpp` | Task 5 | `src/storage/ddl/ob_full_text_index_write_task.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 208 | A | 新增且无同名冲突 | `src/storage/ddl/ob_full_text_index_write_task.h` | Task 5 | `src/storage/ddl/ob_full_text_index_write_task.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 209 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_group_write_macro_block_task.cpp` | Task 5 | `src/storage/ddl/ob_group_write_macro_block_task.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 210 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_group_write_macro_block_task.h` | Task 5 | `src/storage/ddl/ob_group_write_macro_block_task.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 211 | A | 新增且无同名冲突 | `src/storage/ddl/ob_merge_sort_prepare_task.cpp` | Task 5 | `src/storage/ddl/ob_merge_sort_prepare_task.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 212 | A | 新增且无同名冲突 | `src/storage/ddl/ob_merge_sort_prepare_task.h` | Task 5 | `src/storage/ddl/ob_merge_sort_prepare_task.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 213 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_pipeline.cpp` | Task 5 | `src/storage/ddl/ob_pipeline.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 214 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_pipeline.h` | Task 5 | `src/storage/ddl/ob_pipeline.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 215 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_tablet_slice_writer.cpp` | Task 5 | `src/storage/ddl/ob_tablet_slice_writer.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 216 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_tablet_slice_writer.h` | Task 5 | `src/storage/ddl/ob_tablet_slice_writer.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 217 | M | 修改且当前路径分叉 | `src/storage/ddl/ob_writer_args_struct.cpp` | Task 5 | `src/storage/ddl/ob_writer_args_struct.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 218 | M | 修改且当前路径分叉 | `src/storage/direct_load/ob_direct_load_dag_insert_table_row_writer.cpp` | Task 5 | `src/storage/direct_load/ob_direct_load_dag_insert_table_row_writer.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 219 | M | 修改且当前路径分叉 | `src/storage/direct_load/ob_direct_load_dag_lob_builder.cpp` | Task 5 | `src/storage/direct_load/ob_direct_load_dag_lob_builder.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 220 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_dat_dict.cpp` | Task 2 | `src/storage/fts/dict/ob_ft_dat_dict.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 221 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_dat_dict.h` | Task 2 | `src/storage/fts/dict/ob_ft_dat_dict.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 222 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_dict.h` | Task 2 | `src/storage/fts/dict/ob_ft_dict.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 223 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_dict_def.cpp` | Task 2 | `src/storage/fts/dict/ob_ft_dict_def.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 224 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_dict_def.h` | Task 2 | `src/storage/fts/dict/ob_ft_dict_def.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 225 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_range_dict.cpp` | Task 2 | `src/storage/fts/dict/ob_ft_range_dict.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 226 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_range_dict.h` | Task 2 | `src/storage/fts/dict/ob_ft_range_dict.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 227 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_trie.cpp` | Task 2 | `src/storage/fts/dict/ob_ft_trie.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 228 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_ft_trie.h` | Task 2 | `src/storage/fts/dict/ob_ft_trie.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 229 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_gen_dic_loader.cpp` | Task 2 | `src/storage/fts/dict/ob_gen_dic_loader.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 230 | M | 修改且当前路径分叉 | `src/storage/fts/dict/ob_gen_dic_loader.h` | Task 2 | `src/storage/fts/dict/ob_gen_dic_loader.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 231 | A | 新增且无同名冲突 | `src/storage/fts/ik/ob_fast_list.h` | Task 2 | `src/storage/fts/ik/ob_fast_list.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 232 | A | 新增且无同名冲突 | `src/storage/fts/ik/ob_fast_segment_array.h` | Task 2 | `src/storage/fts/ik/ob_fast_segment_array.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 233 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_arbitrator.cpp` | Task 2 | `src/storage/fts/ik/ob_ik_arbitrator.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 234 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_arbitrator.h` | Task 2 | `src/storage/fts/ik/ob_ik_arbitrator.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 235 | A | 新增且无同名冲突 | `src/storage/fts/ik/ob_ik_char_util.cpp` | Task 2 | `src/storage/fts/ik/ob_ik_char_util.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 236 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_char_util.h` | Task 2 | `src/storage/fts/ik/ob_ik_char_util.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 237 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_cjk_processor.h` | Task 2 | `src/storage/fts/ik/ob_ik_cjk_processor.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 238 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_letter_processor.h` | Task 2 | `src/storage/fts/ik/ob_ik_letter_processor.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 239 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_processor.cpp` | Task 2 | `src/storage/fts/ik/ob_ik_processor.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 240 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_processor.h` | Task 2 | `src/storage/fts/ik/ob_ik_processor.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 241 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_quantifier_processor.h` | Task 2 | `src/storage/fts/ik/ob_ik_quantifier_processor.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 242 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_surrogate_processor.h` | Task 2 | `src/storage/fts/ik/ob_ik_surrogate_processor.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 243 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_token.cpp` | Task 2 | `src/storage/fts/ik/ob_ik_token.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 244 | M | 修改且当前路径分叉 | `src/storage/fts/ik/ob_ik_token.h` | Task 2 | `src/storage/fts/ik/ob_ik_token.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 245 | M | 修改且当前路径分叉 | `src/storage/fts/ob_beng_ft_parser.cpp` | Task 2 | `src/storage/fts/ob_beng_ft_parser.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 246 | M | 修改且当前路径分叉 | `src/storage/fts/ob_beng_ft_parser.h` | Task 2 | `src/storage/fts/ob_beng_ft_parser.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 247 | A | 新增且无同名冲突 | `src/storage/fts/ob_ft_token_processor.cpp` | Task 2 | `src/storage/fts/ob_ft_token_processor.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 248 | A | 新增且无同名冲突 | `src/storage/fts/ob_ft_token_processor.h` | Task 2 | `src/storage/fts/ob_ft_token_processor.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 249 | M | 修改且当前路径分叉 | `src/storage/fts/ob_fts_doc_word_iterator.cpp` | Task 2 | `src/storage/fts/ob_fts_doc_word_iterator.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 250 | M | 修改且当前路径分叉 | `src/storage/fts/ob_fts_doc_word_iterator.h` | Task 2 | `src/storage/fts/ob_fts_doc_word_iterator.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 251 | M | 修改且当前路径分叉 | `src/storage/fts/ob_fts_literal.h` | Task 2 | `src/storage/fts/ob_fts_literal.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 252 | M | 修改且当前路径分叉 | `src/storage/fts/ob_fts_parser_property.h` | Task 2 | `src/storage/fts/ob_fts_parser_property.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 253 | M | 修改且当前路径分叉 | `src/storage/fts/ob_fts_plugin_helper.cpp` | Task 2 | `src/storage/fts/ob_fts_plugin_helper.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 254 | M | 修改且当前路径分叉 | `src/storage/fts/ob_fts_plugin_helper.h` | Task 2 | `src/storage/fts/ob_fts_plugin_helper.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 255 | A | 新增且无同名冲突 | `src/storage/fts/ob_fts_stop_token_check.cpp` | Task 2 | `src/storage/fts/ob_fts_stop_token_check.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 256 | A | 新增且无同名冲突 | `src/storage/fts/ob_fts_stop_token_check.h` | Task 2 | `src/storage/fts/ob_fts_stop_token_check.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 257 | D | 删除且当前路径存在 | `src/storage/fts/ob_fts_stop_word.cpp` | Task 2 | `src/storage/fts/ob_fts_stop_word.cpp` | 未移植 | HEAD 待删除路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 258 | D | 删除且当前路径存在 | `src/storage/fts/ob_fts_stop_word.h` | Task 2 | `src/storage/fts/ob_fts_stop_word.h` | 未移植 | HEAD 待删除路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 259 | M | 修改且当前路径分叉 | `src/storage/fts/ob_fts_struct.cpp` | Task 2 | `src/storage/fts/ob_fts_struct.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 260 | M | 修改且当前路径分叉 | `src/storage/fts/ob_fts_struct.h` | Task 2 | `src/storage/fts/ob_fts_struct.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 261 | A | 新增且无同名冲突 | `src/storage/fts/ob_i_ft_parser.h` | Task 2 | `src/storage/fts/ob_i_ft_parser.h` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 262 | M | 修改且当前路径分叉 | `src/storage/fts/ob_ik_ft_parser.cpp` | Task 2 | `src/storage/fts/ob_ik_ft_parser.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 263 | M | 修改且当前路径分叉 | `src/storage/fts/ob_ik_ft_parser.h` | Task 2 | `src/storage/fts/ob_ik_ft_parser.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 264 | M | 修改且当前路径分叉 | `src/storage/fts/ob_ngram2_ft_parser.cpp` | Task 2 | `src/storage/fts/ob_ngram2_ft_parser.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 265 | M | 修改且当前路径分叉 | `src/storage/fts/ob_ngram2_ft_parser.h` | Task 2 | `src/storage/fts/ob_ngram2_ft_parser.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 266 | M | 修改且当前路径分叉 | `src/storage/fts/ob_ngram_ft_parser.cpp` | Task 2 | `src/storage/fts/ob_ngram_ft_parser.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 267 | M | 修改且当前路径分叉 | `src/storage/fts/ob_ngram_ft_parser.h` | Task 2 | `src/storage/fts/ob_ngram_ft_parser.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 268 | M | 修改且当前路径分叉 | `src/storage/fts/ob_whitespace_ft_parser.cpp` | Task 2 | `src/storage/fts/ob_whitespace_ft_parser.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 269 | M | 修改且当前路径分叉 | `src/storage/fts/ob_whitespace_ft_parser.h` | Task 2 | `src/storage/fts/ob_whitespace_ft_parser.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 270 | M | 修改且当前路径分叉 | `src/storage/fts/utils/ob_ft_ngram_impl.cpp` | Task 2 | `src/storage/fts/utils/ob_ft_ngram_impl.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 271 | M | 修改且当前路径分叉 | `src/storage/fts/utils/ob_ft_ngram_impl.h` | Task 2 | `src/storage/fts/utils/ob_ft_ngram_impl.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 272 | M | 修改且当前路径分叉 | `src/storage/retrieval/ob_text_retrieval_token_iter.cpp` | Task 4 | `src/storage/retrieval/ob_text_retrieval_token_iter.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 273 | M | 修改且当前路径分叉 | `src/storage/tablet/ob_tablet_ddl_complete_mds_helper.cpp` | Task 5 | `src/storage/tablet/ob_tablet_ddl_complete_mds_helper.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 274 | M | 修改且当前路径分叉 | `src/storage/tablet/ob_tablet_ddl_complete_mds_helper.h` | Task 5 | `src/storage/tablet/ob_tablet_ddl_complete_mds_helper.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 275 | M | 修改但当前路径不存在 | `unittest/sql/engine/table/test_ob_ai_split_document.cpp` | Task 2 | `unittest/storage/fts/test_ft_parser.cpp` | 未移植 | HEAD 无同名跟踪路径；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 276 | M | 修改但当前路径不存在 | `unittest/sql/memory_usage/optimizer/test_log_plan_size.result` | Task 6 | `unittest/storage/ddl/test_pipeline_and_op.cpp` | 未移植 | HEAD 无同名跟踪路径；待 `test_pipeline_and_op`、`observer` 链接、`ik_custom_dict` |
| 277 | M | 修改且当前路径分叉 | `unittest/storage/CMakeLists.txt` | Task 4 | `unittest/storage/CMakeLists.txt` | 未移植 | HEAD 同名路径存在；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 278 | M | 修改且当前路径分叉 | `unittest/storage/blocksstable/cs_encoding/ob_row_vector_converter.h` | Task 3 | `unittest/storage/blocksstable/cs_encoding/ob_row_vector_converter.h` | 未移植 | HEAD 同名路径存在；待 `test_storage_sort_vec_impl`、`observer` 链接 |
| 279 | M | 修改且当前路径分叉 | `unittest/storage/ddl/test_batch_rows_generater.h` | Task 5 | `unittest/storage/ddl/test_batch_rows_generater.h` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 280 | M | 修改且当前路径分叉 | `unittest/storage/ddl/test_pipeline_and_op.cpp` | Task 5 | `unittest/storage/ddl/test_pipeline_and_op.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_sample_pipeline`、`test_merge_sort_op`、`test_pipeline_and_op`、`observer` 链接 |
| 281 | M | 修改且当前路径分叉 | `unittest/storage/fts/test_ft_parser.cpp` | Task 2 | `unittest/storage/fts/test_ft_parser.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
| 282 | A | 新增且无同名冲突 | `unittest/storage/test_fts_encoding.cpp` | Task 4 | `unittest/storage/test_fts_encoding.cpp` | 未移植 | 上游新增，HEAD 无同名冲突；待 `test_fts_encoding`、`test_fts_property`、`observer` 链接 |
| 283 | M | 修改且当前路径分叉 | `unittest/storage/test_fts_plugin.cpp` | Task 2 | `unittest/storage/test_fts_plugin.cpp` | 未移植 | HEAD 同名路径存在；待 `test_fts_hotpath`、`test_ft_parser`、`test_fts_plugin`、`test_task3` |
