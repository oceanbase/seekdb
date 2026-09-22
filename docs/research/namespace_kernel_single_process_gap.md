# 单进程化的 kernel 落位差距分析（决策支持）

状态：分析结论，**未改任何代码**。基准：`HEAD = ce16cca2f`，工作区仅 `.scratch/` 未跟踪。
方法：所有 `path:line` 均为本次实读；凡推断而非实证者标 **推断**。

上游文档：`namespace_single_process_plan.md`（Phase 3 落位）、`namespace_single_process_audit.md`（§8.4/§8.6）、`namespace_phase1a_skeleton.md`（已落地部分）、ADR-0001/0002/0003。

---

## 0. 摘要（先看这一节）

| 判断 | 结论 |
|---|---|
| kernel 规模 | `namespace_fork_kernel_prototype.h` 153 行 / `.cpp` 3494 行；**56 个静态方法**（53 public + 3 private） |
| 落位是"机械搬迁"吗 | **不是**。kernel 的 namespace 身份来自 ambient 状态，且该状态的载体本身要被 Phase 3 删除 |
| 身份解析 | `current_namespace_id()` → `resolve_shared_inner_sql_namespace()`（kernel:1484-1485），实现落在 `namespace_worker_gateway_prototype.ipp:65-80`（Phase 3 删除），依赖 `thread_local` + trace 全局表 |
| 受影响的 kernel 函数 | **24 个**函数内含 `ControlSqlNamespaceScope`/`ExplicitSqlNamespaceScope`（共 31 + 5 处），即 24 处需要把 ns 改成显式参数 |
| 存活文件里的耦合 | kernel 与 worker 全局被 **48 个 kernel 之外的文件**引用；其中 14 个 worker 原型文件（Phase 3 删除）占 70 处，**存活 `.cpp/.h` 里仍有 270 处** |
| fork 与 registry 的关系 | **完全断开**：`control_namespace()` 只写 `__fork_proto_meta.namespaces`（kernel:2034-2042），从不调用 `NamespaceRegistry::register_namespace`；registry 的唯一真实调用者是我的探针（`ob_server.cpp:2537/2573`） |
| forked ns 的 schema 权威 | **不是** per-ns `ObMultiVersionSchemaService`。forked ns 的 schema 由 kernel catalog 覆盖层直接服务（`namespace_worker_gateway_prototype.ipp:544-568`），**当前没有任何代码为 fork 出的 ns 实例化 schema service** |
| `FORK NAMESPACE` 语法 | **不存在**。parser 只有 `FORK TABLE` / `FORK DATABASE`（`sql_parser_mysql_mode.y:4400/4407`） |
| 真正的可行性闸口 | **Step 1（身份显式化）**：24 个 kernel 函数 + 4 处外部 ambient 消费者必须都能拿到显式 ns，否则 ADR-0003 在 kernel 上不成立 |
| 三个最高风险 | ① 身份显式化漏一处 = 跨 ns 静默读错（无编译期保护）；② 控制元数据表在单进程内的读写语义（`ensure_control_schema` 与 worker 对账纠缠）；③ forked ns 的 schema 权威路线未定（覆盖层 vs actualize 出真的 schema service） |

**一句话**：kernel 的存储机制（编码 id + COW 链探测 + 例外表）基本是 ns-blind、可搬迁的，**约 60% 行数属于此类**；真正的差距集中在"身份从哪来"和"控制面谁执行"这两处，而它们恰好都寄生在 Phase 3 要删的文件里。

> **父代理独立复核（2026-09-22，本节数字以复核为准）**
>
> 已验证为真的关键结论：
> - `uses_remote_schema()`（`namespace_worker_protocol_prototype.h:196`）确实是 `worker_process && worker_namespace != 0 && !owns_namespace_schema()`，而 `owns_namespace_schema()`（`:192`）正是前两个合取项 → **恒为 false**，其调用点、`worker_request_schema_version` thread_local 与 `fetch_schema_version` 属**当下即死代码**。
> - parser 里**没有 `FORK NAMESPACE`**：`sql_parser_mysql_mode.y:4400/4407` 只有 `FORK TABLE` / `FORK DATABASE`。
> - kernel 与 `NamespaceRegistry` **完全断开**：kernel 与 7 个 worker `.ipp` 里都搜不到 `register_namespace`/`NamespaceRegistry`。
> - 共享进程 SQL 确实被 `check_sql_execution_role()` 拒绝：`ob_sql.cpp:1063/1303/2961`。
>
> **需修正的数字**：§0/§2 的"48 个文件、270 处"在任一单一口径下都复现不出来。复核实测（排除 kernel 自身两文件）：
>
> | 口径 | 文件数 | 引用数 |
> |---|---:|---:|
> | kernel 类名，仅存活 `.cpp/.h` | **17** | **98** |
> | kernel 类名，worker `.ipp`（Phase 3 删） | **7** | **58** |
> | kernel ∨ worker 全局，存活 `.cpp/.h` | 44 | 284 |
> | kernel ∨ worker 全局，含 worker `.ipp` | 55 | 365 |
>
> 结论不变且更强：worker `.ipp` 只占约 20%，**真正的工作量在存活文件里**。§2.2 的分文件数字是按更宽口径统计的，逐项核对时以本表口径为准。§1/§3/§4 的 `path:line` 与机制描述复核未发现冲突。


---

## 1. Kernel 资产盘点

`NamespaceForkKernelPrototype` 56 个静态方法按职责分四组（`.h` 行号 / `.cpp` 定义行号）。

### (a) ns-blind 存储机制（只吃编码 id）—— 可搬迁，机械工作

| 方法 | path:line | 契约 |
|---|---|---|
| `resolve_read_tablet` | `.cpp:3193` | 读路径绑定解析：本地已物化→自身；未物化→沿父链探测并给 `cap_scn`；未编码 id 直接透传 |
| `ensure_tablet_impl` | `.cpp:3233`（`ensure_tablet` 包装 `.cpp:3190/3227`） | 写路径（DDL fork）沿链物化 tablet |
| `check_table_access` | `.cpp:1341` | 准入：按 `database_of(tablet)` 取 ns，持访问租约 |
| `check_baseline_access` | `.cpp:1300` | fork 基线访问租约（另一把） |
| `release_access` / `drain_access` | `.cpp:1326` / `.cpp:1329` | 释放租约 / 排空 |
| `protect_snapshot_tablets` | `.cpp:1379` | 加租约保护快照依赖，`need_retry` 用于重试 |
| `schedule_baseline` | `.cpp:3146` | tablet 空壳/未完成 fork 时调度基线补齐 |
| `is_tablet_owned` / `owned_storage_tablets` | `.cpp:2920` / `.cpp:2941` | 按 ns + local id 判/滤 tablet 所有权 |

**关键观察**：这一组的 ns 全部来自入参 tablet id（`database_of(id)`），**不读 ambient 身份**——与审计 §1 的实测一致。它们的迁移成本接近 0。

### (b) 控制面 / 元数据（要执行 SQL）—— 差距主体

| 方法 | path:line | 契约 |
|---|---|---|
| `ensure_control_schema` | `.cpp:1166` | 建 `__fork_proto_meta` 库与 6 张表（DDL 见 `.cpp:1171-1193`）；走 `GCTX.sql_proxy_` |
| `control_namespace` | `.cpp:1992` | **fork/drop 主入口**：加源 ns 锁 → 写 `namespaces` 行 → 取存储快照 → 写 snapshot pin |
| `roots` / `save_roots` | `.cpp:692` / `.cpp:735` | 读/写某 ns 的 `roots` 行（catalog/directory 根 + snapshot + schema_version + 栅栏列） |
| `load_exceptions` / `exception_owned` / `exception_tombstoned` / `apply_exception_owned` | `.cpp:431/461/470/476` | 例外表：`(ns, local tablet) → owned/tombstone`，进程内 `exception_sets` 缓存 |
| `capture` | `.cpp:3009` | 从源 ns 复制 root 引用为子 ns 的快照根（`cap_min(snapshot)`） |
| `database_by_id` / `database_in_namespace` / `database_by_address` | `.cpp:1922/1967` / `.cpp:1909` / `.cpp:1901` | ns 作用域的库 schema 查询（读 catalog 覆盖层） |
| `observe_schema(_in_namespace)` / `forget_schema(_in_namespace)` | `.cpp:2124/2136` / `.cpp:2448/2463` | DDL 目录增删（含批量前后 schema 差分） |
| `publish_schema_delta` | `.cpp:2788` | DDL 提交后按 tablet 所有权做批量目录替换 |
| `check_ddl` / `check_database_ddl` | `.cpp:3113` / `.cpp:1983` | DDL 栅栏与合法性 |
| `begin/finish_schema_change` / `begin/finish_schema_recovery` | `.cpp:1726/1741/1773/1801` | 持久化 DDL/fork 栅栏（`active_schema_changes`、`pending_schema_version`） |
| `begin/finish/flush_schema_changes` | `.cpp:1834/1855/2407` | 事务级 schema 变更批（按 `ObISQLClient*` 聚合） |
| `namespace_schema_version` | `.cpp:1712` | 读某 ns 的 `roots.schema_version`（供版本栅栏） |
| `is_namespace_address` / `parse_namespace_address` | `.cpp:1869` / `.cpp:1872` | 解析 `ns.db` 形式地址 |

### (c) 翻译助手（唯一合法解释 ns 的地方）

| 符号 | path:line | 契约 |
|---|---|---|
| `NamespaceObjectKey::storage_id` | `.h:24-32` | `ns==1 ? local : (1<<62)|(ns<<32)|local`（ns1 恒等） |
| `is_encoded_id` | `.cpp:1475` | 是否带 `1<<62` 标记 |
| `encode_id` / `namespace_of` | `.cpp:1478` / `.cpp:1481` | 编码 / 反解 owner ns |
| `current_namespace_id` | `.cpp:1484` | **读 ambient 身份**（见 §3.2） |
| `local_object_id` / `storage_object_id` | `.cpp:1487` / `.cpp:1497` | 编码↔local 互转，带 ns 校验 |
| `encode_object` | `.cpp:1509` | DDL 建对象时编码 db/table id |
| `make_namespace_schema` / `make_storage_schema` | `.cpp:1513` / `.cpp:1618` | 存储 schema ↔ ns 逻辑 schema 互转（id 重写） |
| `table_id_for_tablet` | `.cpp:3064` | 由 tablet 反查 table（继承目录无 `__all_table_history` 行） |
| `schema_by_name` / `schema_by_id` / `list_schemas` | `.cpp:3023` / `.cpp:3049` / `.cpp:3087` | catalog 覆盖层的 schema 读取三件套 |

### (d) 血缘 / 快照 / GC

| 方法 | path:line | 契约 |
|---|---|---|
| `resolve_inherited_tablet` | `.cpp:505` | 沿父链探测物理 tablet，返回 `cap_scn`；`encode(1,t)=t` 使链尾直接命中 ns1 原生对象 |
| `snapshot_roots` / `release_lineage` / `remember_chain_link` | `.cpp:713` / `.cpp:1029` / `.cpp:385` | 快照行读 / 释放血缘 / 链缓存 |
| `begin/lock/finish_namespace_drop` | `.cpp:1212/1256/1281` | drop 三阶段（登记、锁+取绑定 tablet、完成） |
| `release_namespace_schemas` | `.cpp:2976` | 释放 ns 的 schema holder |
| `collect_metadata` | `.cpp:1057` | `__gc__` 元数据可达性扫描与回收 |
| `NamespaceSourceDropGuard` | `.h:11-21` / `.cpp:1202` | 一次 drop 事务的能力标记（非"当前 ns"） |

---

## 2. 当前接线：谁在用 kernel / worker 全局

统计口径：`grep -rn 'NamespaceForkKernelPrototype\|namespace_worker_prototype::'`，排除 kernel 自身两文件。

| 类别 | 文件数 | 引用数 | 代表作 |
|---|---:|---:|---|
| **(i) worker 进程路径，Phase 3 整体删除** | 14 | 70 | `namespace_worker_gateway_prototype.ipp`(22)、`namespace_sql_worker_prototype.ipp`(14)、`namespace_worker_write_prototype.ipp`(13)、`namespace_worker_scan_prototype.ipp`(11)、`..._direct_insert...ipp`(5)、`..._range...ipp`(4)、`..._commands...ipp`(1) |
| **(ii) 共享进程且单进程下依然有效** | 20 | ~150 | `ob_schema_getter_guard.cpp`(80)、`ob_latest_schema_guard.cpp`(10)、`ob_access_service.cpp`(6)、`ob_tablet_fork_task.cpp`(5)、`ob_table_sql_service.cpp`(8)、`ob_database_sql_service.cpp`(3)、`ob_drop_table_helper.cpp`(12)、`ob_table_helper.cpp`(2)、`ob_tablet_scheduler.cpp`(1)、`ob_empty_shell_task.cpp`(1)、`ob_drop_table_resolver.cpp`(1)、`ob_sql_utils.cpp`(1)、`ob_multi_version_schema_service.cpp`(17)、`ob_server_schema_service.cpp`(8)、`ob_sql_session_info.{h,cpp}`(3) |
| **(iii) 共享进程但依赖 IPC / worker 生命周期** | 14 | ~120 | `ob_inner_sql_connection.cpp`(32，`inner_call`/`inner_read`/`close_session`)、`ob_fork_database_service.cpp`(13，`deactivate_namespace`/`drain_storage_namespace_access`/`reload_storage_freeze_info`)、`ob_system_package_load_task.cpp`(6，`reconcile_namespace_workers`/`broadcast_system_package_ready`)、`ob_ddl_service.cpp`(10，`sync_namespace_schema_delta`)、`obmp_base.cpp`(5，`begin_direct_request`)、`ob_server.cpp`(4，`proxy`/`stop_all`)、`obmp_connect.cpp`(7)、`ob_local_management_service.cpp`(2，`admin_set_config`)、`obmp_packet_sender.cpp`(2)、`obmp_disconnect.cpp`(2)、`ob_dml_cg_service.cpp`(4，`fetch_schema_version`)、`ob_sql.cpp`(4，`check_sql_execution_role`)、`ob_expr_sys_privilege_check.cpp`(2)、`ob_variable_set_executor.cpp`(2)、`ob_ddl_executor_util.cpp`(2) |

分类边界说明（重要）：

- (ii) 与 (iii) 的差别不是文件，而是**同一文件内的调用族**。例如 `ob_fork_database_service.cpp` 既直接调 kernel（`.cpp:46/58/88/100/150`，属 ii），又调 `reload_storage_freeze_info`/`drain_storage_namespace_access` 这类存储 RPC（属 iii）。所以"删 14 个 .ipp"远不足以完成 Phase 3：**存活文件里有 270 处引用需要逐处判定归属**。
- (i) 的 14 个 `.ipp` 里也含 kernel 的**服务端处理器**（`gateway.ipp:500-568` 就是 forked ns 的 schema 读取实现），删除它们等于删除 forked ns 的 schema 服务实现本身。

---

## 3. 差距逐项

### 3.1 控制元数据表（`__fork_proto_meta.*`）

| 项 | 现状 | 缺口 | 规模 | 风险 |
|---|---|---|---|---|
| 表定义 | 6 张：`pages`/`roots`/`namespaces`/`endpoints`/`exceptions`/`snapshots`，DDL 在 `kernel.cpp:1171-1193` | 无（表结构可留） | — | — |
| 建表时机 | `ensure_control_schema()` 由 `ob_system_package_load_task.cpp:104`/`:123` 在**共享进程**调用 | 建表后紧跟 `reconcile_namespace_workers()`（同文件 `:105`/`:124`）——单进程下该函数语义消失 | 有界 | 低 |
| 读写方式 | 一律 `GCTX.sql_proxy_` + `ControlSqlNamespaceScope`（如 `roots` `.cpp:692`、`load_exceptions` `.cpp:431`、`control_namespace` `.cpp:2004`） | 单进程下 `GCTX.sql_proxy_` 就是本地 proxy，**机制可用**；但"选控制 ns"靠 ambient（§3.2） | 有界 | 中 |
| 写入者 | 今天：worker 执行、共享进程只做建表与 drop 排空（`ob_fork_database_service.cpp:150` 在共享进程调 `control_namespace`）| 单进程下写入者就是本地进程，需确认 `check_sql_execution_role()`（`ob_sql.cpp:1063/1303/2961`）不再拒绝共享进程 SQL | 有界 | **高**（见 §3.3） |
| DDL/fork 栅栏 | `active_schema_changes`/`pending_schema_version` 列（`roots` 结构 `kernel.cpp:124-133`） | 无 | — | — |

**结论**：控制元数据本身**不需要改表结构**，需要改的是"谁执行"和"ns 从哪来"。

### 3.2 `ControlSqlNamespaceScope` / `push_inner_sql_namespace_override`（ADR-0003 违规点）

现状（三处实证）：

1. `thread_local std::vector<uint64_t> inner_sql_namespace_overrides` 定义在 **`namespace_worker_gateway_prototype.ipp:34`**（Phase 3 删除的文件）。
2. `push/pop_inner_sql_namespace_override` 定义在同文件 `:57-64`，仅声明在 `namespace_worker_protocol_prototype.h:205-206`。
3. `resolve_shared_inner_sql_namespace()`（同文件 `:65-80`）按四级优先级取 ns：`worker_namespace` → thread_local 栈顶 → trace 绑定的全局表 `shared_inner_sql_namespaces`（`:32-33`）→ 默认 1。`current_namespace_id()` 直接返回它（`kernel.cpp:1484-1485`）。

用量：kernel 内 `ControlSqlNamespaceScope` 31 处、`ExplicitSqlNamespaceScope` 5 处；**分布在 24 个函数**（见 §1 分组，含 `resolve_read_tablet`、`ensure_tablet_impl`、`roots` 路径、`observe_schema*`、`control_namespace`、`publish_schema_delta` 等）。

外部 ambient 消费者（存活文件，必须一起改）：

| 位置 | 现状 | 可行的显式载体 |
|---|---|---|
| `ob_drop_table_helper.cpp:62` | `namespace_schema_for()` 内取 `resolve_shared_inner_sql_namespace()` | 函数已有 `storage_schema`（编码 id）→ `database_of(table_id)`；**推断**该 id 在此时已编码（同文件 `:1291` 另算 `current_namespace_id()`） |
| `ob_drop_table_helper.cpp:75` | `namespace_metadata_object_id()` 同上 | 入参 `storage_id` 即编码 id → `database_of` |
| `ob_table_sql_service.cpp:704` | 写 `__all_table_*` 前取 ns | `storage_schema` 编码 id → `database_of`（`make_namespace_schema` 的入参证明其 id 是存储态，`kernel.cpp:1513-1526`） |
| `ob_table_sql_service.cpp:2203` | 批量同上 | 同上 |

**替代方案**（按 ADR-0003 的要求）：kernel 的 24 个函数加显式 `uint64_t namespace_id` 形参；`ControlSqlNamespaceScope`/`ExplicitSqlNamespaceScope` 两个类整体删除；`push/pop/bind/unbind/resolve_shared_inner_sql_namespace` 五个符号随 Phase 3 一起消失；上面 4 处外部点改为从编码 id 推出或由调用链传入。

**规模**：24 个 kernel 函数 + 4 处外部点；**有界但面广**。
**风险（最高）**：漏改一处不会编译错——`current_namespace_id()` 会返回默认 1，表现为**跨 ns 静默读错数据**。这是本项唯一没有编译期保护的地方，也是 ADR-0003 存在的理由。

### 3.3 `FORK NAMESPACE` → `NamespaceRegistry`

现状：

- 语句层：**没有 `FORK NAMESPACE`**。parser 只有 `FORK TABLE`（`sql_parser_mysql_mode.y:4400`）与 `FORK DATABASE`（`:4407`）；resolver 为 `ob_fork_database_resolver.cpp`。
- 内核入口：`control_namespace(source, target, id)`（`kernel.cpp:1992`），调用者 `ob_fork_database_service.cpp:150`（`fork_database_arg.src/dst_database_name_`）。
- **它只写 SQL**：`INSERT INTO __fork_proto_meta.namespaces(...)`（`kernel.cpp:2034-2042`），随后 `namespace_named()` 回读 id（`:2042`）。
- 它**从不**调用 `NamespaceRegistry::register_namespace`；全仓 `register_namespace` 的真实调用者只有 `ob_server.cpp:2537`（系统 ns）与 `ob_server.cpp:2573`（我的探针）。
- 它还依赖 `acquire_storage_snapshot()`（`kernel.cpp:2048`），定义在 `namespace_worker_gateway_prototype.ipp:93-98`（Phase 3 删除）。该函数体只是 `ObDDLTaskUtil::calc_snapshot_with_gts`，可搬迁。

缺什么才能"一条 FORK 语句建出 ns + runtime"：

1. **调用点**：需要一个能拿到 `NamespaceRegistry&` 的服务层（`OBSERVER.get_namespace_registry()` 已存在，`ob_server.h:343`）。
2. **runtime 容器**：`ObServer` 现在只有写死的 runtime 成员（`ob_server.h:479-489`：`namespace_registry_` / `namespace_system_runtime_` / `namespace_worker_runtime_` / `namespace_fork_runtime_`）。多 ns 需要 `std::map<ns_id, NamespaceRuntime>` 之类的容器，且 runtime 必须**进程生命周期存活**（registry 只存裸指针，见 `namespace_registry_prototype.h:249` 的 `runtimes_.insert(..., &runtime)`）。
3. **懒激活**：`NamespaceRuntime::set_service_inited()`（`namespace_registry_prototype.h:186-187`）已就绪，但**没有任何代码在首次登录时驱动激活**；`bind_login_namespace`（`obmp_connect.cpp:688-741`）只有 `get_runtime` + 绑定，没有激活调用。
4. **语法**：若验收要用 `FORK NAMESPACE ns2 FROM ns1`，需要新语法（见 §5）。

**规模**：容器 + 激活路径是**有界**（百行量级）；新语法是**独立工作面**。
**风险**：registry 存的是裸指针且 probe 已证明"重复注册会二次分配并覆盖指针"（`namespace_phase1a_skeleton.md` §10 记录的 `-11` 崩溃）——容器化时必须一次分配、幂等。

### 3.4 id 编码翻译入口 / 未编码 id 的调用者

现状：

- 唯一编码实现 `NamespaceObjectKey::storage_id()`（`.h:24-32`）与 `encode_id`/`namespace_of`（`.cpp:1478/1481`），`ns==1` 仍走恒等分支（`.h:30`）——即审计 §8.7 提议的"ns1 也编码"**尚未做**。
- `is_encoded_id` 在 `src/share/schema/` 有 **19 处**（`ob_schema_getter_guard.cpp` 15、`ob_latest_schema_guard.cpp` 3、`ob_multi_version_schema_service.cpp` 1）。
- `local_object_id` 容忍未编码输入（`.cpp:1487-1496`：非编码则原样返回）；`namespace_of` 对未编码 id 返回 1（审计 §8.4 #5~#7）。

单进程下依然存在"原始（未编码）id"路径：ns1 的引擎对象保持原生 id（`.h:23` 注释、`.cpp:30`）。所以**不能删掉未编码兜底**，除非先完成审计 §8.7 的"对外表示统一"。

**规模**：保持现状即可工作；"ns1 也编码"是独立重构（审计估 8 处依赖）。
**风险**：低（本项不是落位阻塞）。

### 3.5 `ObSchemaGetterGuard` 的编码 id 路径

现状：`ob_schema_getter_guard.cpp` 用 19 组 `is_encoded_id(...) → kernel::schema_by_*` 分流（代表：`:781-782`、`:1129-1137`、`:1333-1340`、`:2558-2568`、`:4189-4191`），并配合 `owns_namespace_schema()`（19 处）与 `worker_schema_prototype()`（`：106-124`，走 `worker_catalog_fetch` IPC）。

单进程下 needed：

- `owns_namespace_schema()` = `worker_process && worker_namespace != 0`（`protocol.h:192-195`）。单进程 `worker_process=false` → **恒 false**，于是 `!owns_namespace_schema() && is_encoded_id(...)`（如 `:1053`）变成恒真的编码判断——**恰好是想要的行为**。这部分可以不改就工作。
- 但 `worker_schema_prototype()` 一族走 `worker_catalog_fetch`（`protocol.h:141`，worker 启动时赋值于 `namespace_sql_worker_prototype.ipp:260`）。单进程下该指针恒为 `nullptr`，所以**这些函数的调用点必须全部改为直连 kernel**（因为 kernel 与 SQL 同进程，不再需要 IPC 取 schema）。代表调用点：`:724`、`:1288`、`:1345`、`:2786-2795`、`:3125`、`:3253`。

**规模**：约 10 处调用点改为直连 + 删除 `worker_schema_prototype`/`decode_worker_schema_prototype`/`worker_table_schemas_prototype`（`：106-175` 一带）。**有界**。
**风险**：`worker_catalog_fetch` 若被无条件调用而单进程下为 null → 空指针；**推断**这些调用点当前都由 worker 模式门控（`owns_namespace_schema`/`worker_process`），但改造时必须逐个确认。

### 3.6 per-namespace schema 版本 / 刷新

**这是文档与代码偏差最大的一项。**

- `namespace_schema_version()`（`kernel.cpp:1712-1725`）读 `roots.schema_version`，但**唯一调用者是 `namespace_worker_gateway_prototype.ipp:514/547/563/1223/1363`**（Phase 3 删除）。
- forked ns 的 schema 服务实现是 `gateway.ipp:544-568`：直接调 kernel 的 `database_in_namespace`/`database_by_id`/`schema_by_name`/`schema_by_id`/`list_schemas`，并用 `namespace_schema_version` 做版本栅栏（不一致返回 `OB_SCHEMA_EAGAIN`，`:548-549`）。
- **没有任何代码为 fork 出的 ns 实例化 `ObMultiVersionSchemaService`**：`alloc_instance()` 的调用者只有 `ob_server.cpp:2393`（issue 04 探针）与 `:2534/2571`（issue 05 探针）。

因此：

| 说法 | 与代码是否相符 |
|---|---|
| ADR-0002 / plan "Runtime 持有 per-ns `ObMultiVersionSchemaService`" | **对 source/native ns 成立**（`NamespaceRuntime` 能持有实例，`namespace_registry_prototype.h:172-187`）；**对 forked ns 不成立**——代码里 forked ns 的 schema 权威是 kernel catalog 覆盖层，不走 schema service |
| issue 05 "两 ns 各自 schema service 独立推进" | 需要先把 forked ns 的 schema 权威**决定**为覆盖层还是真实例；否则无法验证 |

两条可选路线：

- **路线 A（跟随现状）**：forked ns 的 schema 权威 = kernel catalog 覆盖层。单进程下 sql 层直连 kernel 三件套即可，**不需要 per-ns schema service**；代价是 DDL/刷新走 kernel 的 `observe_schema*`/`publish_schema_delta` 而非原生 schema service，功能面窄（这也解释了为什么原型把 forked ns 的 DDL 都收口到 kernel）。
- **路线 B（按 ADR-0002）**：forked ns 也 actualize 一个 `ObMultiVersionSchemaService`，从 `__all_*` 读自己的 schema。**当前代码里这条路的读侧（`ob_server_schema_service.cpp:1772-1791` 用 `worker_namespace`）和版本侧（`namespace_schema_version`）都还不存在于单进程形态**。

**规模**：路线 A 是**有界**（直连替换 + 版本栅栏内联）；路线 B 是**重新设计**（需要为 fork 物化 `__all_*`，即改变 fork 的 O(1) 语义）。
**风险**：**高**——选错会让 issue 05/06 的验收标准无法定义；且路线 B 与 `kernel.cpp:3015` 的"fork 必须 O(1)（不枚举表）"卖点直接冲突。

---

## 4. 建议落地顺序

依赖序，每步给出可观测的验证方式。

| # | 步骤 | 依赖 | 规模 | 验证方式（现成 / 需新增） |
|---|---|---|---|---|
| **1** | **身份显式化**：kernel 24 个函数加显式 `namespace_id`；删 `ControlSqlNamespaceScope`/`ExplicitSqlNamespaceScope`；4 处外部 ambient 点改为 `database_of(id)` 或调用链传入 | — | 有界、面广 | 编译期无法证明；需**新增**探针：两线程分别用 ns1/ns2 并发跑 `resolve_read_tablet`+`roots`，断言互不串（现探针 `ob_server.cpp` 的 `ns-runtime-group.result` 可扩展） |
| **2** | **回收 Phase-3 消失的符号**：`resolve_shared_inner_sql_namespace`/`push`/`pop` 删除；`acquire_storage_snapshot` 搬去 `storage/ddl`；`reload_storage_freeze_info` 搬去 freeze info mgr；`bind/unbind_shared_inner_sql_namespace`、`check_sql_execution_role` 删除 | 1 | 有界 | **最强验证**：在**不编译任何 `src/observer/namespace_*.ipp`** 的前提下 `make -j16 seekdb` 通过（编译期证明无残留依赖） |
| **3** | **控制面本地化**：确认本地进程可执行控制元数据 SQL（删 `check_sql_execution_role` 拒绝路径，`ob_sql.cpp:1063/1303/2961`）；`ensure_control_schema` 保持由本地调用 | 2 | 有界 | bootstrap 套件**需要改断言**：它现在要求 `shared_sql_forbidden`（`namespace_worker_bootstrap_prototype.py:61` 断言不出现 `PROTOTYPE_V18_SHARED_SQL_REJECT`），单进程下该不变量作废 |
| **4** | **slot 容器 + 激活路径**：runtime 容器化（替换 `ob_server.h:479-489` 的写死成员）；新增"按 ns 解析→必要时激活→绑定"的单一入口，供 `bind_login_namespace` 调用 | 3 | 有界 | 扩展现探针：断言第二次登录同 ns 复用同一 runtime 指针、不同 ns 得到不同指针 |
| **5** | **FORK 接入 registry**：在 `control_namespace` 成功后（或包一层服务）调用 `register_namespace` | 4 | 有界 | 新增探针：模拟 fork 后 `resolve_name("ns2")` → `get_runtime` 拿到已激活 runtime（现探针已验证前半段） |
| **6** | **forked ns schema 权威选型**（§3.6 路线 A/B） | 5 | A 有界 / B 重设计 | 路线 A：forked ns 建表后 `schema_by_name` 可读、`roots.schema_version` 前进；路线 B：需先补 `__all_*` 物化 |
| **7** | **`FORK NAMESPACE` 语法**（若验收坚持用该语句） | 6 | 独立工作面 | mysqltest：`FORK NAMESPACE ns2 FROM ns1` 语法解析 + 建 ns |
| **8** | **数据隔离端到端 + 性能** | 6,7 | — | 两 ns 并发读写隔离探针；sysbench `oltp_point_select`（issue 06） |

**真正的可行性闸口 = Step 1。** 理由：其余步骤都是"接线 / 选型 / 补语法"，而 Step 1 决定 ADR-0003 在 kernel 上是否成立——如果 24 个函数中有任何一个**拿不到**显式 ns（既无法从编码 id 推出，调用链也没有上下文），就必须保留 ambient 兜底，那么"无 ambient 上下文"这条全局不变式在 kernel 上就破产，整个单进程设计的立论基础（plan.md 不变式 2）需要重新讨论。本次抽查的 4 处外部 ambient 消费者**都**能通过 `database_of(编码 id)` 得到 ns（§3.2），所以 Step 1 **看起来可行**，但我只实证了这 4 处 + 分组依据，24 个 kernel 函数内部的具体载体需逐个确认（见 §6）。

---

## 5. 文档与代码不一致（逐条）

| # | 文档说法 | 代码事实 | 影响 |
|---|---|---|---|
| 1 | `namespace_worker_functional_delivery.md` 把"共享入口薄路由 / worker 只读副本"当作演进方向；`plan.md` 把 `uses_remote_schema` 视为活模式 | **`uses_remote_schema()` 恒为 false**：`protocol.h:196-198` 是 `worker_process && worker_namespace != 0 && !owns_namespace_schema()`，而 `owns_namespace_schema()`（`:192-195`）恰是前两项的合取 → `X && !X` | 其 10 处调用点、`worker_request_schema_version`（4 处，`ob_multi_version_schema_service.cpp:664/1733/2389` 等）、`fetch_schema_version`（`gateway.ipp:1614` 起的 IPC 版本获取）**全部是死代码**。`namespace_phase1a_skeleton.md` §8 说该 thread_local "只服务 worker 远程 schema 路径"——结论（不转换）仍成立，但前提描述不准：那条路径现在就不通。 |
| 2 | `namespace_single_process_audit.md` §8.7 把"catalog 树退役"列为 TODO | `native_namespace_schema_authority()` **硬编码 return true**（`kernel.cpp:205-208`），于是 `persist_legacy_catalog = !native... = false`（`:2290`）——旧 catalog 写入路径已死 | 该项比文档描述的更接近完成；文档应更新 |
| 3 | `CONTEXT.md` / spec / issue 05 以 `FORK NAMESPACE ns2 FROM ns1` 为主路径 | parser 只有 `FORK TABLE`（`sql_parser_mysql_mode.y:4400`）与 `FORK DATABASE`（`:4407`），**无 `FORK NAMESPACE`** | issue 05 的验收标准（"FORK NAMESPACE ns2 FROM ns1 在单进程内完成"）**当前无法执行**；需先加语法（Step 7）或改验收口径 |
| 4 | ADR-0002 / plan §"Runtime 持有"：per-ns `ObMultiVersionSchemaService` 服务各 ns 的 schema | 现状只为 **source/native ns** 提供该能力；**forked ns 的 schema 由 kernel catalog 覆盖层直接服务**（`gateway.ipp:544-568`），无任何 ns-service 实例化（`alloc_instance` 仅探针调用） | 直接决定 issue 05/06 怎么验收（§3.6 路线 A/B） |
| 5 | plan.md Phase 1.4："每语句版本 pin 从 thread_local 改 session 级（~6 处）" | 该 thread_local 的读写全部在 `uses_remote_schema()` 门控下（恒 false） | 应改为"随 Phase 3 一并删除"，不必单独改造 |
| 6 | `namespace_phase1a_skeleton.md` §12 声称 bazel 门禁"已落地" | 门禁 BUILD 已就位，但 `observer_validate_header_inventory` 的清单漂移在 `ce16cca2f` 才修，且本环境 bazel 无法完成分析 | 门禁**未实证**（该文档 §13 已如实标注） |

---

## 6. 诚实未知（需插桩才能回答）

| # | 未知 | 需要插桩/验证的对象 |
|---|---|---|
| 1 | 24 个含 scope guard 的 kernel 函数中，**每个**能否从入参/上下文拿到显式 ns（Step 1 的可行性下限） | 逐函数检查入参；对可疑者（`database_by_address`、`check_ddl`、`schedule_baseline`、`begin_schema_change`、`flush_schema_changes` 等**只吃 name/trans/id 而 ns 需外部给的**）在 `kernel.cpp` 内插桩打印解析出的 ns 并与调用方期望比对 |
| 2 | `ob_drop_table_helper.cpp:62` / `ob_table_sql_service.cpp:704,2203` 处 `storage_schema` 的 id 在**该时刻**是否已编码 | 在上述三行的 `make_namespace_schema/local_object_id` 调用前插桩打印 `is_encoded_id(table_id)`；本次仅由 `make_namespace_schema` 入参语义**推断**为是 |
| 3 | 单进程下 `worker_catalog_fetch==nullptr` 时，`ob_schema_getter_guard.cpp:120/158` 是否被无条件执行 | 在 `worker_schema_prototype` 与 `worker_table_schemas_prototype` 入口插桩打印 `worker_process`/`owns_namespace_schema()`；或在单进程形态下直接跑 `SHOW`/查询触发 |
| 4 | `__fork_proto_meta` 的 6 张表在"本地进程执行 SQL"时是否有权限/可见性闸门（如 `can_access_namespace_control_database()`，`protocol.h:188-191`，`worker_process=false` 时返回 true） | 单进程形态下以普通用户执行 `SELECT * FROM __fork_proto_meta.namespaces`，观察是否被 schema guard 拦（`ob_schema_getter_guard.cpp:120` 一带的 control-db 判定） |
| 5 | 两 ns 并发时 kernel 的进程级锁（`metadata_mutex` `kernel.cpp:96`、`exceptions_mutex` `:429`、`pending_schema_changes_mutex` `:108`）是否成为性能瓶颈 | obperf 采样 + `MetadataReadGuard` 持锁时长插桩；这直接影响 issue 06 的性能门禁能否通过 |
| 6 | `active_schema_transactions`/`deferred_schema_transactions`（`kernel.cpp:109-110`）以 `ObISQLClient*` 为键，在单进程多 ns 并发 DDL 下是否会把不同 ns 的事务误判为同一事务 | 在 `begin/flush_schema_changes`（`:1834/2407`）插桩打印 `(trans 指针, ns)` 并在两 ns 并发 DDL 用例中比对 |
| 7 | `exception_sets` 的 `loaded` 缓存（`kernel.cpp:431-458`）在 fork 之后是否需要失效（父 ns 后续 DDL 是否影响子 ns 的已缓存例外集合） | 在 `apply_exception_owned`/`load_exceptions` 插桩；跑"父 DDL → 子读"顺序用例 |

以上 7 项我**没有**从代码得出结论，列在这里而不是当作已知。

---

## 7. 对决策的直接输入

- **是否开工**：值得开。kernel 的存储机制（§1a）、翻译助手（§1c）、血缘/GC（§1d）合计约占 3494 行的 60%，本质是 ns-blind、可搬迁的；缺口集中在身份（§3.2）与控制面执行者（§3.1/§3.3）两处，且都有明确的显式化方案。
- **开多大**：Step 1–5 是"让 fork 在单进程内跑起来"的最小集，**不含**语法与性能；Step 6 是必须先做的**架构选型**（覆盖层 vs 真 schema service），它会改变 issue 05/06 的验收定义。
- **先做什么**：Step 1 单独成项，且必须配"两线程/两 ns 并发互不串"的探针——它是唯一没有编译期保护的高风险改动。
- **不要做什么**：不要把 issue 05 的验收挂在 `FORK NAMESPACE` 语句上（语法不存在，§5 #3）；不要在选型前实现 Step 6 的路线 B（与 fork O(1) 卖点冲突，§3.6）。
