# V14：SQL worker 自动提交 INSERT 原型

状态：Linux 功能原型及回归验收通过。DAS 留在 worker，通过现有 DML/事务接口代理，在共享引擎完成真实写入和提交。

范围限于已有 namespace 原型的两列整数表、普通单语句自动提交 INSERT。沿用共享 IPC、请求路由、取消和有界批次；不新增缓存、线程或第二套事务/存储引擎。真实事务由共享端的请求作用域持有，异常结束时回滚尚未结束的事务。

验收：worker 计算表达式并插入；超过一个 IPC 批次；来源/兄弟分支隔离；跨批次重复主键整条语句回滚；同一连接失败后继续使用；共享引擎重启后仍能读取已提交行。尚不覆盖 UPDATE/DELETE、显式事务、DDL、复杂表或跨平台实机。

## 实现

- worker 绑定 `RemoteDmlService`、`RemoteWriteContext`、`RemoteTransactionService`，原有 SQL 执行器和 DAS 继续运行。共享端只接收事务操作、写入参数和批量行，不重新执行原始 INSERT SQL。
- 复用现有请求路由和共用 IPC，新增 `T` 事务帧、`W` 写入帧、`w` 响应。每批最多 32 行；同一请求仅持有一份真实事务和一个准备好的写入上下文。提交发生在所有写入完成之后。
- 写上下文获取与执行准备合并为一次 IPC。共享端核对 namespace、表、schema 版本、tablet 和列，再调用原生 `ObIDmlService`。SQL mode、日志选项、时区、快照和写入标记保留。
- worker 为现有事务访问函数保留一个 `ObTxDesc` 兼容视图，通过现有值序列化更新；它不在 worker 注册真实事务或启动事务服务。该视图仍是原型适配成本，后续收窄事务接口时可改成紧凑句柄；不是新增事务缓存。
- 原生存储在释放写上下文时才把写入状态合入事务描述符。因此必须在共享端释放完成的响应中更新 worker 视图，SQL 自动提交才能看到事务已发生写入。共享端也检查成功结束时没有未结束的事务，避免“返回成功但实际回滚”。
- `ObResultSet::close(int &)` 的参数用于报告错误，不是设置执行错误；调用前显式 `set_errcode`，让 IPC 错误、超时及结果发送失败参与原生回滚判断。
- 请求结束时释放执行和写上下文，回滚尚未结束的事务，再释放真实描述符。没有新增后台线程或对象缓存。

## 运行

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_sql_worker_prototype.py \
  --binary build_release/src/observer/seekdb --case insert
```

测试自动启动独立临时实例，创建来源与两个分支，执行 SQL、崩溃恢复，结束后停止进程并归档临时数据。默认不带 `--case insert` 仍运行已有只读、会话、并发与超时回归。

## 验证

最终二进制 SHA256：`8b0807f8086f63d6967531af8f96fd4d693e03d6e070c635baa8d4c4dbbf5dc4`。构建日志：`/data/1/tmp/namespace-v14-build-validated.log`。

| 验收 | 结果与日志 |
| --- | --- |
| V14 INSERT、回滚、隔离、崩溃恢复 | PASS；`/data/1/tmp/namespace-v14-insert-validated.log` |
| 原有会话、查询、并发、慢客户端、超时、worker 死亡与重启 | PASS；`/data/1/tmp/namespace-v14-flow-validated.log` |
| IPC 槽位/代次、执行/排队/额度/RPC 取消、重复复用 | PASS，最终 active=0；`/data/1/tmp/namespace-v14-handles-validated.log` |
| 关闭 worker 模式的原生路径 | PASS，20 轮 fork/access/drop、GC、重启，最终元数据页数 0；`/data/1/tmp/namespace-v14-native-validated.log` |
| 脚本语法及 diff 检查 | `py_compile`、`git diff --check` 通过 |

最终 INSERT 实例：`/tmp/namespace_fork_PROTOTYPE_insert_v14_llu9ikzw`。所有集成测试实例均已停止并归档，完成实例的日志另以 gzip 保留。

- SQL 会话变量及算术表达式计算、NULL、负数、调换 INSERT 列顺序、affected rows、其他会话可见。
- 96 行跨多个 32 行 IPC 批次提交。
- 41 行新数据后遇到重复主键，返回 1062；包括前一成功批次的整条语句数据都不可见，连接随后继续 INSERT 成功。
- 0.5 秒查询期限中断包含 SLEEP 的 INSERT，实测约 0.507 秒，数据不可见；同一连接继续使用。
- 同一 worker 的两个会话并发 INSERT，各自提交。
- 来源 A、原始数据库和兄弟 C 保持不变；共享引擎被 kill 后重新启动，B 的已提交行仍存在，恢复后继续 INSERT。

## 边界

仅普通 VALUES INSERT、自动提交、现有两列整数表、root。INSERT IGNORE、REPLACE、ON DUPLICATE KEY UPDATE、INSERT SELECT、UPDATE/DELETE、显式事务、DDL 和复杂表仍不支持。保留已有扫描代理的查询范围限制。

真实跨进程事务对象只有共享端一份，但兼容描述符、请求参数及有界批次会有进程间传输和临时内存成本。本轮没有做内存优化或新增性能结论。沿用现有协作取消；提交期间断链可能造成结果不确定，不自动重放写入。没有新增跨平台实机验证。

后续 [V15](namespace_worker_dml_v15.md) 复用这些接口接通 UPDATE、DELETE 及锁行，并为写语句的扫描传递事务快照。
