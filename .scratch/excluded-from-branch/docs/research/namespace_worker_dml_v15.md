# V15：复用数据平面接口接通 UPDATE / DELETE

状态：Linux 功能原型及回归验收通过。

本轮沿用 V14 的真实事务、写上下文和共享 IPC，补齐 `update_rows`、`delete_rows` 和普通 UPDATE 也会调用的 `lock_rows`。SQL 解析、表达式计算、DAS 算子和 SQL 事务控制继续在 worker 执行；共享引擎调用原生存储接口，不解析或重放 UPDATE/DELETE SQL。

## 实现

- `RemoteDmlService` 的 INSERT、UPDATE、DELETE、锁行共用一个批量传输函数和同一个准备好的执行上下文。每帧最多 32 次行操作；UPDATE 传旧行、新行对及更新列 ID。共享端保留两个交替的行缓冲，避免取新行时覆盖旧行。
- `lock_rows` 允许只传主键列，这是未改变值的 UPDATE 所需的原生行为。完整 schema 仍限于 namespace 原型已有的 `id/v` 两列整数表。
- 主键更新在本次测试中由原有 DAS 拆成 DELETE + INSERT；同一批量代理直接处理这些底层调用，无需新增“主键 UPDATE”语句逻辑。
- 扫描传递现有 DAS 参数中的事务 ID、快照、read-latest 标志、锁等待期限及序号。共享端核对请求持有的真实事务，绑定本进程的描述符；纯 SELECT 沿用 gateway 固定的读快照。
- 原生 SQL 仍负责保存点、受影响行数、语句回滚及自动提交。写上下文释放后更新 worker 的事务兼容视图，复用 V14 的提交与回滚流程。
- 请求异常结束时先释放扫描，再释放写上下文、回滚及释放真实事务。没有新增事务表、对象缓存、线程或另一套存储引擎。

## 运行

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_sql_worker_prototype.py \
  --binary build_release/src/observer/seekdb --case dml
```

测试使用独立临时实例；结束后停止进程并归档数据。`--case insert` 保留 V14 INSERT 回归，不指定 case 运行原有查询、会话、IPC 并发及取消回归。

## 验证

最终二进制 SHA256：`7209a9722da3d9728de81be1e4ea68ad824345ac84278a169de5990fcc925a1c`。构建日志：`/data/1/tmp/namespace-v15-build2.log`，无编译错误或警告。

| 验收 | 结果与日志 |
| --- | --- |
| UPDATE/DELETE、锁行、回滚、隔离、崩溃恢复 | PASS；`/data/1/tmp/namespace-v15-dml-final.log` |
| INSERT、跨批次失败回滚与恢复回归 | PASS；`/data/1/tmp/namespace-v15-insert-validated.log` |
| 原有查询、会话、共享 IPC 并发、超时和 worker 重启 | PASS；`/data/1/tmp/namespace-v15-flow-validated.log` |
| IPC 槽位、代次、取消及重复复用 | PASS，最终 active=0；`/data/1/tmp/namespace-v15-handles-validated.log` |
| 关闭 worker 模式的原生生命周期路径 | PASS，20 轮 fork/access/drop、GC、重启，最终元数据页数 0；`/data/1/tmp/namespace-v15-native-validated.log` |
| 脚本语法及 diff 检查 | `py_compile`、`git diff --check` 通过 |

- SQL 变量、算术、NULL、负数、主键及普通列过滤；无匹配行和未改变值的 UPDATE 返回 0；同一 UPDATE 混合修改与锁行只计实际改变的行。
- 96 行普通列 UPDATE 使用 3 个批次，96 行主键更新复用原有 DAS 改写；随后删除 64 行。
- 主键更新在前一成功批次之后遇到重复键，返回 1062；整句删除与插入均回滚，旧数据完整保留。
- 0.5 秒 DELETE 超时后数据未删，连接可以继续使用。
- 两个会话修改同一行，冲突请求明确失败并回滚；检查已提交增量，再由客户端重试。先读后写的旧快照同样返回冲突，不覆盖新值。
- 来源 A、原始数据库及兄弟 C 保持不变；共享引擎被 kill 后重新启动，B 已提交的 UPDATE/DELETE 结果保留，恢复后继续修改和删除成功。

最终 DML 实例：`/tmp/namespace_fork_PROTOTYPE_dml_v15_7s7nkem6`。所有集成测试实例已停止并归档数据，日志另以 gzip 保留。

## 边界

已验收范围为 root、两列整数表、单语句自动提交的普通 INSERT / UPDATE / DELETE。IGNORE、显式事务、DDL、复杂表及更广泛 SQL 组合不在本轮验收范围。沿用已有扫描及协作取消限制，不自动重放提交结果不确定的请求。本轮不新增性能或跨平台实机结论。

worker 尚未接入 MySQL 入口的语句重试循环：锁冲突返回 6005，先读后写期间其他事务已提交则返回 6001。原生存储检查及失败回滚保留；验收检查失败请求没有覆盖已提交数据，再由客户端明确重试。不能把此版本视为已经具备原生入口的透明并发重试能力。

## 下一步建议

已按后续讨论调整为 [V16：复用原生 SQL 请求执行链](namespace_worker_execution_reuse_v16.md)。让 worker 完整复用原生请求执行、结果处理、重试与收尾代码，再在数据平面接口处补齐能力。目标架构的 namespace SQL 统一进入 worker；迁移期间保留旧执行路径作为回归对照。自动重试作为执行链复用的验收项目，显式事务随后接入。
