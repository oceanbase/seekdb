# 单进程多 namespace 可行性审计（点查追平单体路线）

状态：审计结论（未实施）。目标函数：`sysbench oltp_point_select` 追平单体（单体 0.13ms/query，worker 当前 0.91ms/query）。

## 0. 结论摘要

| 结论 | 数字 |
|---|---|
| 存储读/写路径的 ns 敏感点 | **0**（ns 已编码进 tablet id，靠 `database_of(id)` 推导） |
| 点查热路径的 ns 敏感点 | **4 处**（全在 schema 版本获取） |
| 全仓"隐式当前版本"schema 调用点 | **343**（279 + 45 + 19），但 **>50% 在 DDL/rootserver** |
| 已具备的 per-ns/session 载体 | 3 个（session binding / schema guard cache / 内部 SQL ns override） |
| 需要新增的真实改动 | plan cache key 加 1 个维度；schema version pin 从 `thread_local` 改 session 级（~6 处） |

判定：**行数量级远低于 500 行阈值 → 单进程多 namespace 是明确正确的方向。**

> **措辞更正（重要）**：本设计里不存在"多 namespace 模式"。存储引擎是 **ns-blind** 的，ns 只以**数据**形式存在（编码进 id / 显式 `ns` 参数 / 每 ns 元数据键空间），唯一感知 ns 的是翻译层 `NamespaceForkKernelPrototype`。因此正确的说法是"**把 SQL 侧也做成 ns-blind**"，而不是"单进程多 namespace"。相应地，**ambient 上下文（thread_local / scoped guard）不是可选项，是违例**——第 5 节原来的三选一作废，见第 9 节。

## 1. 存储读写路径：0 改动

namespace 已经编码在 tablet id 里：`id = [ns:32][local:32] | ID_MARK`。

```
namespace_fork_kernel_prototype.cpp:305  database_of(id) = (id & ~ID_MARK) >> 32
namespace_fork_kernel_prototype.cpp:306  local_of(id)    = id & 0xffffffff
namespace_fork_kernel_prototype.h:66     static bool is_encoded_id(uint64_t id)
```

读路径与写路径全部只吃 id、自己推出 ns：

| 位置 | 签名 | ns 来源 | 改动 |
|---|---|---|---|
| `ob_access_service.cpp:597` | `resolve_read_tablet(tablet_id, physical, cap)` | `database_of`/`local_of` | **0** |
| `namespace_fork_kernel_prototype.cpp:1409` | `check_table_access(table_id, tablet_id, held)` | 同上 | **0** |
| `namespace_fork_kernel_prototype.cpp:3316` | `ensure_tablet_impl(tablet_id, ...)` | 同上 | **0** |
| `namespace_fork_kernel_prototype.cpp:1418` | 非编码 id 回退 ns=1 | 兼容分支 | **0** |

这不是"存储层已经在做多 namespace"，而是**存储层本来就是 ns-blind 的**：它只认不透明的 64 位 id，ns 的语义完全由 id 的编码承载、由翻译层解释。共享进程今天能服务所有 ns，正是因为这个性质。

实测复核（`is_encoded_id` 的 38 处外部调用点）：

| 位置 | 数量 | 性质 |
|---|---|---|
| `src/storage/tx_storage/ob_access_service.cpp:677` | 1 | **`DEBUG_SYNC` 测试插桩**，非逻辑 → 存储引擎实际 **0** |
| `storage/ddl/ob_tablet_fork_task.cpp` | 3 | fork 机制本身 |
| `observer/namespace_worker_*.ipp` | 11 | 翻译层/适配器本身 |
| `rootserver/parallel_ddl` | 4 | fork 相关 DDL |
| `share/schema`（`ob_schema_getter_guard` 等） | 19 | 按 id 查 schema 的边界，同时收两种 id 表示 |

**结论：存储引擎读路径 0 处真实感知**，"存储层不感知 ns"的目标已达成。

## 2. 点查热路径：4 处 ns 敏感点，全在 schema 版本

热路径：`ObMPQuery::process` → `process_single_stmt` → `ObSql::stmt_query` → `ObResultSet::open/execute/get_next_row/do_close`。

| # | 位置 | 读的是什么 | ns 敏感 | 改法 |
|---|---|---|---|---|
| 1 | `observer/mysql/obmp_query.cpp:104` | `get_published_schema_version(version)` | ✅ | 走第 5 节机制，调用点零改动 |
| 2 | `observer/mysql/obmp_query.cpp:515` | `get_runtime_refreshed_schema_version(tmp)` | ✅ | 同上 |
| 3 | `observer/mysql/obmp_query.cpp:850` | `task_ctx.schema_service_ = gctx_.schema_service_` | ⚠️ | 传服务本身不敏感，版本敏感由 1/2 覆盖 |
| 4 | `sql/ob_sql.cpp:1115` | `get_runtime_schema_guard(guard)` | ✅ | 同上 |

热路径上的其他全局访问，**全部 ns 不敏感**：

| 位置 | 内容 | 判定 |
|---|---|---|
| `obmp_query.cpp:127,439`、`ob_sql.cpp:1428,3115,3380`、`ob_sql_trans_control.cpp:431,665,1141`、`obmp_packet_sender.cpp:612,796` | `GCONF.*`（实例级配置） | ❌ 不敏感，不动 |
| `ob_sql_trans_control.cpp:1141`、`ob_result_set.cpp:64,77`、`ob_sql.cpp:2177` | `GCTX.sql_proxy_` | ⚠️ 靠已有的 `ControlSqlNamespaceScope` override |
| `obmp_packet_sender.cpp:741-766` | `server_service<ObSQLSessionMgr>()` | ❌ 不敏感（见 §4） |

热路径文件本身的全局访问密度极低（`get_instance()` 基本为 0）：

```
obmp_query.cpp        1337 行  get_instance=0  GCTX=1  GCONF=2
ob_sql.cpp            4278 行  get_instance=0  GCTX=2  GCONF=3
ob_result_set.cpp     1604 行  get_instance=0  GCTX=3  GCONF=0
ob_sql_trans_control  1284 行  get_instance=0  GCTX=0  GCONF=3
ob_query_driver.cpp    616 行  get_instance=0  GCTX=0  GCONF=0
```

## 3. 真正的 ns 敏感面：schema 的"隐式当前版本"

`ObMultiVersionSchemaService::get_runtime_schema_guard` 的版本参数**有默认值**：

```cpp
// src/share/schema/ob_multi_version_schema_service.h:158-160
virtual int get_runtime_schema_guard(ObSchemaGetterGuard &guard,
                                     int64_t runtime_schema_version = common::OB_INVALID_VERSION,
                                     const RefreshSchemaMode refresh_schema_mode = ...);
```

于是所有不带版本参数的调用点，语义都是"取**服务端当前**的 runtime schema"。在单进程多 ns 下这就是歧义点。全仓统计：

| 访问器 | 语义 | 调用点数 |
|---|---|---|
| `get_runtime_schema_guard(guard)`（用默认版本） | 取服务端当前版本 | **279** |
| `get_runtime_refreshed_schema_version(v)` | 取服务端已刷新版本 | 45 |
| `get_published_schema_version(v)` | 取服务端已发布版本 | 19 |
| **合计** | | **343** |

这 343 处就是"各个模块都要感知 ns"在这份代码库里的**精确形态**——不是给每个函数加参数，而是**"当前版本"这个默认语义需要一个 ns 维度**。

### 279 处按目录分布

| 目录 | 数量 | 性质 |
|---|---|---|
| `rootserver` + `rootserver/ddl_task` + `parallel_ddl` + `pl_ddl` + `freeze` + `fork_table` | **152** | DDL / DDL 任务 / 冻结 |
| `share/schema` + `share` | 28 | schema 服务自身 |
| `observer/vector_index` / `observer/mysql` / `observer` / `observer/virtual_table` | 35 | 接入层 + 虚表 |
| `sql/engine/expr` / `engine/cmd` / `resolver/ddl` / `das` / `session` / `optimizer` / `code_generator` | 35 | SQL 层 |
| `storage/ddl` / `storage/ls` / `storage/tablet` / `storage/lob` / `storage` | 13 | 存储侧 |
| 其他 | 16 | |

**>50% 在 DDL/rootserver**，点查热路径只占 4 处。这决定了两件事：

1. 只让点查 work 是不够的——一旦共享进程能跑 SQL，同一 ns 的 DDL/PL/虚表都必须能跑，否则产品不成立。
2. 但 DDL 路径通常**自己知道在操作哪个 ns**（它是对某个 namespace 的显式 DDL），所以这 152 处大概率可以走显式参数，而不是 ambient。

## 4. 已经具备的 per-ns / per-session 载体（无需新建）

| 载体 | 位置 | 说明 |
|---|---|---|
| session 的 ns 绑定 | `sql/session/ob_sql_session_info.h:415` `namespace_storage_binding()` | session 级，已在用 |
| session 的 schema guard cache | `session.get_cached_schema_guard_info()`（`obmp_query.cpp:596,815`） | 已挂在 session 上，天然 per-ns |
| 内部 SQL 的 ns override | `ControlSqlNamespaceScope`（`namespace_fork_kernel_prototype.cpp:217`）+ `push_inner_sql_namespace_override` / `bind_shared_inner_sql_namespace` | 已实现 |
| 事务服务 | `share::server_service<ObTransService>()` | **全局一套，今天已在服务所有 ns**（`write:470,1402`） |
| session id 分配 | `ob_sql_session_mgr.cpp:213 next_sessid_` 全局自增 | 跨 ns 天然唯一 |
| schema 服务 | `ObMultiVersionSchemaService::get_instance()` | 全局一套，今天已在服务所有 ns |

对照 OceanBase 的 `MTL()`/`ObTenantSwitchGuard`：**这份代码库里没有 MTL、没有 ObTenantBase（已确认 0 处引用）**，而上面这些载体已经覆盖了大部分需求。这是"seekdb 把多租户拆掉"之后留下的有利地形。

## 5. 唯一的设计抉择：ns 从哪里来

343 处调用点都要回答"当前 ns 是哪个"。三条路：

| 路 | 机制 | 调用点改动 | 风险 |
|---|---|---|---|
| **A. ambient** | `get_runtime_schema_guard` 的默认值解释为"当前上下文 ns 的当前版本"；用 scoped guard 在请求入口建立上下文 | **0** | 就是 OceanBase 的老路：靠 save/restore 纪律，漏一次 = 静默读错 schema → 错 tablet → 跨 ns 数据访问 |
| **B. 显式 / 从 id 推** | 每个调用点显式传 ns 版本；或让 table/db id 也纳入 ns 编码，从而 `(version, encoded_id)` 自洽 | 152 处 DDL 路径大概率可传；无 id 的少数点仍需 ambient | 无静默风险；但要动 id 体系（对应 TODO #4「ns1 tablet 纳入编码公式 + catalog 树退役」） |
| **C. 只覆盖点查** | 4 处显式传 ns 版本 | 4 | 改动最小，但产品上不成立（见 §3） |

**关键约束**：guard 的版本**不能**只靠 id 推。fork 的 schema 是"`schema_version <= fork_cap` 的行 + 本 ns 后续 DDL"，而父 ns fork 之后的 DDL 必须对子不可见。所以"当前版本"本质上是一个 per-ns 的量，必须显式给出或由上下文给出。

## 6. 需要新增的改动清单

| 项 | 位置 | 改动 | 量级 |
|---|---|---|---|
| schema 版本 per-ns 存取 | `share/schema/ob_multi_version_schema_service.{h,cpp}` | 版本存储 + 3 个访问器支持 ns 维度 | 中（单文件） |
| 每语句 schema 版本 pin | `ob_multi_version_schema_service.cpp:664,1724,1729,2380`（`thread_local worker_request_schema_version`） | **必须改成 session 级**：合并后线程跨 ns 复用，thread_local 会串 | ~6 处 |
| plan cache key | `sql/plan_cache/ob_plan_cache_struct.h:56` | 加 1 个 fork-ns 字段 + hash + 相等 + `deep_copy` | **~6 行** |
| plan cache key 构造 | `ob_plan_cache.cpp:1985` `construct_plan_cache_key(session, ns, key)` | 加 1 行取 session 的 ns | **1 行** |
| plan cache 归属（二选一） | `observer/ob_server.h:729 mods_plan_cache_` + `ob_server.h:446 sql_engine_` | 走 key 加维度（推荐，不动归属）；或 per-ns 实例（要 N 个 `ObSql`） | 小 / 中 |
| session → per-ns 状态包 | `ob_sql_session_info.h:415` | `SessionBinding*` 扩成 `NamespaceBase*`（本次不需要，留作将来） | 小 |
| **不该动的** | `GCONF.*`、`server_service<ObSQLSessionMgr>`、`ObTransService`、session id 分配、存储读写路径 | | **0** |

**注意**：`ObPlanCacheKey::namespace_`（`ob_plan_cache_struct.h:56`）是 `ObLibCacheNameSpace`（`NS_CRSR` 等库缓存命名空间），**不是** fork namespace。这是审计中唯一的命名陷阱，别改错字段。

## 7. 判定与下一步

对照开工前定的阈值：

- 总改动 **< 500 行** → 合并是明确正确的方向
- 500 ~ 2000 行 → 可做，但要钉死状态包边界并保留 `worker_process` 隔离模式
- \> 2000 行 → 回退到进程模式，改做读路径共享化

**当前判定：改动量在数百行量级（远低于 500 行阈值），方向成立。** 但审计还缺最后一步，它才是决定风险的东西：

> **把 343 处按"调用点是否已经知道 ns"分三类：**
> 1. **能传**——调用点有 session / DDL 目标 / 显式 ns（预计 152 处 DDL 路径大多数属于这类）
> 2. **能推**——调用点手里有 ns 编码的 id
> 3. **必须 ambient**——既没 session 也没 id（真正的风险源，数量决定成败）

分类 3 的数量就是"纪律风险"的暴露面。**如果分类 3 只有个位数，可以放心走 B 路（显式/推），彻底避开 ambient；如果分类 3 有几十处，就必须上 scoped guard，也就走回了 A 路。**

### 建议执行顺序

1. 完成 343 处的三分分类（1 天，纯读代码）
2. 若分类 3 ≤ 10：按 B 路改造（显式 + id），不引入任何线程级 ambient
3. 若分类 3 > 10：先做点查路径的单进程原型（4 处显式传版本）验证"共享进程跑 ns>1 SQL"这件事本身可行，再决定是否上 scoped guard
4. **在此之前不要继续投 IPC 优化**（帧合并 / shm 环 / busy-poll）：一旦读路径本地化，570µs 的跨界开销连同整个传输层一起消失，那些改动会全部作废

## 8. 更正：ambient 分支作废，以及"ns1 纳入编码"的影响面

### 8.1 三个结论的更正

| 原表述 | 更正 |
|---|---|
| "存储层本来就是单进程多 namespace 的" | 存储层是 **ns-blind** 的；ns 由 id 编码承载、由翻译层解释 |
| "ns 从 ambient 来还是从 id 来"是二选一 | **ambient 是违例**，不是选项。ns 只能以数据形式进入 |
| 给存储层引 `NsTabletID` 强类型 | **方向反了**：存储层类型名带 ns = 存储层感知 ns。存储层继续用 `ObTabletID` |

强类型本身不算错，但**用错层**：只有翻译层的边界函数可以带 ns 名字（`NamespaceForkKernelPrototype::storage_object_id` 已经是对的）。

### 8.2 vanilla 残留（可独立清理，零风险）

五个开关已全部恒定化：

```cpp
// namespace_fork_kernel_prototype.cpp:1189-1193
enabled() / namespace_mode() / lifetime_mode() / lineage_mode() / metadata_gc_mode()  → 全部 return true;
```

| 开关 | 恒真调用点 |
|---|---|
| `namespace_mode()` | 45 |
| `lifetime_mode()` | 17 |
| `enabled()` | 16 |
| `lineage_mode()` | 10 |
| `metadata_gc_mode()` | 5 |
| **合计** | **93** |

这些分支永远走成立那一侧，机械删除即可。

### 8.3 `encode(1,t)=t` 恒等支撑的是什么：**零物化继承**

```cpp
// namespace_fork_kernel_prototype.cpp:508-531
uint64_t cur = ns;
for (depth...) {
  namespace_chain_link(sql, cur, parent, fork_cap);   // 终止条件：parent == 0（ns1 行 parent_namespace=0，数据决定）
  cap = cap_min(cap, fork_cap);
  const uint64_t candidate = encoded(parent, local);  // parent==1 时 candidate == local
  if (probe_physical_tablet(candidate, exists) && exists) { physical = candidate; found = true; }
  else { cur = parent; }
}
```

- ns>1 的候选是编码 id → 探测该 ns 自己物化的 tablet
- 走到 `parent == 1` 时，`encoded(1, local) == local` → **直接命中 main 原有的物理对象**

所以继承的 tablet 不需要为子 ns 物化任何东西：链一走到 main 就指向 main 的对象。**这个支点就是恒等。**

注意区分：链的**终止**由数据决定（ns1 在 `namespaces` 里有行且 `parent_namespace=0`，kernel:2097）；恒等管的是**命中**。

### 8.4 依赖清单（8 处）

| # | 位置 | 依赖形态 | ns1 也编码之后 |
|---|---|---|---|
| 1 | `NamespaceObjectKey::storage_id()` h:30 | 三元定义本身 | 去掉三元（1 行） |
| 2 | `resolve_inherited_tablet` :520 | **零物化继承的支点** | **链尾显式解码一次**（唯一真正的功能改动） |
| 3 | `encode_object` :1572 | 输入 db id 未编码时原样返回 | 契约改为"输入必须编码"；3 个 DDL 调用点须持有编码 db id |
| 4 | `local_object_id` :1550 | 容忍未编码输入、直接透传 | 去掉容忍（或降为 assert） |
| 5 | `check_table_access` :1418 | `未编码 ⇒ ns1` | 简化为 `database_of(x)`；`if (id==1)` 逻辑不变 |
| 6 | `protect_snapshot_tablets` :1513 | 同上 | 同上 |
| 7 | `schema_by_id` / `list_schemas` :3113 / :3170 | 同上 | 同上 |
| 8 | `is_encoded_id` 语义 | "属于 ns≠1" | 变成"这是对外表示" |

除 #2、#3 外，其余 6 处都是**简化**（删判断），不是改造。

### 8.5 关键：不需要给 ns1 建编码副本，持久化格式也不变

把"对外表示"和"内部存储"分开：

| 面 | ns1 的 id 表示 |
|---|---|
| 对外（SQL 层 / schema guard / DDL 参数 / 翻译层接口） | **一律编码**，包括 ns1 |
| 内部（tablet manager / memtable / sstable / 引擎 key） | 保持原样，ns1 对象继续在原生 id 上 |
| 两者之间 | **一个显式解码点**，替代今天靠恒等隐式完成的那件事 |

持久化格式实测**不变**：

```
exceptions(namespace_id, tablet_id, table_id, kind, drop_scn)
load_exceptions: SELECT tablet_id,table_id,kind FROM exceptions WHERE namespace_id=%lu
调用方索引:      exception_owned(database_of(id), local_of(id))
```

存的是 **local id + 显式 `namespace_id` 列**，不是编码 id；`catalog`/`roots`/`pages` 存的是 page ref。所以：

> **"ns1 纳入编码"只改对外的 id 表示，不改任何持久化格式。**

这显著降低了 TODO #4 里"风险最高"的成色——真正会动元数据格式的是同一条 TODO 的另外两项（directory 例外表化、catalog 树退役）。

### 8.6 风险集中点

| 风险点 | 内容 |
|---|---|
| #2 链尾解码 | 必须保证所有沿链探测路径走到同一处解码：`resolve_read_tablet`(:3306)、`ensure_tablet_impl`(:3431)、`protect_snapshot_tablets`(:1513 区)、:2833、:3367 |
| #3 `encode_object` 契约 | 三个 DDL 调用点必须持有编码 db id：`ob_table_helper.cpp:572,583`、`ob_ddl_service.cpp:737`；否则 ns1 的 DDL 仍建原生 tablet |
| #5~#7 的 `: 1` 兜底能否删 | 取决于"对外接口是否真的只有编码 id"这条不变式；若仍有内部路径递原生 id，兜底是承重的 |

### 8.7 修正后的执行顺序

1. 删 93 处恒真开关 —— 纯机械，零风险
2. "对外表示统一"（ns1 也编码）—— 改动集中在 `encoded()` / `local_object_id` / `encode_object` 三个函数的契约 + 一个链尾解码点，**不动持久化格式**
3. 做完 2 之后：schema guard 那 19 处判断可删，`is_encoded_id` 的判断点从 38 处收敛到边界
4. 最后才是 TODO #4 的另外两项（directory 例外表化 + catalog 树退役）——那两项才真正动元数据格式

## 9. 附：本次审计的证据来源

```
resolve_read_tablet / check_table_access / ensure_tablet_impl / database_of / local_of
    namespace_fork_kernel_prototype.cpp:305,306,1409,1418,3276,3316
ob_access_service.cpp:582-651
ob_multi_version_schema_service.h:158-160,208,216
ob_multi_version_schema_service.cpp:664,1724-1729,2380-2381
obmp_query.cpp:104,502-540,596,814-850
ob_sql.cpp:1115,1222-1240,1856-1861,2177,2725
ob_result_set.cpp:64,77,1236
ob_plan_cache_struct.h:56-139    ob_plan_cache.cpp:1985-1997
ob_server.h:446,566,729
ob_sql_session_info.h:415,1032   ob_sql_session_mgr.cpp:213,249
namespace_fork_kernel_prototype.cpp:217-248（ControlSqlNamespaceScope）
```

## 10. 2026-09-22 实测复核：table_id 编码没有活的靶子（结论：不改）

在动手改 `table_id` 编码之前做了一轮实测，结论是**这个改造没有活的靶子，应该停**。

### 10.1 已落地并回归通过的一步：标记位单点化

标记位 `1ULL << 62` 此前在 kernel 外被手写了 5 次。现新增两个唯一入口：

```cpp
static uint64_t encode_id(uint64_t namespace_id, uint64_t local_id);  // NamespaceObjectKey{ns,local}.storage_id()
static uint64_t namespace_of(uint64_t id);   // is_encoded_id(id) ? database_of(id) : 1
```

替换掉 kernel 外的全部手写位运算：

```
ob_schema_getter_guard.cpp:1048,1082,1133,1339   ← 4 处手写"补编码"
namespace_worker_gateway_prototype.ipp:522       ← 1 处手写"解码"
```

现在全仓只有 `ID_MARK` 定义与 `NamespaceObjectKey::storage_id()` 提到标记位。改动 4 文件 +28/-13，
门禁四件套 PASS：bootstrap `y27heweo` / sql_worker `l5djxvzu` / direct full `cy774wct` / direct tls `z7msaj2h`。

### 10.2 `bind_shared_inner_sql_namespace` 是死代码

```
$ grep -rn "bind_shared_inner_sql_namespace" src/
...h:203   声明
...ipp:35  定义
（无调用者）
```

门禁日志佐证：`PROTOTYPE_NATIVE_NAMESPACE_BIND = 0`，619 条 `PROTOTYPE_NATIVE_NAMESPACE_RESOLVE` 全是
`trace=1 ns=1 found=0`。即"共享进程在收发阶段拼 namespace"这个机制**只有 API 壳子，没接线**。

推论：ns 的传输载体**就是编码 id 本身**——它从 worker 一路带到共享进程，每层解一次。不是缺口，是设计。

### 10.3 `ObSchemaGetterGuard` 的路由分支从不触发

`worker_namespace` 只在 worker 进程被赋值（`namespace_sql_worker_prototype.ipp:62`），共享进程恒为 0。代入
`owns_namespace_schema() = worker_process && worker_namespace != 0`：

| 分支 | 条件 | 判定 |
|---|---|---|
| 第一支（"补编码"） | `!owns && worker_namespace > 1 && !is_encoded_id(X)` | 共享进程 `worker_namespace > 1` 恒 false；worker 里 `owns` 为 true → **证明为死代码** |
| 第二支（"按编码 id 路由 fork catalog"） | `!owns && namespace_mode() && is_encoded_id(X)` | 只在共享进程可达 |

第二支加正向计数后跑门禁：

| 套件 | `guard` 路由到 fork catalog | `catalog()` 帧路径 |
|---|---|---|
| bootstrap | **0** | 0 |
| sql_worker full | **0** | 2 |
| direct full | **0** | 1 |
| direct tls | **0** | 0 |

**共享进程真正查 schema 的路径是 `catalog()` 帧处理（`gateway:502` 起），用帧里带来的编码 id，不经过 guard。**

### 10.4 结论与对前面计划的更正

| 原计划 | 更正 |
|---|---|
| "去掉 table_id 编码，ns 改由 guard 携带 / 句柄显式传"（估 200–400 行） | **不做**。没有活的靶子 |
| "接线 trace 绑定让 ns 从上下文来" | 不做。它是为上面那个改造服务的 |
| 给存储层引 `NsTabletID` 强类型 | 不做。名字带 ns 就是让存储层感知 ns，方向反了 |
| 标记位单点化 | **已做**，门禁 PASS |

语义上"table_id 不该带 ns"成立；但在这份代码里，编码 id 只活在**传输层 + kernel 边界函数 + 进程级缓存 key**
里，从未漏到业务模块。因此现状无需改动。

若将来真要让 schema 对象脱离编码 id，前置条件是先决定 ns 的替代传输方式（显式句柄参数 优于
线程上下文），而**当前没有任何调用路径要求这么做**。

## 11. 合并为单进程后的目标状态（验收标准）

不列"输入约束"，而是描述**合并完成后系统应该处于什么状态**。每条都是可检查的断言。

### 11.0 一句话

> 一个进程 = 一个完整数据库实例，进程内同时承载 N 个 namespace；
> namespace 的差异只体现在**数据**（id / 版本 / 句柄 / 各自的元数据键空间）上，
> 不体现在进程、线程、模块签名或任何需要维护的上下文里。

### 11.1 身份与上下文状态

| 断言 | 检查方式 |
|---|---|
| 不存在"一 namespace 一进程"的结构 | 系统中无 per-ns 子进程、无 spawn、无 endpoint 注册表 |
| 不存在 per-ns 线程/线程池 | 只有一套 OMT 池，所有 ns 共用 |
| **不存在任何可变 ns 上下文**：无 thread_local ns、无 scoped guard 作为唯一防线 | 全仓搜不到"进请求设置 / 出请求恢复"式的 ns 保存恢复代码 |
| ns 只有三种载体，且都是数据：编码 id、显式句柄/参数、该 ns 自己的元数据键空间 | 新增代码若需要 ns，只能从这三处取 |
| 模块无法"忘记"ns——因为它不需要知道 ns | 除翻译层外，模块签名里不出现 ns 参数 |

### 11.2 进程与角色状态

| 断言 |
|---|
| `worker_process` / `worker_namespace` 这类**进程角色开关不存在** |
| 不存在 IPC 通道：无帧协议、无 `RequestRoutes`/`PendingRequest`、无 credit、无管道读写线程 |
| 不存在 proxy 转发（公共 MySQL 端口由本进程直接服务） |
| 不存在 worker 激活 / 换代 / endpoint 发布 / `generation` 重建这套机制 |
| 只有一套 schema 缓存、plan cache、存储引擎（ns 维度体现在 id 与版本上） |

### 11.3 fork 状态（不得退化）

| 断言 | 当前基线 |
|---|---|
| `FORK` 语句仍是 **O(1)**：耗时与对象数无关 | 0.29s（pages 0 行） |
| **fork 后没有"激活"阶段**：不需要 spawn、不需要 schema 全量刷新、不需要 endpoint 发布 | 现在 1.19s = fork 语句 0.29 + spawn/init 0.2 + refresh 0.5 + 首查询懒加载 0.25 |
| 因此 **fork→可服务 ≈ fork 语句耗时** | 目标：≤ 0.3s 量级 |
| COW 语义完整保留：cap 钳制、沿父链探测、例外表（owned/tombstone）、DROP 保护 | 已有 |
| 读写隔离语义不变：子看不到父 fork 后的写；子写后物化且对父不可见；多级链 cap 累积 | 已有 |
| fork 延迟不随 namespace 总数线性增长 | 已有（懒激活），合并后进一步消除 |

### 11.4 性能状态

| 断言 | 参考 |
|---|---|
| 点查延迟与吞吐达到**单进程单体水平** | 0.13ms / 8t 53231 QPS |
| 热路径跨界次数 = **0**（因为不存在边界） | 现状 6–8 次 |
| **不靠常驻自旋核换延迟** | 不需要 busy-poll、不需要 isolcpus |
| `FORK` 与查询延迟都不随 namespace 数量退化 | — |

### 11.5 代码状态

| 删除 | 约 |
|---|---|
| 传输/控制层：`gateway` / `multiplex` / `protocol` / `proxy` / `sql_worker` 主循环 | ~2940 行 |
| 15 个 `Remote*` 适配器（换成直接调用；native 实现本来就在） | — |
| 进程角色开关及其分支 | 当前 93 处恒真判断 |

| 保留 | 理由 |
|---|---|
| `NamespaceForkKernelPrototype`（编码、链探测、例外表、cap） | 这是 fork 的实质，与进程模型无关 |
| 端口/适配器接口（`ObITransactionService` / `ObITabletScan` / `ObIDmlService` / `ObILobReadService` / `ObIRangeService` / `ObIPrivMgr`） | **可逆性**：将来若需要隔离，仍可在工厂层换回 `Remote*` |

### 11.6 每 namespace 的状态边界

| 必须 per-ns | 明确不 per-ns |
|---|---|
| schema 版本（`roots.schema_version`） | 内存配额 |
| plan cache（key 带 ns，或 per-ns 实例） | CPU 配额 / 线程池 |
| session 归属（已有 `namespace_storage_binding_`） | 故障域 |
| 事务/快照（已由 session 携带） | 存储引擎（完全共享） |

### 11.7 明确不在目标状态内

- 不做多租户**故障隔离**（一个 ns 出错不影响其他）
- 不做多租户**资源隔离**（per-ns 内存/CPU 配额）
- 不支持 **vanilla 单 namespace 模式**

### 11.8 唯一未定的实现选择（不影响上述状态）

点查达成"零跨界"有两种实现，**目标状态相同，只是实现不同**：

1. **进程内直读**：SQL 与存储同进程，读路径就是普通函数调用
2. **共享内存直读**（仅当 D1 选择"worker 持只读副本"时才相关）：把只读数据 mmap 给 SQL 侧

合并方案下自然落到 (1)，(2) 只有在"保留进程拆分但去掉 IPC"的备选路线上才需要。
状态 11.4 只要求结果，不规定实现。
