# V17：同 session 的原生嵌套 SQL

问题：外层 SQL 与原生 inner_sql 复用 worker 内的同一个 session，真实事务通过 IPC 留在共享进程，能否保留读己之写、语句回滚与外层状态恢复？

共享进程允许发起 SQL；目标架构禁止其执行 SQL 引擎。独立后台内部连接可以代理到 worker。复用活动 session 的调用链放到该 session 所属 worker，保留原生嵌套调用，不作为新的客户端请求排队。

本轮先验证嵌套执行。bootstrap 和独立后台 SQL 的迁移是后续阶段；当前原型的共享 SQL 入口尚未关闭。

实施范围：共享事务从请求移到已有会话绑定；存储执行上下文独立持有；原生 BEGIN/COMMIT/ROLLBACK、保存点与外键 inner_sql；取消时允许原生资源释放和事务清理完成。

验收：显式事务中父子表读己之写、外键失败只回滚当前语句、多层嵌套状态恢复、整事务回滚、其他 session 隔离、断连及 worker 退出后的回滚。使用真实 scratch 数据库，复用现有 Python 集成脚本。

## 调用归属

| 原生调用 | 所有权与迁移要求 |
| --- | --- |
| DML 外键 `ObTableModifyOp::open_inner_conn` | SQL worker 当前 session，原生保存、恢复语句状态 |
| SPI/PL、hybrid search | SQL worker 当前 session，保留执行栈与内部结果集 |
| 动态采样、统计估计、SET/诊断/表命令、表锁 SQL | SQL worker，使用原生 external-session connection |
| 向量刷新、调度任务中的 external-session connection | 随创建该 session 的 SQL 控制逻辑迁移，不能从共享进程传原生指针 |
| bootstrap、独立后台内部连接 | 后续迁移 SQL 控制逻辑或代理独立内部连接；等待结果期间共享存储仍须能够处理请求 |

## 实现

- 真实事务描述符挂到已有 gateway session，`SessionBinding` 持有其存储执行状态。跨请求保留事务；worker 保留原生 SQL session 和事务描述符视图。没有新增按 session ID 查找的请求路径、完整对象缓存或回收线程。
- BEGIN、COMMIT、ROLLBACK、autocommit 和保存点沿用原生同步 driver / 事务控制。事务 IPC 增加对应的原生服务操作；写入上下文按执行句柄持有，嵌套操作不会覆盖外层上下文。
- inner_sql 使用原生 external-session connection。worker 补齐 virtual-table factory 的绑定；内部结果集按 worker 的进程生命周期持有 SQL 模块，不依赖共享进程的 OMT 控制器。原生 `begin_nested_session/end_nested_session` 不作修改。
- namespace schema 同时改写外键所属表、父表、子表和引用对象 ID。原生外键的快速 DAS 检查需要零列存在性扫描，扫描 IPC 保留行数、原生 query flag、外键标志和快照，支持零列结果。
- 已发送的 RPC 先消费其回复；取消后拒绝新业务操作，但允许关闭扫描、释放写入上下文和原生事务清理。存储失败通过对应 RPC 错误回复返回，避免后续清理等待丢失的回复。
- 断连先等待原有 session query lock，再释放事务。worker 通道失效时沿已绑定连接触发原生网络 shutdown，由现有断连任务回滚；IPC reader 不执行 SQL 或同步回滚。

## 已通过的验证

Release 二进制 SHA256：`d434eeaef80a786227ce2810e5a4682ddabce59dc325bc589cb8dc7729301c13`。

`--case nested` 在 `/tmp/namespace_fork_PROTOTYPE_nested_session_v17_k0d095eo/experiment.jsonl` 通过：

- 显式事务先写父表，再写子表，原生外键检查读到未提交父行；另一个 session 不可见。
- 多行插入的外键失败仅回滚当前语句，保留之前成功的语句。
- parent → child → grandchild 的级联删除进入同一 session 的两层原生 inner_sql；leaf 的 RESTRICT 约束使深层执行失败后，外层整条语句回滚，session 的变量和默认数据库恢复。删除限制行后，级联删除和保存点恢复成功。
- 整事务回滚、autocommit=0、COMMIT 可见性和协议事务状态正确。
- 查询超时后之前成功的写入仍能继续提交。
- 断连与 SIGKILL worker 都会回滚未提交事务并释放行锁；检查到原先提交的值恢复，再由新连接更新同一行。

现有 DML 回归也通过，证据 `/tmp/namespace_fork_PROTOTYPE_native_execution_v16_qmzrsyku/experiment.jsonl`：原生重试、并发更新、跨批次语句回滚、类型转换、namespace 隔离和实例重启恢复。IPC 句柄测试通过，证据 `/data/1/tmp/namespace-v17-handles.log`。

完整入口回归通过，证据 `/tmp/namespace_fork_PROTOTYPE_timeout_v13_psqlfvf1/experiment.jsonl`：31 秒长查询、并发 session、重复超时、排队取消、慢客户端、worker 退出和重建。超时后扫描已在请求完成前释放，旧测试中要求析构兜底释放一个扫描的断言改为检查剩余数为零。

INSERT 回归通过，证据 `/tmp/namespace_fork_PROTOTYPE_insert_v14_gyoj_3n6/experiment.jsonl`：批量插入、重复键整语句回滚、取消、并发写入和恢复；新增 `BEGIN; INSERT … SELECT`，确认本 session 可见、其他 session 不可见，ROLLBACK 后消失。

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_sql_worker_prototype.py --binary build_release/src/observer/seekdb --case nested
```

## 范围

本轮证明原生嵌套调用与远程真实事务可以共存，尚未完成 SQL 唯一执行进程的迁移：bootstrap、独立后台 inner_sql 和未标记 namespace 的客户端仍有共享进程 SQL 路径。

表仍限两列整数、单列主键、无二级索引；外键限真实主键引用。PL/SPI、完整 DDL、预处理语句、所有隔离级别/事务错误组合和 Windows/macOS 尚未完整验证。不能把本轮外键闭环等同于全部 worker 功能已接通。

下一步迁移 bootstrap 和独立后台内部连接，再关闭共享进程 SQL 引擎入口，以从空目录启动作为验收；共享进程仍允许发起内部 SQL 请求。
