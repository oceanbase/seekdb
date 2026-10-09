# V16：worker 使用原生同步 SQL driver

已实现并完成 Linux 实测。worker 的手写执行循环已替换为原生同步 driver。namespace SQL 进入对应 worker；共享进程负责接入、真实事务和存储。

```text
客户端 → 共享端口/路由 → namespace worker
                        stmt_query（解析、优化）
                        ObSyncPlanDriver / ObSyncCmdDriver
                        ObQueryDriver（字段、行值转换）
                        ObQueryRetryCtrl（原生重试决策）
                        DAS → data_plane IPC → 共享事务/存储
                        结果 → IPC → 共享端 Rust MySQL 编码器 → 客户端
```

## 改动

- 同步 driver 的发送依赖改用已有 `ObIMPPacketSender`。worker 实现 IPC sender，不再自己 `open/get_next_row/close`，也不再逐项转换 LOB、字符集或 SQL 数据类型。
- IPC 传输现有 `ObMySQLField`、`ObMySQLCellValue` 和 packed row。共享端直接交给原有 MySQL 编码器，不重新恢复 SQL 行、推导类型或转换字段。
- 建会话时一次传入客户端协商能力，原有算子据此处理 `FOUND_ROWS` 等行为。请求外壳绑定已有 session、原始截止时间、warning buffer、schema guard 和 retry info，然后调用原生 driver。是否重试、何时关闭、回滚与提交、开始返回结果后禁止重试，沿用原生实现。worker 没有网络包重排队，因此原生策略选择本地重试。
- 删除 `ObResultSet::start_stmt` 的 namespace 特判。SELECT 也经过原生事务/快照接口，扫描传递 DAS 实际使用的快照。共享端 session 持有该快照的回收保护，直到请求结束。
- 将“准备语句”和“准备自动提交重试”从直接修改描述符的函数收进事务服务接口。worker 发出调用，共享端修改真实事务并回传描述符；复用事务也调用共享端的原生 `reuse_tx`。
- 原生成功终包先暂存，共享端收到 D 并确认事务已结束后才向客户端发送。OK 参数交给原有 `send_ok_packet`，包括 `NO_BACKSLASH_ESCAPES` 等状态位。失败不会因提前生成 OK 而被报告为成功。

事务仍由请求持有，关闭扫描后释放；不引入事务查找表、完整对象缓存或后台清理线程。IPC 中的事务描述符是 SQL 现有访问器需要的状态视图，共享端持有真实事务对象。

## 代码位置

- [worker 请求外壳](../../src/observer/namespace_sql_worker_prototype.ipp)
- [原型范围检查与 IPC sender](../../src/observer/namespace_worker_sql_request_prototype.ipp)
- 结果值传输文件已随旧 Worker IPC 清理删除；本文保留 V16 方案记录。
- [共享端结果转发](../../src/observer/mysql/namespace_worker_query_prototype.ipp)
- [事务和写入代理](../../src/observer/namespace_worker_write_prototype.ipp)
- [扫描代理](../../src/observer/namespace_worker_scan_prototype.ipp)

## 验证

Release 编译通过。最终二进制 SHA256：`a31b0306ff74d3de816b5ad7e639f774923452bc2526d0344f80046f2fc682a6`。

以下测试均在该二进制上通过：

| 测试 | 验收结果 | 本机证据 |
| --- | --- | --- |
| DML | 同行并发更新 17→19、快照冲突更新 20→22；客户端重试均为 0；跨批次回滚、主键修改、超时回滚、隔离和重启恢复 | `/tmp/namespace_fork_PROTOTYPE_native_execution_v16_q9wfz2db/experiment.jsonl` |
| 结果与连接参数（同一 DML 用例） | DECIMAL、DATE、DATETIME、TIME、NULL、中文；NO_BACKSLASH_ESCAPES 状态位；普通连接无变化 UPDATE 为 0，FOUND_ROWS 连接为 1 | 同上 |
| INSERT | 跨批次写入、失败回滚、并发、隔离、重启后再次写入 | `/tmp/namespace_fork_PROTOTYPE_insert_v14_zvezfz01/experiment.jsonl` |
| 完整流程 | 单端口、会话状态、共享 IPC 并发、31 秒查询、反复超时、排队取消、慢客户端、worker 死亡与重新激活 | `/tmp/namespace_fork_PROTOTYPE_timeout_v13_4lw1sv4j/experiment.jsonl` |
| IPC 句柄 | 陈旧代际、并发、执行/等待 credit/等待 RPC/排队取消、反复复用、最终 active=0 | `/data/1/tmp/namespace-v16-handles-final.log` |

测试实例已停止，数据保存在各目录的 `data.tar.gz`，大日志已压缩。

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_sql_worker_prototype.py --binary build_release/src/observer/seekdb --case dml
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_sql_worker_prototype.py --binary build_release/src/observer/seekdb --case insert
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_sql_worker_prototype.py --binary build_release/src/observer/seekdb
python3 tools/obtest/namespace_worker_handles_prototype.py --binary build_release/src/observer/seekdb
```

## 边界与下一步

本轮复用的是原生同步 plan/cmd driver、结果转换与重试控制；没有把 `ObMPQuery` 的网络请求对象、审计、异步提交和所有请求管理逻辑搬入 worker。worker 仍有负责组装上下文的请求外壳。

本轮仅在 Linux 实测，Windows/macOS 尚未验证。原型的数据平面限制仍存在：两列整数表、单列主键、无索引、自动提交、有限的扫描/写入接口。范围检查集中在独立函数，不承担 SQL 执行业务语义。原生 driver 的复用不等于全部 SQL 能力已经接通。

建议下一步接通显式事务：把共享端真实事务的所有权从单次请求移到已有 session 绑定，复用原生 BEGIN/COMMIT/ROLLBACK 流程，验证多语句读己之写、提交可见性和断连回滚。
