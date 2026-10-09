# V11：连接级 SQL session 原型

日期：2026-09-14。实验分支：`codex/namespace-sql-session-v11`。基于 V10 及其 worker 内存定容修正。

同 worker 的有限并发和 IPC 分发已继续到 [V12 原型](namespace_worker_concurrency_v12.md)。本文保留 V11 的实现与验收记录；当前流程脚本已扩展到 V12。

每个 MySQL 连接在登录时创建一个 worker 内的真实 `ObSQLSessionInfo`，后续请求使用同一个对象。用户变量、允许的 session 系统变量和默认数据库随连接保留；关闭连接后销毁 session。共享端口、namespace 固定绑定和真实引擎扫描沿用 V10。

## 路径与所有权

```text
socket 的 ObSMConnection
  └─ SessionBinding：本地 worker 路由指针 + worker 代次 + slot/slot_generation
       └─ IPC：按 slot 索引，校验 generation
            └─ 稳定地址的 SessionOwner → ObSQLSessionInfo → 原有 SQL 执行器
```

共享入口登录时绑定路由，后续执行无需查 namespace map。入口现有 SQL session 由连接持有一次引用，`packet_sender.get_session()` 直接借用该指针，无需每次按 `session_id` 查 map；原有非 worker 连接保持原路径。

worker 使用标准库 `vector<SessionSlot>`，连接增加时按需扩容，没有按最大连接数预分配。slot 保存 `unique_ptr<SessionOwner>` 和 64 位 generation，session 独立分配，vector 扩容不移动 session。IPC 入口只做一次下标访问和代次校验，之后沿用本地 session 指针。

关闭后销毁整个 SessionOwner；空闲 slot 可复用，generation 加一，旧句柄不能引用后来创建的 session。代次耗尽则停止复用该 slot。slot 元数据及空闲下标数组保留到 worker 的连接高水位，不随每次关闭缩容；worker 退出时整体释放。分配器是否立即向操作系统归还页不由 slot 回收保证。

断开连接使用现有 `ObDisconnectTask`，没有增加 session 回收线程。Rust 请求生命周期先结束当前请求，再转交 binding。若客户端在查询中断开，共享端收完当前 IPC 回复并释放扫描，再关闭远端 session；其他连接仍可使用该 worker。异常 IPC 或 worker 死亡按原路径失败，重启后旧连接不能进入新进程。

## 本轮功能

- `SET @x=...`、`SELECT @x`，包括字符串值；由原有 resolver/executor 处理，保存在 worker session 内。
- `SET NAMES`，以及 session 的 `sql_mode`、`ob_query_timeout`、`character_set_client/connection/results`、`collation_connection`。
- 同 namespace 的 `USE db` 和 `COM_INIT_DB`。IPC 将协议数据库名作为独立字段传递，再按标识符转义，通过原有 USE 执行器处理。跨 namespace、系统库和不存在的库被拒绝。
- gateway 同步默认数据库及协议编码需要的少量标量状态；不会把整个 session 或用户变量序列化回来。
- SQL 错误后保留连接状态；多语句、全局 SET、写入、事务和未支持的命令明确失败。

## 验证

从仓库根目录使用当前分支编译的二进制：

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp \
python3 tools/obtest/namespace_sql_worker_prototype.py \
  --binary build_release/src/observer/seekdb

python3 tools/obtest/namespace_worker_handles_prototype.py \
  --binary build_release/src/observer/seekdb
```

第一个脚本自建隔离实例，准备真实 fork 表并经公共 MySQL 端口验收。包含变量/字符集/SQL mode 隔离、两种数据库切换、拒绝路径、14 个同时存活的 session、扩容后原连接可用、反复复用、正常关闭、TCP reset、查询中断线和 worker 强杀重启。最后重跑 V10 的扫描、过滤、排序、多批结果和资源检查。第二个脚本直接驱动 worker IPC，覆盖过期 generation、非法下标、重复关闭、复用后空白状态和最终 active=0；它不替代真实存储测试。

本轮验收均通过：

- [离线 release 编译](/data/1/tmp/namespace-session-v11-build-verified.log)。二进制 SHA-256：`3d796cec86ababcf0568de855d70d0ed967be5aaf9fb4f66e381879ee0006808`。
- [真实 MySQL 流程](/data/1/tmp/namespace-session-v11-flow-verified.log)：PASS。slot 高水位 14，关闭附加连接后 active=2，重复连接复用槽位；查询中断线实际触发 `PROTOTYPE_V11_RESPONSE_DRAIN`，原有连接变量和表查询均保持正确。强杀后的旧连接返回 4124，新连接变量为空且可读原表。
- [IPC 句柄探针](/data/1/tmp/namespace-session-v11-handles-verified.log)：PASS。17 个 session 扩容、槽位复用、非法/过期句柄及重复关闭被拒绝，最终 active=0。
- [关闭 worker 开关后的 V9 回归](/data/1/tmp/namespace-session-v11-native-regression.log)：PASS。20 轮 fork/访问/drop、重启恢复及最终元数据页数为 0。

真实流程实例为 `/tmp/namespace_fork_PROTOTYPE_sql_session_v11_ljudwl4z`，公共端口 62643；回归实例为 `/tmp/namespace_fork_PROTOTYPE_metadata_gc_v9_lifecycle_hf12gixv`。实验已停止，数据归档在各自的 `data.tar.gz` 中。worker 小数据流程后的私有页约 18 MiB，沿用的 64 MiB 回归阈值通过；本轮未做一体化内存对照或高并发测量。

## 保留的边界

这是功能原型，仍只接入 root 和简单只读表。文本结果增加字符串支持，尚无完整类型覆盖。预处理语句、事务、写入、重认证、连接重置、完整 warning/session tracking 尚未实现。

共享进程仍保留现有认证/协议 session；worker 中是持久 SQL session。本轮没有精简 gateway session，也没有消除 V10 已有的 schema 副本或调整进程预算。

IPC 仍是 V10 同步、有界管道：每 worker 同时一个执行，忙时返回错误，登录和关闭会等待该 worker 的当前执行。IPC 单次等待仍有 30 秒上限，设置更长 SQL timeout 不解除此上限。没有新增 Mio 调度、独立取消通道或高并发结论。正常关闭复用现有任务队列；该队列投递失败时仍沿用原有同步清理回退。只验证当前 Linux 环境。

下一步建议：补齐连接控制操作，优先做连接重置和查询取消。前者明确连接池复用时哪些 SQL 状态要清空，后者验证一个连接的取消不会销毁其他连接的 session；写事务仍单独推进。
