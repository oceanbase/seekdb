# Phase 1b Spike：schema service 第二实例共存

状态：spike 已跑，**验收未通过**——第二实例 init 失败，根因已定位到唯一一处，修法明确。
上游：issue `.scratch/namespace-fork/issues/04-schema-service-second-instance-spike.md`、`namespace_single_process_plan.md` Phase 1.3、`docs/adr/0002-per-ns-service-group.md`。

## 0. 结论

| 项 | 结果 |
|---|---|
| 第二 `ObMultiVersionSchemaService` 实例能否在进程内 init | **否**：`init()` 返回 `OB_INVALID_ARGUMENT`(-4002) |
| 根因 | **唯一一处**：`ObSchemaCache::init()` 往进程级 KV cache 注册表注册**同名** cache，被拒绝 |
| 是否需求引入 ambient 上下文 | 否。修法是 per-ns cache 命名（或注册幂等），不需要 thread_local |
| 其它依赖 | 全部可注入（构造参数），无第二个阻塞点 |
| 门禁是否被影响 | 否。bootstrap 套件在 probe 开/关两种模式下均 PASS |

一句话：**"两个 schema service 实例共存"卡在一个进程级全局注册表上，不是架构性障碍，是命名空间化 cache 注册的问题。**

## 1. 验证方式

probe 挂在 `ObServer::init_schema()` 末尾，由 `SEEKDB_NAMESPACE_SECOND_SCHEMA_PROBE` 开启；因为 harness 以 `--log-level=WARN` 启动且该函数的 `LOG_*` 输出不落盘，结果写文件 `/tmp/ns-schema2-probe.result`：

- `coexist=true …` 表示第二实例 init 成功且版本状态独立；
- `coexist=false init_failed_ret=<ret>` 表示 init 失败并给出错误码。

复现命令：

```bash
source ~/.bashrc && cd build_release && make -j16 seekdb && cd ..
rm -f /tmp/ns-schema2-probe.result
SEEKDB_NAMESPACE_SECOND_SCHEMA_PROBE=1 \
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/data/1/nijia.nj/test \
python3 tools/obtest/namespace_worker_bootstrap_prototype.py --binary build_release/src/observer/seekdb
cat /tmp/ns-schema2-probe.result
```

实测输出：

```
coexist=false init_failed_ret=-4002
```

过程证据（在最终清理前用临时 breadcrumb 插桩取得）：

- `ObServer::init()` 内的 probe 调用点确实执行（文件系统标记验证，排除日志过滤误判）；
- 配对步骤：`start ret=0` → `pre_init ret=0` → 第二次 `init()` 返回 `-4002`，每次实例化均如此；
- init 链内逐段 breadcrumb：`schema_cache ret=-4002`（其余段从未到达）。

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
| 7 | `ObSchemaPublishSignal` / scheduler / backend 生命周期 | — | 与 schema service 同生命周期释放 |

即：**1 处新 API（工厂），其余全是注入**——这与 `plan.md` §"Runtime 持有"的判断一致，此前的"最大技术风险"现在收敛为一个具体的 cache 注册问题。

## 3. 根因

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

主实例已经用 `OB_SCHEMA_CACHE_NAME` 注册过，第二实例注册同一个名字必然失败，错误码一路上抛为 `ObMultiVersionSchemaService::init()` 的返回值。

注意这与 `ObKVGlobalCache` 自己的 `init()` 无关（那是全局缓存本体，只初始化一次）；冲突点在**每实例 cache 对象的名字注册**上。

## 4. 修法（下一步，不在本 spike 内实施）

两条路，推荐第 1 条：

1. **cache 名带 ns 维度**：`ObSchemaCache::init(const char *name_suffix)`（或构造时注入 instance tag），让第二实例注册 `schema_cache_ns<id>`。改动局限在 `ObSchemaCache` 的 3 个 `init(name)` 调用点 + `ObSchemaCache` 的构造/init 签名；`ObKVGlobalCache` 的 `.insts_` 本就按 `cache_id` 分开，容量统计不受影响。
2. **`register_cache` 幂等**：同名时复用已有 `cache_id`。改动更小，但会让两个 ns 共享同一 cache 实例的容量计数，语义上不如第 1 条干净，且与"per-ns 实例、模块内部单 ns 逻辑"的 ADR-0002 定位冲突。

修完后本 spike 的判据变为：`coexist=true`，且
- `primary_before == primary_after`（主实例未被污染），
- `second_after == 4000000000`（第二实例版本状态独立可写）。

## 5. 落地物

- `ObMultiVersionSchemaService::alloc_instance()` / `free_instance()`（`src/share/schema/ob_multi_version_schema_service.h/.cpp`）——per-ns 实例的正式构造入口，非临时脚手架。
- `ObServer::probe_second_schema_service()`（`src/observer/ob_server.cpp`，`SEEKDB_NAMESPACE_SECOND_SCHEMA_PROBE` 门控）——诊断探针，写 `/tmp/ns-schema2-probe.result`，不改变任何启动语义。

## 6. 门禁证据

| 运行 | 结果 |
|---|---|
| probe 关闭 | `{"event": "PASS", "cold_bootstrap": true, "shared_sql_forbidden": true, "recovery": true}`，归档 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_nw87ufa4/data.tar.gz` |
| probe 开启 | `{"event": "PASS", ...}`（同上三项），归档 `/data/1/nijia.nj/test/namespace_fork_PROTOTYPE_bootstrap_v18_hufp3xi_/data.tar.gz` |

两种模式下行为一致，说明新增工厂与 probe 未改变主路径。
