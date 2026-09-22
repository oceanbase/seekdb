# Phase 1a：Namespace/Runtime/Registry 骨架 + root@ns 登录绑定

状态：骨架落地，**单 ns（ns 1）形态验收通过**；多 ns 与 per-ns 服务组留待 issue 04/05。
上游：issue `.scratch/namespace-fork/issues/03-namespace-skeleton-login-binding.md`、`namespace_single_process_plan.md` Phase 1.1/1.2、`docs/adr/0002-per-ns-service-group.md`、`docs/adr/0003-no-ambient-context.md`。

## 0. 结论

| 项 | 结果 |
|---|---|
| 无 `@` 登录落 ns 1，行为与单体一致 | **通过**：bootstrap 门禁套件 PASS（cold_bootstrap / shared_sql_forbidden / recovery） |
| session 缓存 Runtime 指针，热路径无 registry 查找 | **通过**：登录时解析一次并 `set_namespace_runtime()`，热路径只读 session 上的裸指针 |
| 无 thread_local 当前 ns、无新增 `get_instance` | **通过**：registry 由 `ObServer` 显式持有，经 `OBSERVER.get_namespace_registry()` 取得，未引入 ambient 上下文 |
| 登录绑定确实执行 | **通过**：probe 证据 `ns_id=1 account=root`（见 §4） |

## 1. 三件套落位

`src/namespace/namespace_registry_prototype.h`（头文件，注释见 §3 关于构建的说明）：

- **`Namespace`**：身份/血缘/存储根，纯元数据。id + name + parent_id + fork_scn；`SYSTEM_NAMESPACE_ID = 1`；名字规则（非空、非 `__` 前缀）。
- **`NamespaceRuntime`**：per-ns 服务组持有者。构造即绑定一个 ns（**按值持有**，与调用方无生命周期耦合，见 §5），当前持有注入的 schema service，issue 04 换成自己拥有的实例。
- **`NamespaceRegistry`**：`ns_id → Namespace/Runtime` 与 `name → ns_id` 两张索引；`register_namespace` / `get_runtime` / `get_namespace` / `resolve_name`。
- **登录路由 `split_login_namespace`**：把握手用户名按 `account@ns` 切开；无 `@` 时 ns 名为空 = 默认 ns。

## 2. 接线

| 位置 | 改动 |
|---|---|
| `ObServer`（`ob_server.h/.cpp`） | 持有 `namespace_registry_` + `namespace_system_` + `namespace_system_runtime_`（+ worker 用的 `namespace_worker_runtime_`）；新增 `init_namespace_registry()`、`register_worker_namespace()` |
| `ObServer::init()` | 在 `init_local_management_service()` 之后调用 `init_namespace_registry()` |
| worker bootstrap（`namespace_sql_worker_prototype.ipp`） | `init_schema()` 之后加 `init_namespace_registry()` + `register_worker_namespace(worker_namespace)` |
| `ObMPConnect`（`obmp_connect.h/.cpp`） | 新增 `bind_login_namespace()`，在 `create_session()` 之后、`verify_identify()` 之前调用 |
| `ObSQLSessionInfo`（`ob_sql_session_info.h/.cpp`） | 新增 `namespace_runtime_` 字段 + `get/set_namespace_runtime()`；`reset()` 时清空（pooled session 复用安全） |

进程角色差异：**worker 进程**固定绑定自己的 ns（不解析 `@` 后缀，也不能切换）；**共享进程**按 `account@ns` 路由，缺省落 ns 1。这对应"session 登录时一次绑定 Runtime，换 ns = 重连"的语义决策。

## 3. 关于构建：为什么是头文件

CMake 与 bazel 都按**显式源码清单**（`*_source_inventory.bzl` + unity group）编译，新增 `.cpp` 需要改清单。本次先把边界层做成自包含头文件（只依赖 `lib/ob_define.h`、`ob_string.h`），零构建风险；Phase 3 删除 IPC 层时随 `src/namespace/` 的正式目录一起加编译单元。

## 4. 验收证据

登录绑定 probe（`SEEKDB_NAMESPACE_LOGIN_BIND_PROBE=1` 时把每次绑定写 `/tmp/ns-login-bind.result`；worker 日志级别是 WARN，普通 marker 不落盘，故用文件）：

```
2 ns_id=1 account=root
```

即每次 login 都绑定到 ns 1 的 runtime，账号名 `root` 不带 ns 后缀（后缀只作路由信息）。

门禁：

| 运行 | 结果 |
|---|---|
| probe 开 | `{"event": "PASS", "cold_bootstrap": true, "shared_sql_forbidden": true, "recovery": true}`，归档 `.../namespace_fork_PROTOTYPE_bootstrap_v18_exejph_l/data.tar.gz` |
| probe 关 | `{"event": "PASS", ...}`，归档 `.../namespace_fork_PROTOTYPE_bootstrap_v18_ccz_fz7m/data.tar.gz` |

worker bootstrap 步骤实测：`init_namespace_registry() => 0`、`register_worker_namespace(...) => 0`。

## 5. 过程中修掉的两个真实缺陷

1. **runtime 悬垂指针**：第一版 `NamespaceRuntime::init(const Namespace &)` 存的是 `const Namespace *`，而 `register_namespace` 传的是**局部** `Namespace`，函数返回即悬垂；worker 启动直接崩在 bootstrap 1/10。改为**按值持有** `Namespace`，并让 `runtime.init()` 先于插入 registry 执行（失败自然回滚，无需 erase）。
2. **用户名缓冲区越界**：`bind_login_namespace` 把 `account@ns` 的账号名拷回 `user_name_var_`，但长度检查在后面的 `load_privilege_info` 才做，超长名会先越界写。已在拷贝前加同款边界检查（超限返回 `OB_PASSWORD_WRONG`，与既有行为一致）。

## 6. 未完成 / 下一步

- **多 ns**：`FORK NAMESPACE` 当前仍是 worker/kernel 侧的原型路径；单进程内 fork 出 ns 2 并让它进入本 registry，属于 issue 05。
- **per-ns 服务组**：runtime 现在只注入 schema service；issue 04 的 `alloc_instance`/instance_tag 机制要接进来，让每个 runtime 拥有自己的 schema service。
- **`root@ns` 在共享进程的端到端**：共享进程的 2881 入口现在由 worker 代理接管，因此 gate 套件只覆盖 worker 侧的绑定；共享进程按名路由的路径要等单进程形态（Phase 3 删代理）才端到端可用。
- **依赖方向**：`src/sql/session/ob_sql_session_info.h` 反向 include 了 `src/namespace/...`，与 `plan.md` 的 `namespace → observer → sql → storage` 方向相反。当前用最小 include 降低影响；Phase 1 收尾时应把 session 侧的 runtime 绑定改为不透明句柄（或把该类型下沉），并靠 bazel 门禁固化。

---

# 附：Phase 1c 进展（issue 05，进行中）

状态：**服务组入口打通并验证**；"单进程内 fork 出 ns2"与性能门禁仍需单进程 fork 机制，未完成。

## 7. runtime 拥有 per-ns schema service 实例（已完成）

`NamespaceRuntime` 新增 `set_owned_schema_service(instance, tag)` / `owns_schema_service()` / `get_schema_service_instance_tag()` / `set_service_inited()` / `is_service_inited()`：

- 实例由调用方通过 issue 04 的 `alloc_instance()` 分配、用 `free_instance()` 释放；
  runtime 只记录归属与 tag，**不解引用 schema service 的完整类型**（头文件保持零依赖）。
- tag 即 issue 04 的 cache 命名空间化参数，保证多实例在进程级 KV 注册表里不撞名。
- 系统 ns 仍走 `set_schema_service(&schema_service_)`（进程级 schema 权威），行为不变。
- `is_service_inited()` 为将来"懒激活：首连接阻塞等待 runtime 就绪"预留判据。

验证（probe `SEEKDB_NAMESPACE_SERVICE_GROUP_PROBE=1`，写 `/tmp/ns-runtime-group.result`）：

```
group_ok=true runtime_active=1 owns_schema=1 service_inited=1 tag=_ns5_probe
singleton_leaked=0 owned_isolation=1 registry_count=1
```

即：runtime 完成绑定、own 了一个真实 schema service 实例、该实例 init 成功且与自己 tag 对应；写它的 published version 不会污染进程单例（`singleton_leaked=0`），实例自身状态独立可写（`owned_isolation=1`）。

同时跑 issue 04 的 probe（两者互不影响）：

```
coexist=true init_ret=0 primary_leaked=0 second_isolation=1
```

门禁（三种模式全绿）：

| 运行 | 结果 |
|---|---|
| 两个 probe 全开 | PASS（cold_bootstrap / shared_sql_forbidden / recovery） |
| 仅服务组 probe | PASS，归档 `.../bootstrap_v18_p4tp9k4z/data.tar.gz` |
| 全关（回归） | PASS，归档 `.../bootstrap_v18_1clkg5kh/data.tar.gz` |

## 8. 明确不做的改动及理由：`worker_request_schema_version` 转 session 级

`plan.md` Phase 1.4 要求把"每语句版本 pin 从 thread_local 改 session 级（约 6 处）"。实测该 thread_local 的**全部 4 个读写点都被 `observer::namespace_worker_prototype::uses_remote_schema()` 门控**：

- `ob_multi_version_schema_service.cpp:664`（读，remote 时兜底 requested_version）
- `:1733-1738`（DDL/刷新可见性栅栏，remote 时抬升 pin）
- `:2389-2390`（读，remote 时作为 published version）
- `:1619-1623`（gateway 诊断打印）

也就是说它只服务 **worker 进程的远程 schema 路径**，而 worker 进程模式在 ADR-0001 里已被判死刑、Phase 3 整体删除。现在把它改成 session 级，等于给一条即将删除的路径重做载体，且 `bind_login_namespace` 之后的 session 与 schema service 之间没有现成通道。**结论：本阶段不转换，随 Phase 3 删除该路径时一并消失**；单进程下的等价机制是"runtime 拥有自己的 schema service 实例"（§7 已完成），版本自然按 runtime 归属，不需要 thread_local。

如果 reviewer 认为仍需在 worker 路径上消除 thread_local，应作为 Phase 3 之前的一次独立清理，而不是混在 issue 05 里。

## 9. issue 05 剩余工作

1. **单进程内 fork 出 ns2**：需要把 `FORK NAMESPACE` 从 worker/kernel 原型路径接入单进程 registry（创建 Namespace 记录 + runtime 懒激活）。这是 issue 05 的主线，依赖控制元数据（`__all_namespace`）与 kernel 的 id 编码翻译层在单进程内的落位。
2. **plan cache per-ns 实例化**：`ObPlanCache` 已是 `server_module_*` 风格，需按 runtime 持有。
3. **热路径 4 入口**（`obmp_query.cpp:104/515/850`、`ob_sql.cpp:1115`）改为取 session 绑定的 runtime 的 schema 版本。
4. **性能门禁**：sysbench `oltp_point_select` 1t/8t 对比单体基线（issue 06）。

## 10. 路由 + 服务组半边已闭环（本轮）

`probe_namespace_service_group` 现在把 **ns2 真的注册进 live registry**（id=500001、name="ns2"、parent=1），并让它拥有自己的 schema service 实例；注册后即参与登录路由。probe 结果：

```
group_ok=true registry_count=2 resolve_ret=0 resolved_id=500001
runtime_by_id_ok=1 runtime_by_name_ok=1 same_runtime=1
owns_schema=1 service_inited=1 tag=_ns2_probe fork_parent=1
singleton_leaked=0 owned_isolation=1 shape_active=1 shape_owns=1
```

逐项含义：

- `registry_count=2`：系统 ns + fork 出的 ns2 同时在册；
- `resolve_name("ns2") → 500001`，且 **by-id 与 by-name 取到同一个 runtime**（`same_runtime=1`）—— 这正是 `bind_login_namespace` 的解析路径，`root@ns2` 会落到 ns2 的 runtime；
- `owns_schema=1 / service_inited=1 / tag=_ns2_probe`：ns2 runtime 拥有并初始化了自己的 schema service 实例；
- `fork_parent=1`：血缘记录 ns2 的父是系统 ns；
- `singleton_leaked=0 / owned_isolation=1`：写 ns2 实例的 published version 不污染进程单例，且 ns2 实例自身状态独立可写；
- `shape_active/shape_owns`：临时 runtime 的绑定/归属语义正确。

本轮同时修掉两个真实缺陷：`NamespaceRegistry::init/register_namespace` 现在**幂等**（崩溃恢复会在同一进程内重跑启动序列，原先第二次会返回 `OB_INIT_TWICE`/`OB_ENTRY_EXIST`，probe 复跑还会二次分配并覆盖 ns2 指针，实测导致 `-11` 崩溃）；probe 自身也做了已注册短路。

**仍未完成**：这一步只证明"路由 + 服务组"这半边闭环；ns2 的数据隔离（fork 的 COW/快照语义、`root@ns2` 实际读写不串数据）仍依赖 kernel 在单进程内落位，属于 §9.1。


