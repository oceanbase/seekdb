# 工单 04：schema service 第二实例共存 spike 报告

目的：验证进程内两个 `ObMultiVersionSchemaService` 实例共存（单进程化最大技术风险点排雷）。

## 结论

**可行。** 第二实例 init + 版本推进成功，与 THE_ONE 互不干扰。实测输出：

```
PROTOTYPE_SCHEMA_SPIKE stage=done ret=0 the_one_version=1790099913891192 second_version=1051440
```

复现：bootstrap 套件带 `SEEKDB_NS_SCHEMA_SPIKE=1` 运行，worker 在首个会话建立后执行 spike（`namespace_sql_worker_prototype.ipp` 的 `run_schema_spike`）。

## 实测发现的隐藏全局依赖与处置

| 依赖 | 位置 | 性质 | 处置 |
|---|---|---|---|
| `ObMultiVersionSchemaService::get_instance()` THE_ONE | `ob_multi_version_schema_service.cpp:1118` | 全 composition 硬连唯一实例；构造函数 protected 强制单例 | Phase 2：放开构造（public 或 friend Runtime），全部消费方改构造注入（见工单 01 分类报告） |
| 具名 KV cache 全局注册 | `ObKVGlobalCache::register_cache`，重名报 `OB_INVALID_ARGUMENT`；`ObSchemaCache::init` 固定注册 `schema_cache`/`schema_history_cache`/`tablet_table_cache` | **本次 spike 实测命中的唯一 init 阻断点** | 已修：`ObSchemaCache::init`/`ObMultiVersionSchemaService::init` 增加 `cache_name_suffix`，第二实例注册 `...@suffix` 槽位 |
| `ObSchemaConstructTask::get_instance()` | `ob_multi_version_schema_service.cpp:92` | 按版本号串行化 construct；跨实例会误串行，wait/wakeup 版本号互相误伤 | per-ns 实例化（改为成员） |
| `worker_request_schema_version` thread_local | `namespace_worker_protocol_prototype.h:161` | ambient pin | 改 session 级（既定结论，随工单 07/08 消除） |
| `ObSysTableChecker::instance()` | `ob_server_schema_service.cpp` init 路径调用 | 幂等只读登记表 | 保持共享，零改动 |
| `ObSchemaPublishSignal` / `ObSchemaRefreshSchedulerAdapter` / `ObSchemaServiceSQLImpl` / `ObMaxIdCacheAdapter` | ObServer 成员 | 本身可实例化 | per-ns new（spike 已验证） |
| `bind_server_service<...>` 进程级服务表 | worker bootstrap | 第二套服务绑定会互相覆盖 | Phase 2：改经 Runtime 取服务实例 |
| refresh 驱动 | 外部调用（ObService 任务 / 会话路径），服务自身无定时器 | 无共享态 | per-ns 调度 |

## 非阻碍项（实测确认）

- `schema_fetcher_` / `schema_mgr_cache_` / `ddl_trans_controller_`（自带线程池）/ `ddl_epoch_mgr_` / `version_his_map_` / 内存上下文：全部实例局部。
- worker 原型的存储路由（`StorageSessionScope`/`begin_direct_request`）依赖会话绑定上下文——这是 worker 模式的 IPC 机制，与多实例无关；spike 的 inner-SQL 全量 refresh 因此改用 `broadcast_runtime_schema` 直推版本验证隔离性。单进程后无 IPC 路由，该约束消失。

## 门禁

- `SEEKDB_NS_SCHEMA_SPIKE=1` + bootstrap 套件：PASS 且 spike `stage=done ret=0`
- 不带环境变量的正常 bootstrap 套件：PASS
