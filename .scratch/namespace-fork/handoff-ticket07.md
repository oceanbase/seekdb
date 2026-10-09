# Handoff: seekdb namespace fork 单进程化 — 继续工单 07（DDL 显式 ns 化）

## 下一个会话的焦点

从工单 07 `07-ddl-explicit-ns.md` 开始继续 `$implement .scratch/namespace-fork/issues/`：
解除 forked ns 的 DDL 硬拦截，DDL/rootserver 152 处调用点显式 ns 化，让 CREATE INDEX /
DROP DATABASE 等长事务 DDL 在 forked ns 内全链路可用。随后按 08→13 顺序推进。

用户在上个会话末尾说过"暂停"，07 只做了代码阅读、**零代码改动**，从干净状态起步。

## 仓库与分支

- 工作区：`/home/nijia.nj/.herdr/worktrees/seekdb/herdr-fork`
- 分支 `codex/namespace-worker-proxy-v20`，HEAD = `0256b1bd1`（已推送 GitHub，工作树干净，`.scratch/` 不入 git）
- 编译：`source ~/.bashrc && cd build_release && CARGO_NET_OFFLINE=true make -j80 seekdb`

## 关键文档（先读，勿重复内容）

- 工单目录：`.scratch/namespace-fork/issues/01..13`（01–06 已完成，每个工单文件末尾有实测结论追加）
- 单进程化 spec：commit `13e0a837e`（to-spec 产物）及 `.scratch/namespace-fork/` 相关文档
- 调用点审计：`docs/research/namespace_single_process_audit.md`（§5 定了走 B 路显式传 ns、不走 ambient；:107 附近是 152 处 DDL 调用点清单；§11 验收断言）

## 架构现状（Phase 1 完成的形态）

- 单进程多 ns：共享进程持有存储引擎 + proxy（唯一对外 MySQL 端口）
- 两个 env 门控：`SEEKDB_NAMESPACE_NS1_IN_PROCESS=1`（05a）+ `SEEKDB_NAMESPACE_FORKED_IN_PROCESS=1`（05c）
- worker 进程模式完整保留，门禁四套件跑的就是它
- 存储边界复用 Remote* stub + 帧 serve handler，仅传输层换进程内直调 `InProcessStorage`（11 号工单才拆 IPC 层）
- proxy fd 注入快速路（06）：endpoint=="run/sql.sock" 时 `MSG_PEEK` 解析 login 后 `nio_inject_fd` 移交本进程 Rust NIO，经 `PendingConn=(ConnStream, skip_greeting)` 走 accept fanout
- 分层不变式：`src/namespace/` 唯一感知 ns；sql 层经 session 拿服务实例；存储层 ns-blind；禁全局单例/thread_local ambient，显式传递代替

## 07 起步要点

- 当前拦截点：`src/sql/ob_sql.cpp:2349`（`is_ddl_stmt && in_process_session_ns>1` → NOT_SUPPORTED），另一处在 ~3400（plan cache 分支）
- 已有可复用基础设施：session 级 `effective_schema_service()`（`src/sql/ob_sql_session_info.cpp:188`，05b）、Runtime 服务槽位、`effective_plan_cache`、inner SQL ns override（`push_inner_sql_namespace_override`）
- 建议路径：先解除拦截走 session effective schema service → 跑通 CREATE TABLE/DROP/CREATE DATABASE/ALTER/CREATE INDEX 基本链路 → 再按审计清单推进 152 处；每步实测 + 四套件
- 命名陷阱：`ObPlanCacheKey::namespace_` 是库缓存命名空间（NS_CRSR），不是 fork ns，别改错

## 门禁（每次提交前必须全绿）

```bash
export SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/data/1/nijia.nj/test
python3 tools/obtest/namespace_worker_bootstrap_prototype.py --binary build_release/src/observer/seekdb
python3 tools/obtest/namespace_sql_worker_prototype.py --binary build_release/src/observer/seekdb --case full
python3 tools/obtest/namespace_worker_direct_prototype.py --binary build_release/src/observer/seekdb --case full
python3 tools/obtest/namespace_worker_direct_prototype.py --binary build_release/src/observer/seekdb --case tls
```

## 运行中的实例

- `/data/1/nijia.nj/test/inproc_05d`：端口 13361，双门控，二进制=HEAD。已有 ns 'a'(ns1 别名)、'nsc'(ns2)、'p1'、'p2'；app.t 36003 行；sbtest 10 万行
- 登录：ns1 `mysql -h127.0.0.1 -P13361 -uroot`，ns2 `-u'root@nsc'`
- vanilla 对照：`/data/1/nijia.nj/lt-local/bin/seekdb` 端口 29815
- fork 正确姿势：`FORK DATABASE __empty__ TO a`（登记 ns1 别名，bootstrap 语义，不是 fork）→ `FORK DATABASE a TO b`（真实 fork）。误把第一步当 fork 会让 root@x 绑定 ns1 双向全可见（namespaces 表仅一行 id=1 可确诊）
- sysbench 必须 `--db-ps-mode=disable`（forked ns 的 PS 协议是 05c 留白）

## 操作坑（已踩过）

- 杀实例：`--base-dir=.` 不含路径，按 `/proc/<pid>/cwd` 匹配杀；`pkill -f`/`pgrep -f` 的模式会匹配到自己 shell 的 cmdline（自杀）
- 后台启动必须 `setsid nohup ... &`，否则 exec 会话结束进程被连带杀
- 错误码：-4002 INVALID_ARGUMENT、-4007 NOT_SUPPORTED、-4012 TIMEOUT、-4015 OB_ERR_SYS、-4008 ITER_END（FORK 源是数据库名而非 ns 名时出现）

## 用户偏好

- 中文交流；prototype 最小改动、英文简洁注释；每步实测；结论追加工单文件；commit+push 备份
- **禁用 web 工具**（代理流量受限）；大下载先尝试绕过代理
- 新 C++ 类不加 `Ob` 前缀，接口保留 `I` 前缀；不顺手重命名旧类型

## 05 系列已知留白（记在各工单文件，归后续工单）

PX loud fail、AUTO_INCREMENT 单例绑 ns1、direct insert 'J'、PS 协议 plan cache/serving scope、GRANT/CREATE USER 类非 is_ddl_stmt 语句未 gate、DROP namespace registry remove（归 13）

## Suggested skills

- `implement`（若用户继续用 `$implement .scratch/namespace-fork/issues/` 驱动）
- `compile` — 编译 seekdb 二进制/单测
- `obtest` / `mysqltest` — 跑门禁与回归
- `deploy` — 部署测试实例
- `diagnosing-bugs` — 排查 DDL 链路问题
- `obperf` — 若涉及性能验证

## 2026-09-23 最新续接状态（以此节为准）

- 分支仍为 `codex/namespace-worker-proxy-v20`，最新推送 HEAD `def446339`。本轮新增并推送 `76bc4b115`、`cc5b84cd3`、`f38ddabfb`、`aecfbd653`、`def446339`；此前还有 FTS/自增/索引等多个 DDL 提交，详见 `git log` 和 `.scratch/namespace-fork/issues/07-ddl-explicit-ns.md`。
- 五个提交分别覆盖 DDL retry、DROP LOB、向量索引创建/删除/重建任务上下文、进程级会话虚表查询、FORK TABLE 任务上下文。每个提交前均编译并运行 bootstrap、SQL worker full、direct full、direct TLS 四门禁；最终日志 `/tmp/seekdb-ticket07-fork-table-gate-{bootstrap,sql,direct,tls}.log` 全 PASS。
- 子空间 FTS CREATE/DROP、普通索引 CREATE/DROP、表/列重定义、约束、自增属性 ALTER 等已成功；工单文件附具体 SQL、任务 ID 与恢复验证。ticket 07 **仍未完成**，两个验收框均未勾选；08–13 未开始。
- 明确阻塞：子空间 `CREATE VECTOR INDEX` 的辅助 direct insert 在 `src/storage/ddl/ob_ddl_struct.cpp:565` 因 namespace 协议未携带向量关联表 schema 返回 `OB_NOT_SUPPORTED`；失败任务的 type 14/15 清理现已成功。子空间 `FORK TABLE` 在任务创建前的 `CREATE_TABLET_NEW_MDS` 因 fork 源逻辑 tablet ID 未转换为物理 ID 返回 `OB_TABLET_NOT_EXIST`；继承源需要遵守 fork 快照上限，不能机械编码。父空间 FORK TABLE 成功。
- 其他已知留白：子空间隐式 AUTO_INCREMENT INSERT、FTS MATCH 查询、DROP PRIMARY KEY 堆表崩溃恢复、FK 校验动态路径、DDL retry 专用任务动态路径，以及剩余 DDL/rootserver 调用点审计。不要把四套 worker 模式门禁的 PASS 误写成子空间 DDL 全链路完成。
- 测试实例 `/data/1/nijia.nj/test/inproc_ticket07_b` 端口 13461，双 in-process 环境门控；子空间 `root@ticket07_b`。`.scratch/` 仍故意保持未跟踪，不要提交。
