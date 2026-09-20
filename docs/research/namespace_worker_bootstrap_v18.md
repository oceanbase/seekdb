# V18：bootstrap 与独立内部 SQL 迁移

验证问题：共享进程禁止执行 seekdb SQL 引擎后，原生 bootstrap 和独立内部连接能否通过 SQL worker 完成空目录启动与崩溃重启？

已验证完整启动、重启、普通端口查询和显式事务，并验证单条 SQL 执行线程配置。共享端仍然能够发起 SQL；执行进入基础 namespace（1）的 worker。SQL 公共检查入口和 PL 入口在实验模式下拒绝共享进程执行 SQL，不回退到共享 SQL 引擎。

## 调用与所有权

- 共享端沿用 `ObInnerSQLConnection` 的读、写、BEGIN、COMMIT、ROLLBACK、会话变量设置接口。已有连接继续持有真实 session 和事务描述符，直接登记 MDS 与 SQL 写入仍属于同一事务。
- 新增 `I` 请求经已有 IPC 进入 worker，worker 调用原生内部连接。worker 内复用 session 的嵌套 SQL 保留原生调用栈，不再经过 IPC，也不经过 MySQL 协议。
- 内部读结果按原生类型逐行返回；调用者的 `next/close` 驱动已有 IPC 请求，等待结果时同一调用线程继续服务存储和 schema RPC。结果持有连接 guard，先结束查询、再释放连接。没有新增内部结果线程或完整结果缓存。
- worker 的表和数据库 schema 副本归当前原生 schema guard 所有，随 guard 销毁，不增加进程级 schema 缓存。

内部调用可能在读结果尚未关闭时再发起查询。worker 在等待信用或 RPC 响应期间可以执行排队的内部请求，每个请求保存并恢复执行上下文与路由；通过条件变量唤醒，不新增线程。嵌套调度暂限 8 层，超限返回错误。单执行线程探针曾因此超时，补齐后启动、系统表查询和重启均通过。

## 接口补齐

系统表使用原生 schema 与存储接口。扫描与写入传递类型值、复合主键和通用列集合，补齐原生 `put_rows`、`insert_rows_fetch_duplicates` 和扫描复用。重复键检测、冲突行查询、更新及事务回滚由原生执行器处理。

配置命令在 worker 解析执行，`admin_set_config` 的类型参数经 IPC 调用共享端已有管理服务。配置持久化仍由共享端原生配置存储负责。虚拟表扫描接入相同扫描通道，共享端生成真实虚拟表数据，过滤与聚合仍在 worker。

bootstrap 的批量 schema INSERT 超过原来的 256 KiB 帧上限。现在仅 SQL 输入 `Q/I` 允许最多 64 MiB，按实际长度分配；结果与存储帧仍为 256 KiB，每批最多 32 行，没有预分配 64 MiB 缓冲区。

验证过程中还修复了两处结果适配错误：写入表达式使用原生 `locate_datum_for_write`；内部结果的整数读取接受所有原生有符号整数类型。扫描返回一个向量批次期间保留其字符串数据，避免跨帧覆盖。

## 复现

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp python3 tools/obtest/namespace_worker_bootstrap_prototype.py \
  --binary build_release/src/observer/seekdb
```

脚本自动设置两个实验开关：`SEEKDB_NAMESPACE_FORK_PROTOTYPE=6`和 `SEEKDB_NAMESPACE_SQL_WORKER_PROTOTYPE=1`。Worker 模式本身就禁止共享进程执行 SQL，不再需要独立的 bootstrap 开关。脚本创建独立临时目录和端口，验证后停止实例并归档数据。

在命令前加 `SEEKDB_NAMESPACE_SQL_WORKER_THREADS=1` 可以复现单执行线程验证。

探针检查 bootstrap 完成标记、SELECT、BEGIN/COMMIT、6 个系统数据库、719 个系统变量及过滤结果、配置虚拟表；随后杀掉共享进程并从同一目录重启，重复验证。整个过程中检查没有 `PROTOTYPE_V18_SHARED_SQL_REJECT`。

## 验证记录

2026-09-15，Linux，二进制 SHA-256：`e45eaae9746733972fa53268aaf0b3e42e29d00e863c0e1fe54c435f6b5c4f29`。

- 空目录启动、崩溃重启、公共端口与系统表探针：通过。默认两线程目录 `/tmp/namespace_fork_PROTOTYPE_bootstrap_v18_rmnaosv_`，日志 `/data/1/tmp/namespace-v18-bootstrap-final.log`；单线程目录 `/tmp/namespace_fork_PROTOTYPE_bootstrap_v18_w5rii1tq`，日志 `/data/1/tmp/namespace-v18-bootstrap-one-thread.log`。
- 嵌套 SQL 与 session 复用：通过。最终二进制重跑日志 `/data/1/tmp/namespace-v18-nested-final.log`。
- 完整 IPC 回归：通过，覆盖多会话并发、慢客户端、超过 30 秒的查询、超时取消、worker 崩溃与重启。最终二进制日志 `/data/1/tmp/namespace-v18-full.log`。
- UPDATE/DELETE/事务恢复：通过。日志 `/data/1/tmp/namespace-v18-dml.log`。
- INSERT、超时/重复键回滚、UPSERT 的事务隔离及回滚：通过。原来期待 UPSERT 不支持的用例改为正向验证；IGNORE、REPLACE 仍保留既有实验限制。日志 `/data/1/tmp/namespace-v18-insert.log`。
- IPC 句柄复用、并发、执行中/排队/等待信用/等待 RPC 时取消：通过。日志 `/data/1/tmp/namespace-v18-handles.log`。
- Rust 大 SQL 输入及结果帧上限测试：通过。日志 `/data/1/tmp/namespace-v18-rust-test.log`。

专门的 DML、INSERT 和句柄探针在补齐内部请求协作调度之前的 V18 二进制上通过；最终二进制重新运行了上述启动、单线程、嵌套 SQL 和完整 IPC 回归。Python 语法检查、Rust 格式检查和 `git diff --check` 均通过。

## 原型边界与下一步

这证明了本次启动链路能够通过 worker 执行，尚不代表全部 worker 功能已经完成。客户端仍限定 root；通用 DDL、二进制预处理协议、其他管理接口、LOB 与更多扫描能力仍需继续接入、验证。共享内部连接的全部特殊接口与会话上下文也尚未宣称覆盖。

保留实验的请求、扫描句柄、批次和帧大小限制；当前最多启动两个 worker，严格模式下基础 namespace 占用其中一个。已有 namespace 回归使用其原有模式；严格禁止共享执行 SQL 的端到端验证由新的 bootstrap 探针承担。未进行 Windows/macOS 实机验证，未优化模块缓存或内存预算。

下一步接入原生建库、建表、建索引的管理接口，验证严格模式下从 DDL 到读写的完整流程；管理回调如果再发起内部 SQL，需要在已有内部请求调度机制上验证新的嵌套路径及执行线程占满时的进度。
