# 规格：Namespace 服务统一归属，消除系统 ns 特例和全局回退

## Problem Statement

用户连接某个 Namespace 后，schema、缓存、自增和 DDL 等操作应始终使用该 Namespace 的数据与运行时实例。当前实现把系统 ns 的若干实例保留为进程单例，又允许请求或任务在缺少所属 NamespaceRuntime 时退回全局对象。这会把本应失败的上下文丢失变成静默访问其他 Namespace；不同 Namespace 复用相同的 schema_id、database_id 时，全局统计和预处理语句缓存还可能混用状态。系统 ns 作为 bootstrap 和全局管理元数据的承载者，不应成为普通请求的隐含默认值。

## Solution

每个 Namespace，包括系统 ns，都以同一种方式拥有和获取自己的 NamespaceRuntime 服务组。登录、内部 SQL 和异步任务在入口绑定或显式接收所属运行时；正常请求路径不按 ns_id 分流，也不在运行时缺失时退回进程单例。系统 ns 的管理权限及全局调度元数据目标由显式能力和目标 Namespace 表达。与 schema、SQL proxy、缓存、序列或后台任务有关的可变状态归所属 Namespace；真正按物理 tablet 等全局唯一键工作的底层执行资源可以共享，但必须显式获得所需依赖。

## User Stories

1. As a Namespace user, I want each query to resolve schema in my Namespace, so that an identically numbered table elsewhere cannot affect my result.
2. As a Namespace user, I want reads and writes to use my Namespace's SQL proxy, so that internal table access stays within my catalog.
3. As a Namespace user, I want DDL to use my Namespace's schema and management services, so that its result is visible only where I issued it.
4. As a Namespace user, I want DDL tasks to retain their owner after asynchronous dispatch, so that completion and cleanup affect the intended Namespace.
5. As a Namespace user, I want task recovery after restart to restore the same owner, so that a resumed task cannot use another Namespace's services.
6. As a Namespace user, I want missing request context to produce an error, so that the server never silently redirects my work.
7. As a Namespace user, I want missing task context to produce an actionable failure, so that background work cannot silently redirect its data access.
8. As a Namespace user, I want table and column statistics isolated, so that another Namespace's row counts or histograms do not change my plans.
9. As a Namespace user, I want statistics collection and monitoring isolated, so that my activity is neither attributed to nor flushed into another Namespace.
10. As a Namespace user, I want prepared statements isolated, so that identical database IDs and SQL text cannot reuse another Namespace's statement metadata.
11. As a Namespace user, I want plan cache entries and invalidations isolated, so that DDL elsewhere does not change my execution plans.
12. As a Namespace user, I want automatic increment state isolated, so that inserts and DDL use the correct sequence.
13. As a Namespace user, I want tablet automatic increment operations to route through my Namespace's tablet mapping, so that a logical tablet ID never targets another Namespace's storage.
14. As a Namespace user, I want direct insert and DML to use explicit service bindings, so that bulk writes cannot escape their owner.
15. As a Namespace user, I want vector index queries and maintenance to use my Namespace's schema and SQL dependencies, so that background loading and cleanup remain isolated.
16. As a Namespace user, I want physical tablet schema resolution to find the owner explicitly, so that shared storage tasks never assume the system Namespace.
17. As a system Namespace user, I want ordinary SQL to follow the same service selection rules as every other Namespace, so that behavior does not depend on a hidden privileged fast path.
18. As a system administrator, I want Namespace management commands to require an explicit authority, so that global control remains auditable.
19. As a system administrator, I want bootstrap and global scheduling metadata operations to name their target Namespace explicitly, so that control operations never rely on a missing context.
20. As a developer, I want one service composition path for all Namespaces, so that new modules cannot accidentally register only for non-system Namespaces.
21. As a developer, I want service ownership and destruction tied to NamespaceRuntime, so that a dropped or reactivated Namespace cannot retain stale state.
22. As a developer, I want asynchronous work to carry the required service references, so that thread changes do not lose ownership.
23. As a developer, I want shared infrastructure to receive explicit physical identifiers or service references, so that it stays Namespace blind.
24. As a developer, I want direct singleton access and implicit fallback removed from normal request paths, so that code review can identify ownership from the interface.
25. As a tester, I want the same operations exercised in the system and a forked Namespace, so that accidental special treatment is observable.
26. As a tester, I want two Namespaces with identical logical IDs but divergent schemas and data, so that cross-Namespace cache reuse is observable.
27. As a tester, I want restart and task recovery checks for each affected service family, so that ownership survives process lifecycle changes.
28. As an operator, I want an activation failure to remain explicit and recoverable, so that resource pressure cannot produce a wrong-Namespace request.
29. As an operator, I want per-Namespace resource use to be visible, so that splitting stateful caches does not hide memory growth.
30. As an operator, I want global storage, transaction, networking, and process resources to remain shared where they are Namespace blind, so that service isolation does not duplicate the engine.

## Implementation Decisions

- Preserve the domain model: Namespace holds identity, lineage, and storage root; NamespaceRuntime owns per-Namespace compute services; NamespaceRegistry is the sole global lookup of Namespace identity. The system ns remains the bootstrap/control metadata owner, but has no special service lookup semantics in ordinary requests.
- Construct and register the same required service group for every active Namespace, including the system ns. Move existing process-owned schema, SQL proxy, plan cache, direct insert, root command, schema lifecycle, and automatic increment wiring into that composition model. A process resource may be injected where appropriate, but a missing runtime service is an initialization error.
- Bind a NamespaceRuntime at login and preserve it through SQL execution, inner SQL, PX work, DDL task dispatch, retry, recovery, and cleanup. Background work without a session carries explicit owner service references or a stable Namespace identity resolved at dispatch. Do not infer an owner from thread local state or a global singleton.
- Remove `get_instance()` and server-global service fallbacks from normal request paths. Interfaces that currently accept a fallback object instead require the bound service or return an error. Bootstrap and process administration name their target explicitly.
- Replace system-versus-fork service classes and ns-ID branches in normal execution with one service contract configured by explicit catalog/storage dependencies. Keep management authority as a capability, not an implication of an ID in arbitrary modules.
- Give optimizer statistics management, statistics monitoring, and prepared statement cache per-Namespace instances. Their existing logical keys can remain unchanged within an instance; schema_id and database_id remain Namespace-local and are not encoded with ns_id.
- Preserve per-Namespace plan cache ownership and route all accesses and invalidations to its owner. Prepared statement metadata and its plan cache must have compatible lifetimes.
- Ensure the shared physical tablet schema resolver resolves the owning Namespace from an explicit physical tablet identity or receives the required schema service directly. Remove direct use of its system-Namespace default in normal storage operations.
- Give vector operations explicit owner schema and SQL dependencies. Decide the vector manager instance boundary with the separately recorded scheduler/cache TODO; until then, no request or task may rely on the process-global system-Namespace dependencies. Physical tablet cache keys remain Namespace-distinct.
- Keep the shared storage engine, transaction/log services, networking, and genuinely stateless execution adapters process-wide. Sharing is allowed only when mutable Namespace-local state is absent or keyed by a globally unique physical resource ID and all service dependencies are explicit.
- Retain system Namespace management authority and the location of global scheduling metadata established by the current architecture. These are control-plane decisions, not normal request fallbacks.
- This version has no persistence-format or upgrade-compatibility requirement. Do not encode ns_id into table, database, or other schema IDs.

## Testing Decisions

- Use the existing MySQL-facing four-part gate as the primary test seam. Assertions check observable SQL results, errors, catalog isolation, cache behavior, and task recovery rather than internal pointers or service implementation types. Prior namespace bootstrap, SQL, direct-access, and TLS gates already exercise both the system and forked Namespaces.
- Test the same DDL/DML and prepared-statement sequences through two Namespace connections with shared logical schema/database IDs but divergent table definitions and data. Include statistics gathering, plan choice, cache invalidation, automatic increment, direct insert, and vector operations where supported.
- Exercise asynchronous DDL and vector maintenance across restart, then verify task records, data, and index behavior only in the owning Namespace. Include failure cleanup paths that previously selected a global service.
- Validate a missing runtime or required service at the highest reachable request/task boundary with a visible error. Do not create test-only seams or assert the spelling of internal helper functions.
- Include an ordinary SQL regression in the system Namespace to prove the common path serves it correctly; keep management and bootstrap checks separate.
- Add previously failing focused cases to the local four-part gate. The current user preference is not to commit test or other non-code artifacts to the branch. Full mysqltest and sysbench runs are not required for this work.

## Out of Scope

- Changing Namespace fork lineage, snapshot semantics, physical tablet-ID encoding, or the location of system control metadata.
- Adding compatibility layers for older persistence formats or an upgrade path.
- Rebuilding the vector scheduler/cache architecture recorded in the separate TODO, beyond removing implicit system-Namespace service dependencies in affected paths.
- Implementing unsupported SQL features solely to exercise this refactor, including exchange partition.
- A repository-wide rewrite of unrelated process-global resources that are demonstrably Namespace blind.
- Running full mysqltest or sysbench suites, or merging test/non-code files into the code branch.

## Further Notes

- Existing ADRs require per-Namespace stateful services and explicit context; the present singleton and fallback paths are unfinished migration work. The user specifically rejects treating ns 1 as a default service source.
- Distinguish confirmed structural risks from reproduced failures. The statistics and prepared statement cache keys omit Namespace identity while their current instances are shared; the specification requires isolation even if a particular collision has not yet been reproduced.
- A process-wide coordinator may iterate the Registry when its task is genuinely global, such as merge/freeze progress aggregation. That does not permit a normal request or Namespace-local background task to choose the system ns as a fallback.
