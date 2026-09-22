# Phase 1b Spike：schema service 第二实例共存

状态：spike 完成，**验收通过**——第二实例可 init，且两实例版本状态互不干扰。
上游：issue `.scratch/namespace-fork/issues/04-schema-service-second-instance-spike.md`、`namespace_single_process_plan.md` Phase 1.3、`docs/adr/0002-per-ns-service-group.md`。

## 0. 结论

| 项 | 结果 |
|---|---|
| 第二 `ObMultiVersionSchemaService` 实例能否在进程内 init | **能**（修 cache 命名后 `init_ret=0`） |
| 两实例版本状态是否互相干扰 | **否**：写第二实例的 published version，主实例读数不变 |
| 是否需要 ambient 上下文 | 否。修法是 per-ns cache 命名，未引入 thread_local |
| 隐藏全局依赖 | 唯一一处：`ObKVGlobalCache` 进程级注册表的 cache 名字冲突，已处置 |
| 门禁是否被影响 | 否。bootstrap 套件在 probe 开/关两种模式下均 PASS |

实测结果：

```
coexist=true init_ret=0 primary_before=1 primary_after=1 second_before=1 second_after=4000000000 primary_leaked=0 second_isolation=1
```

## 1. 验证方式

probe 挂在 `ObServer::init_schema()` 末尾，由 `SEEKDB_NAMESPACE_SECOND_SCHEMA_PROBE` 开启；因为 harness 以 `--log-level=WARN` 启动且该函数的 `LOG_*` 输出不落盘，结果写文件 `/tmp/ns-schema2-probe.result`：

- `coexist=true …` 表示第二实例 init 成功且版本状态独立；
- `coexist=false init_failed_ret=<ret>` 表示 init 失败并给出错误码。

判据：`primary_leaked=0`（主实例未被污染）且 `second_isolation=1`（第二实例版本状态可独立写入）。

复现命令：

```bash
source ~/.bashrc && cd build_release && make -j16 seekdb && cd ..
rm -f /tmp/ns-schema2-probe.result
SEEKDB_NAMESPACE_SECOND_SCHEMA_PROBE=1 \
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/data/1/nijia.nj/test \
python3 tools/obtest/namespace_worker_bootstrap_prototype.py --binary build_release/src/observer/seekdb
cat /tmp/ns-schema2-probe.result
```

## 2. 依赖清单与处置

搭一个 per-ns schema 服务组需要下列对象。全部已实测为**构造注入可满足**，无 ambient 需求。

| # | 对象 | 来源 | 处置 |
|---|---|---|---|
| 1 | `ObMultiVersionSchemaService` | 需要第二实例 | **已加工厂** `alloc_instance()` / `free_instance()`（构造/析构原为 protected，单例独占） |
| 2 | `ObSchemaStatusProxy` | `ObSchemaStatusProxy(sql_proxy_)` | 注入本 ns 的 sql proxy；需要 `init()` |
| 3 | `ObSchemaPublishSignal` | 栈对象 | 每实例一个；需要 `init()` |
| 4 | `ObSchemaServiceSQLImpl` | `(max_id_cache, ddl_sql_proxy, schema_service)` | 注入 |
| 5 | `ObMaxIdCacheAdapter` | `(local_management_service)` | 需要 per-ns 的 `ObLocalManagementService`（Phase 1 已有清单项） |
| 6 | `ObSchemaRefreshSchedulerAdapter` | `(ob_service_, schema_service)` | 需要 per-ns `ObService`（Phase 1 已有清单项） |
| 7 | `ObSchemaCache` 的 KV cache 注册 | 进程级 `ObKVGlobalCache` | **已修**：per-instance cache 名（见 §3/§4） |

即：**1 处新 API（工厂）+ 1 处命名空间化（cache 名），其余全是注入**——这与 `plan.md` §"Runtime 持有"的判断一致。

## 3. 根因（已修）

调用链：

```
ObMultiVersionSchemaService::init()
  └─ schema_cache_.init()                                  // ob_schema_cache.cpp:378
       └─ cache_.init(OB_SCHEMA_CACHE_NAME)                // ObKVCache<Key,Value>::init
            └─ ObKVGlobalCache::get_instance().register_cache(name, pct, cache_id)
                 └─ 已存在同名 cache → ret = OB_INVALID_ARGUMENT   // ob_kv_storecache.cpp:480
```

`ObKVGlobalCache` 是进程级单例（`get_instance()`），其 `register_cache()` 在 `ob_kv_storecache.cpp:479-482` 明确拒绝重名注册：

```cpp
if (0 == STRNCMP(cache_name, configs_[i].cache_name_, MAX_CACHE_NAME_LENGTH)) {
  ret = OB_INVALID_ARGUMENT;
  COMMON_LOG(WARN, "The cache name has been registered, ", K(ret));
}
```

主实例已经用 `OB_SCHEMA_CACHE_NAME` 注册过，第二实例注册同一个名字必然失败，错误码一路上抛为 `ObMultiVersionSchemaService::init()` 的返回值 `-4002`。

注意这与 `ObKVGlobalCache` 自己的 `init()` 无关（那是全局缓存本体，只初始化一次）；冲突点在**每实例 cache 对象的名字注册**上。

## 4. 修法（已实施）

采用"cache 名带实例维度"方案，`ObKVGlobalCache` 的 `.insts_` 本就按 `cache_id` 分开，容量统计不受影响，符合 ADR-0002 的"per-ns 实例、模块内部单 ns 逻辑"定位。

- `ObSchemaCache::init(const char *instance_tag = nullptr)`：把 tag 接到三个 KV cache 名后面（`schema_cache` / `schema_history_cache` / `tablet_table_cache`）。**主实例传 nullptr，缓存名与历史完全一致**，无行为变化；第二实例用 `schema_cache_ns2_probe` 等独立名字。
- `ObMultiVersionSchemaService::init(..., const char *instance_tag = nullptr)`：把 tag 透传给 `schema_cache_.init()`。默认参数保证现有 4 个调用点零改动。
- tag 由 Runtime 构造边界（`src/namespace/`，合法知道 ns 身份）在 `init()` 时显式传入；schema service 与 `ObSchemaCache` 都不解析 ns 语义，不违反不变式 2b。

## 5. 落地物

- `ObMultiVersionSchemaService::alloc_instance()` / `free_instance()`（`src/share/schema/ob_multi_version_schema_service.h/.cpp`）——per-ns 实例的正式构造入口，非临时脚手架。
- `ObSchemaCache::init(instance_tag)` + `ObMultiVersionSchemaService::init(..., instance_tag)`——per-ns cache 命名空间化，默认参数向后兼容。
- `ObServer::probe_second_schema_service()`（`src/observer/ob_server.cpp`，`SEEKDB_NAMESPACE_SECOND_SCHEMA_PROBE` 门控）——诊断探针，写 `/tmp/ns-schema2-probe.result`，不改变任何启动语义。

## 6. 门禁证据

| 运行 | 结果 |
|---|---|
| probe 关闭 | `{"event": "PASS", "cold_bootstrap": true, "shared_sql_forbidden": true, "recovery": true}`，归档 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_j1rk17yf/data.tar.gz` |
| probe 开启 | `{"event": "PASS", ...}`（同上三项）+ `coexist=true … second_isolation=1`，归档 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_hufp3xi_/data.tar.gz` |

两种模式下门禁均通过，说明新增工厂、cache 命名空间化与 probe 未改变主路径。

## 7. 未完成 / 下一步

本 spike 只证明"第二实例能共存 + 版本状态独立"。issue 04 的另一半——**两个实例各自版本推进互不干扰地刷新真实 schema**——需要 per-ns 的 `ObLocalManagementService` / `ObService`（§2 的 #5/#6），属于 Phase 1 Runtime 服务组的落地内容，随 issue 03/05 一起做。
