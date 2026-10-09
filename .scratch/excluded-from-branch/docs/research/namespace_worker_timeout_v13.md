# V13：复用 SQL 截止时间，按请求取消

日期：2026-09-15。实验分支 `codex/namespace-worker-timeout-v13`，基于 V12 `c15800c72`。

验证问题：去掉查询 IPC 路径写死的 30 秒后，能否遵守现有 `ob_query_timeout`，并在一个请求到期时回收其资源、保留 session 和同 namespace 的其他查询？

## 改动

- 共享端按原生 COM_QUERY 路径的方式，从请求接收时间加 session 的 `ob_query_timeout` 得到截止时间。快照获取、排队、存储扫描、IPC 等待和 worker 执行使用同一截止时间。worker 不再从开始执行时重新计时。
- `NS13` 的 Q/U 消息增加绝对截止时间。同一台机器的进程使用同一个系统时钟；这是现有 SQL 超时使用的时间基准。请求收件槽和发送额度等待都按该时间停止，无需独立的 IPC 超时配置。
- 新增 Z 消息，携带已有请求 slot/generation 和取消原因。worker 收包线程直接标记该请求并唤醒等待者；若任务还在队列中，直接移除并完成，无需等待 SQL 执行线程腾出位置。
- 普通原生线程提供的 `lib::Worker::check_status()` 不检查 SQL 超时。SQL 执行线程使用小型派生类，接入执行器已有检查点，检查本次请求的截止时间、取消标记和原生 session 终止状态。没有异步操作其他 session 的查询状态，也不需要清除遗留的 QUERY_KILLED 标记。执行结束后恢复线程的超时上下文。
- 共享端到期后发送 Z，消费并丢弃剩余普通消息，等待保留的 D 完成消息。此时不再发起新的存储操作；执行任务确认退出后，才释放该请求槽位、扫描和快照。真正的管道/进程故障仍会唤醒整个 channel 的请求。
- D 携带最终 session 标量状态。SET 若恰好在超时前完成，最终状态仍能同步到共享端，避免字符集、数据库或 query timeout 的镜像落后。D 不消耗普通结果额度。
- 慢客户端的现有 NIO 阻塞发送也观察当前查询截止时间：在原请求线程中临时设置一个 TLS 标量，沿用 writer 已有唤醒循环。超时后保留已发送和待发送的数据，追加 MySQL ERR；不读客户端时也能先释放 SQL 执行和扫描。普通查询路径不设置该 TLS 值，行为不变。
- 超时错误的归类同样比较实时钟与传入截止时间。原生 `Worker::is_timeout()` 使用缓存时钟，可能落后于 Rust 发送端；重新用缓存时钟判定曾把已发生的发送超时报成内部错误。

没有增加后台线程、对象缓存或新的队列。仍使用 V12 的执行池、请求槽位和每请求一帧额度。

## 随验证修正的结果转换缺口

大字符串正常读取也曾出现 UTF-8 解码错误。缩小到 `SELECT REPEAT('x',65536)` 后，原生路径正确，worker 路径把四字节内部 LOB 头 `01 84 00 00` 一并发给客户端。原型直接序列化执行器结果，漏掉了原生查询驱动在发包前执行的 LOB 转换。

worker 现复用 `ObQueryDriver::process_lob_locator_results`，再序列化结果；没有自行解析或删除固定长度的头。正常大字符串读取与慢客户端恢复读取均纳入验证。

## 一条命令验证

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp \
python3 tools/obtest/namespace_sql_worker_prototype.py \
  --binary build_release/src/observer/seekdb
```

完整 MySQL 用例在 V12 原有验证上增加：

- `ob_query_timeout=40000000` 下执行 31 秒查询，跨过原 IPC 限制，同时另一连接正常查询。
- 五次 0.5 秒超时打断十秒查询；另一连接正常，原连接立即复用，worker PID 不变，遗留扫描回收。
- 两个执行线程忙时，第三条 SET 在队列中超时，及时返回且未修改 session。
- 客户端停止读取约 6 MiB 结果时，查询按两秒期限终止，扫描回收；恢复读取后得到合法 ERR，原连接可继续查询。

直接 IPC 探针还覆盖执行中、等额度、等存储回复和排队中的取消，以及旧 generation、重复取消和会话复用：

```bash
python3 tools/obtest/namespace_worker_handles_prototype.py \
  --binary build_release/src/observer/seekdb
```

慢客户端问题可独立复现/验证：完整 MySQL 命令末尾添加 `--case slow-timeout`，在同一实例连续执行五轮。

## 验证记录

- [最终离线 release 编译](/data/1/tmp/namespace-v13-build-validated.log)。二进制 SHA-256：`1cf4450e4c0798933b17493a32cf8fce8145da164d0e9a5671f68904ed8fb18e`。
- [完整 MySQL 流程](/data/1/tmp/namespace-v13-flow-validated.log)：PASS。长查询 31.004 秒正常完成；五次短超时约 0.503–0.512 秒，原连接和另一连接均继续使用；队列中取消约 0.501 秒，SET 未执行；慢客户端约 2.177 秒终止并释放扫描，恢复读取后连接复用成功。V12 的并发、session 状态、worker 死亡和旧连接拒绝用例也通过。
- [慢客户端重复验证](/data/1/tmp/namespace-v13-slow-validated.log)：同一实例五轮均通过，约 2.139–2.214 秒回收查询，worker PID 保持不变。
- [直接 IPC 验证](/data/1/tmp/namespace-v13-handles-validated.log)：原生执行检查点取消、额度等待、RPC 等待、排队取消、已过期请求、旧 generation 和重复取消均通过，最终 session active=0。
- [原有路径回归](/data/1/tmp/namespace-v13-native-verified.log)：关闭 worker 开关，20 轮 fork/drop、重启与最终元数据页数为 0，PASS。此项在最后的 namespace 专用错误码归类修正之前完成；之后未修改该原有路径。
- [Rust Clippy](/data/1/tmp/namespace-v13-clippy-verified.log)、Python 语法检查和 `git diff --check` 通过。

完整流程实例 `/tmp/namespace_fork_PROTOTYPE_timeout_v13_paawf75w`，公共端口 24839；重复慢客户端实例 `/tmp/namespace_fork_PROTOTYPE_timeout_v13_2v8533vu`。测试进程均已停止，数据归档为实例目录下的 `data.tar.gz`。完整流程中的两个 worker 各 15 个线程，私有页 19252/19152 KiB，没有继承引擎存储或网络描述符。上述时间是功能用例的计时，不是性能基准。

## 保留的限制

- 30 秒仅保留在 worker 启动握手、登录/关闭/ping 等控制操作的保护路径；不再用于 SQL 收消息或结果额度等待。
- 取消依赖执行器的协作检查点。发送 Z 后等待 D，不再凭另一个固定时长认定 worker 故障。真正卡死且不经过检查点的执行、阻塞的底层管道写入仍需后续处理；不能声称实现了硬实时取消或任意死锁恢复。
- NIO writer 沿用现有一秒唤醒间隔，慢客户端路径的超时响应可能晚于截止时间约一个唤醒间隔。
- 客户端发送失败会触发请求取消；尚未把 NIO 的连接断开事件直接转成 Z。纯计算期间断开连接仍可能等到查询检查/回复或截止时间。
- 当前仍是 V12 的只读、root、简单表原型。没有新增 SQL KILL 命令入口、COM_RESET_CONNECTION、写事务或跨平台实机验收。

## 后续优先级

用户反馈：当前原型先覆盖核心功能，连接断开时立即取消暂时后置。COM_RESET_CONNECTION、回收时效和更多异常场景也不作为下一轮目标。

下一轮优先验证写入闭环。当前 worker 仅支持只读 SQL；已有测试通过共享进程的管理连接修改数据，尚不能证明普通 namespace 连接的跨进程写入可用。

第一个切片限定为已有简单表上的单语句、自动提交 INSERT。SQL 解析、表达式计算和执行仍在 worker；事务、行写入和持久化在共享引擎，通过 IPC 返回提交结果。不能用共享端重新执行原始 SQL 代替该验证，也不在 worker 初始化第二套存储或事务引擎。

验收：通过分支 B 的普通连接插入数据，B 能读到、来源 A 和兄弟分支不变；重启后写入仍存在；多行 INSERT 遇到重复主键时整条语句回滚。先证明正常提交和语句原子性，再沿相同接口扩展 UPDATE/DELETE，之后处理 DDL 和显式事务。

源码调查入口：`ObDASInsertOp` 已通过 `ObIDmlService` 写行；查询事务经 `ObITransactionService`。后续 [V14 INSERT 原型](namespace_worker_insert_v14.md) 已沿这些接口完成普通自动提交 INSERT 验证；范围及证据以 V14 文档为准。
