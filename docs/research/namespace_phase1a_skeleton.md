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
