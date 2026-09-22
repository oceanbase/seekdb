# Spec：单进程多 Namespace Fork

状态：待发布到 issue tracker。依据：CONTEXT.md（术语）、docs/adr/0001-0003、namespace_single_process_plan.md（方案）、namespace_single_process_audit.md（审计）。
对账依据：`namespace_work_handoff.md`（13 个 issue 现状）、`namespace_kernel_single_process_gap.md`（kernel 落位差距）、`namespace_step1_identity_feasibility.md`（Step 1 判定）。

> **提交前必读：设计与现状的对账（2026-09-22）**
>
> 本 spec 的 Implementation Decisions 是**目标形态**。下列条目经本会话实证核对，**与当前代码不一致**，直接提交会把目标当现状，故在此逐条标注（详见 `namespace_work_handoff.md` 与 `namespace_kernel_single_process_gap.md`）：
>
> | # | spec 写法 | 代码现状 | 处置 |
> |---|---|---|---|
> | 1 | 语法终态含 **`FORK NAMESPACE`**（主路径） | parser **无该语法**（只有 `FORK TABLE` / `FORK DATABASE`，`sql_parser_mysql_mode.y:4400/4407`） | 要么把它列为**待实现**，要么改用现有 `FORK DATABASE` 口径验收 |
> | 2 | ns 名存系统 ns 的 **`__all_namespace`** | 实际是 `__fork_proto_meta.namespaces`（`kernel.cpp:53`）；`__all_namespace` 在代码里只存在于无关的 `__all_namespace_worker_parameter` | 统一为实际表名或明确改名计划 |
> | 3 | 内置只读模板 **`__template__`** | 代码中**出现 0 次**，尚未落地 | 标为未实现 |
> | 4 | `ControlSqlNamespaceScope` thread_local hack **升格为入口显式参数** | 尚未开始；且实测该 ambient 有**三条**载体（worker 全局 / thread_local / **trace id 键的进程级 map**），都不命中时**静默返回 ns 1** | 见下方"Step 1 的真实形态"一段 |
> | 5 | 模块边界靠 **bazel visibility 编译期强制** | 门禁规则已就位，但本环境 bazel 无法完成分析，**"违规被拦下"未实证** | 标注为"规则就位、待 CI 实证" |
> | 6 | Runtime 服务组 **per-ns 实例化（worker prototype 已验证可行性）** | 边界层与 schema service **实例化**已验证（`coexist=true`、`owns_schema=1`）；但 **forked ns 的 schema 权威未选型**（现状由 kernel catalog 覆盖层直接服务） | 先做架构选型，再据此写验收 |
> | 7 | 存储侧保留 **MVCC pin** 语义 | 现状是 **snapshot 根 + ref_count**（`snapshots` 表），不是 MVCC pin | 措辞按现状修正 |
>
> **Step 1 的真实形态**：身份分类已完成（kernel 25 个 ambient 函数全部可显式化，判定 GO），但它还包含**把 ns 参数化到 inner SQL 入口**——`ObInnerSQLConnection::execute` 一族目前不收 ns，是从上述三条载体里"捡"的。因此 Phase 1.4/2.4 的工作量高于本 spec 原先的描述。
>
> **验收前置**：issue 05/06 的验收标准在 §"未决决策：forked ns 的 schema 权威"确定前**不可执行**。


## Problem Statement

用户（DBA/开发者）需要在同一套数据和资源上秒级派生相互隔离的数据库实例：给测试/分析/多业务方各自一份独立演化的完整数据库，而不是共享库靠库名前缀区分。单体数据库做不到实例级隔离；第一版 worker 进程架构做到了隔离但通用查询性能只有单体的 1/7，不可用。

## Solution

namespace 成为单进程内的对象：每个 namespace 拥有独立的身份、schema、权限、会话与后台服务组，共享同一个存储引擎与 SQL 执行资源。`FORK NAMESPACE` 从任一现有 ns 秒级派生一致快照的新 ns；`CREATE NAMESPACE` 建空 ns；`root@ns名` 登录指定 ns，不指定则落默认 ns，行为与单体完全一致。fork 后各 ns 完全独立演化，互不可见对方的新写入。

## User Stories

1. As a DBA, I want to fork a running database into a new namespace in under a second, so that I can give test environments real production-shaped data without copy time.
2. As a DBA, I want the source namespace to keep serving reads and writes during fork, so that production traffic is never blocked by provisioning.
3. As a developer, I want to log into a specific namespace with `root@ns名`, so that I can work against my own instance with a plain MySQL client.
4. As a developer, I want login without `@ns` to land in the default namespace unchanged, so that existing tooling and muscle memory keep working.
5. As a DBA, I want to create an empty namespace with `CREATE NAMESPACE`, so that I can provision a fresh isolated instance without forking real data.
6. As a namespace user, I want my tables, permissions, system variables, and statistics to evolve independently after fork, so that my experiments never leak into other namespaces.
7. As a namespace user, I want to be unable to see or touch other namespaces' data, sessions, or objects, so that isolation is a hard boundary rather than a convention.
8. As a DBA, I want fork to capture a consistent point-in-time snapshot, so that the child namespace is transactionally coherent.
9. As a DBA, I want to fork a namespace that was itself forked, so that I can build lineage chains (e.g. prod → staging → dev) without depth limits.
10. As a DBA, I want to drop a namespace when it is no longer needed, so that its storage is reclaimed asynchronously.
11. As a DBA, I want drop to be refused while the namespace has active connections, so that I never silently kill in-flight work.
12. As a DBA, I want to drop a namespace that has children forked from it, so that retired parents don't accumulate forever.
13. As a DBA, I want namespace management operations (fork/drop) restricted to the system namespace with SUPER privilege, so that the control plane has one auditable choke point.
14. As a DBA, I want a global monitoring view in the system namespace (processlist/tablet stats across all namespaces), so that I can operate the whole instance from one place.
15. As a namespace user, I want my monitoring views filtered to my own namespace, so that I can't observe other tenants' activity.
16. As a forked-namespace user, I want my first connection to wait briefly while my runtime activates, so that fork can return immediately without paying startup cost for namespaces nobody uses.
17. As a forked-namespace user, I want a failed activation to surface as a connection error that succeeds on retry, so that transient resource pressure doesn't brick my namespace.
18. As a DBA, I want forked namespaces to inherit system variables automatically, so that tuning travels with the data.
19. As a performance-sensitive user, I want point-select and general query performance in any namespace to match the single-process baseline, so that isolation costs me nothing at runtime.
20. As a developer, I want to fork individual tables (FORK TABLE) as today, so that lightweight data copies don't require a whole namespace.
21. As an operator, I want storage and transaction infrastructure shared across namespaces, so that memory and background work do not multiply with namespace count.
22. As an operator, I want TLS, deployment, and platform support to behave exactly like the single-process baseline, so that namespace support adds no operational surface.

## 未决决策：forked ns 的 schema 权威（阻塞 issue 05/06 验收）

fork 出的 ns，其 schema 由谁服务？两条路线，**必须先选一条**，因为它决定验收标准怎么写：

- **路线 A：保留 kernel catalog 覆盖层**。现状即如此（`namespace_worker_gateway_prototype.ipp:544-568`）。改动有界；代价是 forked ns 的 schema 不经过 `ObMultiVersionSchemaService`，与 ADR-0002"每个 Runtime 持有 per-ns schema service"的表述不一致，相关能力（刷新调度、plan cache 失效语义）需另行定义。
- **路线 B：actualize 出真的 per-ns schema service**。与 ADR-0002 一致；但 fork 的 O(1) 卖点依赖"不物化"（`kernel.cpp:3015`），物化 schema 与之冲突，且需要为每个 fork 出的 ns 补 schema 刷新链路。

**影响**：选型结果决定 issue 05（"`root@ns2` 读写隔离正确"如何验证、是否要求真 schema service）与 issue 06（性能基线在哪种实现上对比）。在选型完成前，按现有 issue 文本实现没有意义。

## Implementation Decisions（目标形态；现状差异见文首对账表）

- 架构：单进程多 namespace（ADR-0001）。三件套领域模型：**Namespace**（身份/血缘/存储根，纯元数据）、**NamespaceRuntime**（per-ns 服务组持有者，纯计算层）、**NamespaceRegistry**（唯一新全局，ns 语义的合法解释点之一）。
- 不变式：存储引擎 ns-blind（只认编码 id）；无 ambient 上下文——禁全局单例与 thread_local，一律显式传递（ADR-0003）；传服务实例不传 ns 身份，ns_id/ns_name 解释权只归入口层；SQL 执行线程与 NIO 全局共享、后台服务 per-ns 实例化（ADR-0002）；模块边界靠 bazel visibility 编译期强制。
- 终态分层：入口层感知 ns（登录路由、Registry、id 编码翻译），SQL 层基本不感知，共享存储层完全不感知。源码布局同构：新建 `src/namespace/` 边界层，依赖方向单向 `namespace → observer → sql → storage`。
- Runtime 服务组：schema service、plan/ps cache、session mgr、PX pools、DTL、sql memory manager、DDL launcher/scheduler、shared timer、stats、autoincrement 等 per-ns 实例化（worker prototype 已验证可行性）；存储引擎族、事务日志族、网络/NIO、freeze 执行层共享。
- freeze/merge 调度：全局单例，遍历 Registry 逐 ns 取 schema guard 传入进度检查器（checker 内部零改动）；全局调度元数据（freeze info 等）由系统 ns 承载。
- inner SQL：永远有宿主 ns。进程内直连 SQL 引擎（无协议、无线程移交）；Runtime 内默认本 ns；共享层显式指定目标 ns（全局调度元数据 → 系统 ns）；`ControlSqlNamespaceScope` thread_local hack 升格为入口显式参数。
- ns 身份：名字存系统 ns 的 `__all_namespace` 表（name、ns_id、parent、fork scn），全局唯一，id 创建时分配；v1 不支持 rename；`__` 前缀保留。
- 语法终态：`FORK NAMESPACE ns2 FROM ns1`（主路径）、`CREATE NAMESPACE ns3`（等价于从内置只读模板 `__template__` fork，对用户隐藏模板）、FORK TABLE 保留、FORK DATABASE 退役。
- 认证与权限：`root@ns` = 该 ns 的 root；权限 fork 继承后独立演化；FORK/DROP NAMESPACE 仅系统 ns 内 SUPER 权限可执行。
- 系统 ns = ns 1：bootstrap 自带，系统 ns + 默认用户 ns 合一；`__template__` 只做只读模板母本，不承载持续写入。
- fork 语义：执行时刻全局一致快照（MVCC pin），源 ns 写入不阻塞，fork 后双向独立演化。fork 同步完成元数据登记（< 1s）即可登录；Runtime 懒激活，首连接阻塞等待；激活失败 = 连接报错，不持久化状态，重试自然重触发。
- drop 语义：有活跃连接拒绝；允许 drop 源 ns（子 ns 例外表回源改指 snapshot 根）；异步 GC 回收，drop 只删元数据 + 注销 Registry。
- 隔离：禁止跨 ns 数据访问（无语法）；session 登录时一次绑定 Runtime，不能切换，换 ns = 重连；系统变量 per-ns，fork 自然继承。
- 监控：普通 ns 虚表只见本 ns；系统 ns 提供全局视图。
- 存储侧既有机制保留：例外表覆盖层（`encode(base_ns, tablet) → owner_ns`）、tablet id `[ns:32][local:32]` 编码、fork 全局 MVCC pin。

## Testing Decisions

- 唯一接缝：MySQL 协议面 + 门禁脚本。只测外部行为（fork 语法、登录路由、隔离性、drop、监控视图、性能数字），不测 Runtime/Registry 内部结构，不新开内部测试夹具。
- 载体（全部现成，适配单进程形态）：
  - 四个 prototype 门禁套件（bootstrap/sql_worker/direct/tls 四脚本，PASS 判据 exit 0 + `{"event":"PASS"}`）——fork 正确性、隔离、路由。
  - mysqltest 基础套件——通用 SQL 行为回归。
  - sysbench `oltp_point_select` 等——性能门禁：任一 ns 内达到单体基线（Phase 1 出口硬门槛）；fork→可用 < 1s。
- 性能门禁先行：Phase 1 不过 sysbench 对标不进入 Phase 2。

## Out of Scope

- serverless 休眠/唤醒（冷 ns 停服务退线程）：Phase 4 独立排期。
- MVCC pin 渐进松绑（全局 pin 下放 per-tablet）：Phase 4。
- 共享线程池化（合并 per-ns timer/PX/DDL 线程）：按实测资源占用后定。
- 跨机/分布式部署、多平台 IPC：随单进程化整体消失，无工作项。
- ns rename、ns_id 复用、跨 ns 查询/迁移工具。
- 兼容旧持久化格式与升级路径（本分支不考虑兼容性）。

## Further Notes

- 性能是本项目的第一公民：单进程化转向的唯一理由就是 worker 进程架构的性能不可接受（ADR-0001），任何后续设计妥协都不得退回跨进程热路径。
- fork 相关语法与管理操作全部收口系统 ns，是未来审计、限流、计费的自然挂点。
