# 插件交付验收清单

日期：2026-09-10。结论：完整设计尚未完成；现有工作有可验证的实现基础，
但不能称为完整 PG 式扩展系统或已达到轻量化/AI 产品验收。

依据是 [下一阶段设计](plugin-next-design.md) 第 1–12 节和当前工作区代码，
不是以现有测试覆盖范围倒推需求。本文是收尾核对入口，不替代逐文件代码审查，
不宣称已穷尽所有缺陷。原设计中的后续方向保留，不在本次核对中自动扩展为新功能。

## 需求、当前证据与完成条件

表内源码路径均相对仓库根目录。“部分”指有接线/受控测试，但不足以证明整项完成；
“待验”不能视为已经通过。没有可靠依据给整个设计计算完成百分比或剩余小时数。

| 原设计要求 | 当前证据与判断 | 达到完成仍需的证据 |
| --- | --- | --- |
| §3 Package / Extension / Module 分离 | 部分：`rust/plugin-runtime/src/package.rs`、`src/share/plugin/extension_package.cpp` 与 `ob_plugin_catalog.cpp` 已有包、版本脚本和目录路径；运行时另有 generation | 数据库作用域、权限/成员/依赖、重启后的逻辑身份以及 native 版本冲突的实库组合验收 |
| §4 SQL、SDK、运行时 catalog 三入口 | 部分：`catalog_spi.h`、`catalog_builder.h`、`extension_script.cpp` 与 `caller_catalog_transaction.cpp` 接入声明、builder、查询期 routine mutation | FUNCTION/PROCEDURE 之外的 TYPE/CAST/OPERATOR、配置表/视图及实体 DDL 逐类接入正常权限、依赖和事务；不能把提交 SQL 文本等同支持该 DDL |
| §4 服务端 SQL SPI | 部分：`sql_spi.h`、`plugin_sql_context.cpp`、Rust SDK SQL wrapper；真实内核夹具覆盖参数、错误及取消 | 使用正常 SQL 连接验证调用者身份、事务可见性、错误恢复和多会话行为；后台会话不能复用查询借用 |
| §5 两档 native API 与边界 | 已有 public/server-dev profile、`ServerDev.cmake`、`rust/plugin-runtime/src/build_contract.rs` 和源码/二进制检查；当前 server-dev 明确限 Linux | 两套构建入口及目标平台的 ABI/构建契约正反例；独立 SDK 交付测试不能替代跨平台布局验证 |
| §6 Function / Aggregate | 部分：`execution_spi.h`、`plugin_function_expr.cpp`、aggregate processor，以及 Rust scalar/batch/aggregate 夹具 | 完整 SQL 类型、状态合并、成本与 volatility 行为的实库矩阵；不能只用直接回调成功作验收 |
| §6 Type / Operator / Index | 部分：已有逻辑身份、codec、比较/cast 与多条表达式/排序接入；`execution_spi.h` 也明确比较后缀不自动提供 hash/operator family/index | typmod/hash、统计、索引 build/insert/delete/scan/recheck、LSM/MVCC 和恢复的纵向插件验收。不能把 GIS codec 或比较函数称为完整索引扩展 |
| §6 Planner / Custom operator / Hook | 部分：`server_dev_planner.h`、`server_dev_executor.h`、`plugin_candidate_graph.h`、`plugin_custom_op.cpp`，Rust candidate/sort/JOIN 与 PX 相关夹具 | 多 hook 并发、真实 EXPLAIN/计划选择、缓存失效、并行执行和 spill/cost/resource 的完整支持矩阵；已有 PX 夹具不等于全 PX 支持 |
| §6 Data source / Background task | 表函数已有查询、批量、取消等基础；不能据此认定完整 connector 和后台调度已交付 | schema discovery、pushdown、rescan/cancel 的真实数据源样例；任务独立会话/事务、停止、资源归属验收 |
| §7 按需装载与 optional provider | 未完成：`ObServerPluginRuntime::recover_before_server_ready` 检查所有期望 ACTIVE 的 artifact；`ObPluginCatalog::recover_before_server_ready` 调用 loader.load 恢复。尚不是普通模块 lazy / hook preload 的分流 | 未调用不装载、首次调用 single-flight、失败重试和并发取消；optional 缺失只影响依赖查询，恢复必需模块仍严格检查 |
| §7 AI 执行与轻量化 | 部分：批量、poll/deadline、host-owned 内存、启动额度和 SQL 用量视图已有基础 | 模型/GPU/连接池独立惰性初始化、任务预算/幂等/外部副作用契约；core/package 体积、冷启动、RSS、线程、首次调用、吞吐与取消/释放的可复现测量 |
| §8 原子安装和调用者事务 | 部分：Root routine writer、schema/privilege overlay、savepoint、Rust install/query transaction 协调器与受控夹具；安装入口仍拒绝已有事务 | 正常连接下提交/回滚、保存点、权限、schema invalidation、并发可见性；实体 DDL/storage task 的原子性另行证明，不能使用 DROP 补偿替代 |
| §9–10 Rust runtime / SDK / 错误资源模型 | 已有 Rust host runtime、唯一 generation 状态、native loader、注册/依赖/内存等算法；`rust/seekdb-host` 汇总宿主静态库；GIS 算法保留 C++ | 各调用入口的错误/取消/关闭覆盖与真实模块资源验收。host panic=abort 与插件 unwind 的保证必须分开，native Rust 不是沙箱 |
| §9 SDK 开发工具 | `rust/cargo-seekdb/src/main.rs` 已实现 new/schema/package；`rust/extension-sdk` 提供 FFI 和安全封装；公开 catalog 头文件漏装已在本次修复 | 设计中的 build/test 开发体验尚不能按已有独立子命令宣称完成；独立消费、打包、安装和测试需形成可复现教程 |
| §11 三个纵向样例 | Rust text、纯 SQL 包、Rust candidate、自定义执行与 C++ GIS 均有源码及受控加载测试 | 样例 1 的实库原子安装；样例 2 的并发 hook/真实计划；样例 3 的完整类型/索引/恢复。三者都不能只以单测总数通过代替 |
| §11 两套构建/平台/性能 | CMake 生产构建及受控内核链已有通过记录；`rust/BUILD.bazel` 已有宿主聚合构建定义 | 实际 Bazel 构建/测试、整个 feature-OFF 服务端、目标平台与性能结果。存在 BUILD 文件或编译单个 OFF 对象不等于完成 |

## 当前验证记录如何解读

- 最近一次完整独立 CTest 为 **33/33**，耗时 **105.54 秒**。核对时本地
  `build_release/plugin-runtime-tests/Testing/Temporary/LastTest.log` 有 33 项
  `Test Passed.`，结束时间为 Sep 10 11:54 CST。该日志会被后续 CTest 覆盖，
  不是随源码提交的永久构建证明；对应运行记录见 [实施进度](plugin-implementation-status.md)。
- 生产构建、受控 kernel、runtime 136 项、candidate 28 项、text 4 项，以及
  格式/静态检查的通过记录见实施进度；不在本轮为了更新总数重复运行。
- 新增 SDK 安装测试先复现 `catalog_spi.h` 缺失，再在修复后通过：真实执行
  `Plugin.cmake` 的安装/导出，11 个公开头文件分别编译 C 和 C++ 消费者。
  命令：`python3 cmake/test_plugin_sdk_install.py`。不构建服务端、不加载插件。
  新增 CTest 名为 `plugin_sdk_install`，不计入上述历史 33/33。
  独立工程重新配置后，该 CTest 单项 **1/1 通过，3.63 秒**；这不是完整 34/34。
- `query_catalog_server.py` 明确是 opt-in 实库测试；CTest 中的
  `plugin_query_catalog_runner` 只测验收脚本辅助逻辑。不能拿后者证明实库事务。
  当前没有可交付的实库测试通过证据。

## 收尾顺序与停止扩张规则

1. 本轮完成验收索引与独立 SDK 漏装修复，不新增扩展点、不重复整库构建。
2. 后续优先完成现有 catalog 纵向链的实库验收，确认 routine 开放真正可用。
   使用明确的可丢弃测试实例及已配置插件；当前环境此前无法建立监听 socket，
   不能把受控夹具改名为实库测试，也不能擅用生产实例。
3. 随后推进普通模块 lazy/preload 分流，直接对应轻量化目标；模型/后台任务不再
   混入同一个无截止条件的实现循环。
4. 按原设计保留深度类型/索引、并发 planner 和 AI 资源契约，并为每项设独立的
   输入、输出和验收命令。完整目标仍要求这些能力的适用验收，不因本轮冻结而删除。

阶段成果可以用于审查，不代表生产发布就绪。发布或缩减原目标需要用户明确决策；
本次未提交、推送代码，也未更新语雀。没有完成证据前，完整目标保持未完成。
