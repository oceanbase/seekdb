# 工单 01：隐式"当前版本"调用点三分类报告

- 审计口径：`get_runtime_schema_guard` 默认版本 279 + `get_runtime_refreshed_schema_version` 45 + `get_published_schema_version` 19 = 343。
- 复核口径（当前工作树，逐点解析含跨行调用、排除声明/定义/显式传版本的 28 处）：327 + 43 + 17 = **387** 个唯一调用点。分类结论对两个口径均成立。

## 总表

| 分类 | 数量 | 占比 |
|---|---|---|
| 分类 1：能传（session/exec_ctx/任务参数/构造注入） | 322 | 83% |
| 分类 2：能推（编码 id 推导 / registry 枚举） | 65 | 17% |
| 分类 3：必须 ambient | 0 | 0% |

## 分类 1：能传（322 处）

| 模块组 | 数量 | 判定依据 |
|---|---|---|
| `rootserver/` DDL 服务+操作符 | 68 | DDL 入口 arg 携带目标 id 且由 session 发起；bootstrap 路径 ns 固定为 1（显式常量注入） |
| `rootserver/ddl_task/` | 74 | 任务对象持久化携带 `target_table_id`/`database_id`，ns 是任务显式属性 |
| `share/schema/` | 34 | 机制自身重写为 per-ns 版本存储后 ns 由入口显式传入；`ObLatestSchemaGuard` 构造注入 |
| `sql/engine/` + `engine/px` | 44 | executor/expr 均有 `ObExecContext`→session；PX 子任务有 `task_exec_ctx` |
| `observer/mysql/`（obmp_*） | 27 | 接入层每个 processor 持有 session，含点查热路径 4 处 |
| `observer/namespace_worker_*.ipp` + `ob_inner_sql_connection.cpp` | 14 | 翻译层 worker 本身是 per-ns 对象（构造注入）；inner SQL 已有显式宿主 ns 机制 |
| `sql/` 其他（`ob_sql`/SPI/重试） | 12 | SPI/重试均挂在 session 级路径上 |
| `share/` DDL 工具 | 8 | 调用方持目标 ns，顺调用链补参数 |
| resolver/das/session/code_generator/optimizer/executor | 17 | 天然 session 在手 |
| parallel_ddl/pl_ddl/fork_table/truncate_info | 16 | helper 持目标表 schema；fork 任务 ns 显式 |
| `pl/` | 4 | PL 执行均有 `ObPLExecCtx`→session |
| `observer/` 其他（scheduler/ai/schema_updater） | 3 | session/任务参数在手 |
| `observer/virtual_table/ob_virtual_table_projector.cpp` | 1 | 虚表迭代器运行在 session 下 |

## 分类 2：能推（65 处）

| 模块组 | 数量 | 判定依据 |
|---|---|---|
| `storage/` | 20 | 手里都有 tablet/table id，ns 由 `database_of(id)` 推出；stat_mgr 需按 ns 分组批处理 |
| `observer/vector_index/` | 22 | 全部绑定具体 table/tablet id；全局调度器走 registry 枚举 |
| `rootserver/freeze/` | 8 | 操作对象是 tablet/table id 集合；门控点走枚举 |
| `observer/change_stream/` | 4 | 监控对象属于具体 database；空闲侦测改枚举各 ns 版本 |
| `observer/ob_server.cpp` | 3 | 启动门控/CTAS 清理语义是"全体 ns"，registry 枚举 |
| `ob_all_virtual_server_schema_info` | 3 | 虚表改输出 per-ns 行 |
| `ob_opt_stat_monitor_manager` | 2 | 统计条目自带 table_id，按 ns 分组 |
| `ob_maintain_dependency_info_task` | 1 | 任务携带依赖对象 id 集合 |
| `ob_user_resource_mgr` | 1 | map 条目携带用户身份，逐条推导或枚举 |
| `ob_local_management_service` | 1 | 回收站对象自带 table id，按 ns 分组 |

## 分类 3：必须 ambient（0 处）

严格满足"拿不到 session/任务上下文、推不出 ns、连 registry 枚举都不成立"的调用点为 0。最接近的 17 处后台门控点（取 guard 瞬间无 session 无 id，语义=全体 ns）全部可归入分类 2 的 registry 枚举子模式，集中在 6 个组件：启动门控（`ob_server.cpp:1498/2988/3002`）、冻结门控（`ob_major_freeze_helper.cpp:92`、`ob_local_major_freeze.cpp:314`）、compaction restore 标志（`ob_runtime_status_cache.cpp:72`）、change stream 空闲侦测（`dispatcher:103`、`fetcher:361/757/840`）、连接资源清理（`ob_user_resource_mgr.cpp:326`）、回收站/虚表/向量索引调度（`local_management:2906`、`server_schema_info:36-40`、`async_task_util:341`、`load_scheduler:201`）。

诚实附注：若裁定"registry 枚举"也违反显式传递原则，最坏口径分类 3 = 17 > 10；但这 17 处语义全是"全体 ns"而非"隐式当前 ns"，重写模式统一，不构成可行性卡点。

## 结论

- 分类 3 = 0 ≤ 10，方案成立：显式传递 + id 推导 + registry 枚举，无需任何 thread_local/scoped ambient。
- 工作量分布与审计预判一致：>50% 在 rootserver/DDL（约 210 处），全部经任务/arg 显式获得 ns；点查热路径 4 处均属分类 1。
- 相邻项：`ob_multi_version_schema_service.cpp` 的 `thread_local worker_request_schema_version` pin（约 6 处）是目前唯一现存 ambient 机制，需与本次改造一并消除。
- 工程提示：17 处枚举型点位是轮询路径，per-ns 枚举成本乘 N，registry 宜提供"版本快照数组"批量接口。
