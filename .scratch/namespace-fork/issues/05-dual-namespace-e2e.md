# 05: Phase 1c：双 ns 端到端贯通

**What to build:** 每语句版本 pin 从 thread_local 改 session 级（约 6 处）；plan cache per-ns 实例化；热路径 4 入口改造（obmp_query 3 处 + ob_sql 1 处）；单进程内 FORK NAMESPACE fork 出 ns2，`root@ns1`/`root@ns2` 各自读写隔离。

**Blocked by:** 04

**Status:** in-progress (05a done 2026-09-23, commit 1de02137a; 05b/05c/05d pending)

## 基于工单 04 spike 的施工细化

实测结论（docs/research/namespace_schema_service_spike.md）：
- `ObServer::schema_service_` 是绑定到 `get_instance()` THE_ONE 的引用——整个 composition 硬连唯一实例。单进程第二 ns 的 schema service 必须是新实例（构造已验证，protected ctor 需放开），KV cache 名用 `cache_name_suffix` 隔离（机制已入库）。
- `bind_server_service<>` 是进程级服务定位表，per-ns 第二套会互相覆盖——**这是 05 的第一个真正卡点**。需要先决定：服务定位改经 Runtime（`server_service<T>(runtime)` 形态）还是热路径全部改显式引用。建议：Runtime 持服务组指针表，模块构造时注入引用，废弃 bind_server_service 动态查找（与不变式 2 一致）。
- ns1 特例可利用既定语义"系统 ns = 默认用户 ns 合一"：单进程 ns1 的 schema service 就是 THE_ONE（共享进程现有实例），ns1 Runtime 是对现有 composition 的轻包装，零搬迁。
- 版本 pin（worker_request_schema_version）是 IPC 版本取数的产物，单进程路径不需要它；worker 路径保留现状，单进程路径直接读本 ns 实例，不引入 thread_local。
- inner SQL 全量 refresh 在 worker 原型里需要会话绑定上下文（V22_SCAN_OPEN -4725 实测）；单进程无 IPC 路由，该约束消失，但 inner SQL 必须显式宿主 ns（工单 08）。

建议拆解顺序：
1. 05a: 共享进程恢复 ns1 SQL 服务能力（session mgr/plan cache/sql engine 以 THE_ONE 为 ns1 schema service），proxy 对 ns1 登录改进程内分发，ns>1 仍走 worker 进程。验证：root 无 @ 登录 + sysbench 点查。
2. 05b: Runtime 服务组指针表落地（替代 bind_server_service 动态查找）。
3. 05c: ns2 进程内激活（第二实例全家桶 + cache suffix + 懒激活），root@ns2 读写隔离。
4. 05d: 双 ns 并发隔离验证。

## 05a/05b 落地记录（2026-09-23）

- 05a（commit 1de02137a）：`SEEKDB_NAMESPACE_NS1_IN_PROCESS` 门控下，共享进程直接服务 ns1：proxy 把 ns1 登录字节转发到本进程 `run/sql.sock`；inner SQL 按 ns 判定反弹（`shared_inner_sql_bounces()`，ns1 走 vanilla 本地路径，ns>1 仍弹 worker）；PROXY v2 peer/conn-id 改为按前导存在与否接管。实测：零 worker 子进程服务 ns1；oltp_point_select 8t 文本协议 40.9k tps（vanilla 基线 58.0k tps，差距=proxy 转发一跳，工单 06 处理）。
- 回归修复（commit fdeac02cd）：工单 04 把 ns>1 schema recovery 块移到 `worker_bootstrapping=false` 之后，`worker_read` 切换到 posted-reply 路径但主读循环未启动，`root@ns2` 激活死锁（-4012）。`worker_bootstrapping=false` 移回 recovery 之后。教训：工单 04 只跑了 bootstrap 套件，ns>1 激活路径未被覆盖。
- 05b（commit 904586660）：NamespaceRuntime 服务槽位（void*，保 STL-only）+ `ObSQLSessionInfo::effective_schema_service()`（无槽回退 THE_ONE，行为中性）+ 热路径 4 入口改造（obmp_query 3 处 + ob_sql 1 处）+ `ObCachedSchemaGuardInfo::refresh_runtime_schema_guard(service)` 显式重载。四套件 PASS。

## 05c 施工分析（进程内 ns2 激活）

已确认的深层依赖，按处理顺序：
1. **registry 名字登记**：proxy `resolve_branch` 解析名字时已查 meta 表，顺势 `registry.add(id, name)`；fork 提交后惰性到首登激活（懒激活，免启动加载）。
2. **suffix 保留**：进程内分发时 proxy 不能剥 `@ns` 后缀（剥了会落 home ns1），转发保留后缀由 `bind_session_namespace` 经 registry find 绑定 ns2 runtime。
3. **ns2 schema service 实例化**：spike 序列正式化（ctor 放开 + cache_name_suffix 已入库）。
4. **关键卡点——worker_namespace ambient**：fork 感知的 schema 重定向层（`ob_schema_getter_guard.cpp` 4+ 处 `owns_namespace_schema()`/`worker_namespace` 判断、`worker_storage_space_for_schema`、storage IPC 边界的 (ns,local)→encoded 翻译）全部键在进程全局 `worker_namespace` 上。进程内 ns2 必须改成 session/runtime 取数。这比版本 pin（1.4）范围更大，是 05c 的主要工作量。
5. **ns2 schema 刷新驱动**：worker 模式靠 IPC 存储空间 + 共享端翻译；进程内需刷新路径的存储访问按 ns 归属（内层 SQL 带 ns2 override + 存储访问翻译点）。
6. **plan cache per-ns**：Runtime PLAN_CACHE 槽位，失效按本 ns schema 版本。

- [ ] FORK NAMESPACE ns2 FROM ns1 在单进程内完成，< 1s 返回
- [ ] root@ns2 登录（首连接阻塞等待 Runtime 懒激活）读写隔离正确
- [ ] 两 ns 并发负载互不串数据

## 05c 落地记录（2026-09-23，commit c640a4a71）

**实现**：`SEEKDB_NAMESPACE_FORKED_IN_PROCESS` 门控下，forked ns（ns>1）由共享进程内服务。worker 模式存储边界原样复用（Remote* stub 序列化帧 + serve_storage handler 做 (ns,local)→encoded 翻译），仅传输层由 Unix socket 换成进程内同步直调 `InProcessStorage`。懒激活：首登 `bind_session_namespace` → `ensure_in_process_namespace` 建 per-ns schema service/plan cache 存 Runtime 槽位；proxy `resolve_branch` 对 forked gate `registry.add(id,name)` 并转发保留 `@ns` 后缀；fork 提交时 kernel 内 `registry.add` 替代 worker spawn。

**实测**（inproc_05c5，端口 13351）：fork 出 ns2 零 worker 子进程；`root@ns2` 首登懒激活 ~0.2s；连续 INSERT/UPDATE/DELETE/BEGIN+ROLLBACK 全过；重启后数据持久；ns1/ns2 读写隔离正确；ns2 DDL 快速拒绝（1235，归工单 07）。

**e2e 暴露并修复的五个功能 bug**：
1. scan -4007：storage pushdown 计划被 `RemoteScanIterator::open` 拒绝（worker 模式靠进程级 `_pushdown_storage_level=0`）。`generate_tsc_flags`/`check_aggr_pushdown_enabled` 对进程内 forked session 强制 pd_level=0。
2. 第 2 条语句 -4002：autocommit `submit_commit_tx` 的 async 回调在无 ambient session 上下文执行 reuse/release。新增 `tx_owner_session()` 按 `tx.get_session_id()` 从 session mgr 借出属主 session（tx_desc 指针一致性校验），`tx_rpc`/`release_tx` 经其绑 `StorageSessionScope` 发 'V'。
3. `ObSqlTransControl::get_tx_service` 误用 ambient session（`SERVER_MODULE_SCOPE` 会换掉 THIS_WORKER session），改经参数 session 取 effective 服务。
4. session 清理路径（`clean_status`/`reset`/nested-restore）直接用全局 `query_transaction_service()` 绕过 effective 解析，加文件级 `release_session_tx_desc` 收口。
5. DDL 泄漏进 ns1：ns2 的 CREATE TABLE 曾写进 ns1 schema。`generate_physical_plan` 对 `in_process_session_ns>1 && is_ddl_stmt` 拒 NOT_SUPPORTED（快速失败，不再超时）。

**门禁回归修复**（同 commit）：初版把 `owns_namespace_schema()`（worker ns>=1）机械收窄为 `serves_forked_schema()`（serving ns>1），导致 ns=1 worker 的 scan/range/write stub 不再附带 caller 侧 schema，bootstrap 套件失败（SCAN_SCHEMA_REQUIRED / WRITE_PREPARE stage=schema -4007）。引入 `serves_namespace_schema() = owns_namespace_schema() || serves_forked_schema()` 恢复 worker 行为并兼容进程内 forked 服务。教训：四套件门禁必须在提交前全跑，手工实测只覆盖进程内路径。

**门禁**：bootstrap / sql_worker full / direct full / direct tls 四套件全 PASS。

**已知留白**：PX（loud fail 不错写）、AUTO_INCREMENT（单例绑 ns1 proxy）、direct insert 'J'、PS 协议 plan cache/serving scope、DROP namespace registry remove（工单 13）、forked-ns DDL（工单 07）、GRANT/CREATE USER 类非 is_ddl_stmt 语句未单独 gate。

**Status:** 05a/05b/05c done（c640a4a71）；05d 双 ns 并发隔离验证 pending

## 05d 落地记录（2026-09-23，双 ns 并发隔离验证）

**环境**：inproc_05d 实例（端口 13361，commit c640a4a71 二进制，双门控 SEEKDB_NAMESPACE_NS1_IN_PROCESS=1 + SEEKDB_NAMESPACE_FORKED_IN_PROCESS=1），零 worker 子进程。

**方法**：ns1 建 app.t（基线 24003 行）→ `FORK DATABASE __empty__ TO a; FORK DATABASE a TO nsc`（nsc=ns2，parent=1）→ 8 个并发写入线程（ns1 四个、root@nsc 四个），各自 30 批 × 100 行 INSERT + 行间 UPDATE，pk 区间完全不重叠（ns1: 6/7/8/9M 段，nsc: 6.5/7.5/8.5/9.5M 段），期间 60 次交叉 COUNT(*) 并发读。

**结果**：全部写入零报错；终态两侧各 36003 行 = 基线 24003 + 本方 12000；精确区间核验 ns1 含 nsc 段行数 0、nsc 含 ns1 段行数 0。**并发读写隔离 PASS，05d 完成，工单 05 全部落地。**

**操作陷阱记录**（非代码 bug）：`FORK DATABASE __empty__ TO x` 是 bootstrap 语义——把 ns1 登记为名字 'x'（namespace_id=1），不是创建 fork；真实 fork 必须从已登记名字再 FORK 一次（`FORK DATABASE a TO b`）。误把第一步当 fork 用会导致 root@x 绑定到 ns1、双向全可见（本次实测踩中，namespaces 表仅一行 id=1 可确诊）。该 UX 问题归工单 12（CREATE NAMESPACE 语法糖）一并解决。

**Status:** done（05a 1de02137a / 05b 904586660 / 05c c640a4a71 / 05d 实测验证）
