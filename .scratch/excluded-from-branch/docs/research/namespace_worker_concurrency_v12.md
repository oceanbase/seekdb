# V12：同一 SQL worker 的有限并发

日期：2026-09-15。实验分支：`codex/namespace-worker-concurrency-v12`，基于 V11 的连接级 session。

后续：[V13 查询截止时间与请求取消](namespace_worker_timeout_v13.md) 已接上现有 SQL 超时，本文保留 V12 的实现和验证记录。

本轮验证：多个连接共用一组进程管道时，能否让慢查询、存储回调和慢客户端各自等待，同时让另一 session 继续执行，并保持 session/扫描的生命周期正确。

## 实现

共享进程仍保有公共 MySQL 端口，每个 worker 使用一组 stdin/stdout 管道。帧头改为 `NS12`，负载包含消息类型、请求 slot 和 generation；初始 SQL 消息另外带 V11 的 session 句柄和当前查询快照。请求句柄与 session 句柄分开，一个表示本次执行，一个表示持久连接状态。

```text
共享进程的请求线程                       SQL worker
  A：自己的快照、ReadScans ──┐        ┌─ 执行线程：session A
                            ├─ IPC ──┤
  B：自己的快照、ReadScans ──┘        └─ 执行线程：session B
               ↑                           ↑
       原有 Rust 读取线程分发          主线程统一收包和分发
```

- 共享端直接由已有 Rust 管道读取线程回调 C++ 分发函数，不增加中转线程。回调只按请求 slot/generation 定位待处理请求、投递一帧、唤醒等待者；不会运行 SQL、扫描或客户端写入。
- 共享端的原有请求执行线程继续持有自己的快照、`ReadScans` 和 MySQL packet sender。收到该请求的 schema/扫描消息后，在原线程处理并回包，因此没有把这些线程上下文交给 Rust 收包线程。
- worker 主线程统一读管道。SQL 和登录初始化交给固定数量的原生执行线程；schema 和扫描的同步调用等待各自请求的收件槽，不能再直接读管道。原生线程初始化各自的 `THIS_WORKER`，SQL 继续使用 V11 的真实执行器。
- 不同 session 可以并发；同一 session 一次只接受一个执行，重复并发提交返回 `OB_EAGAIN`。每次接收 SQL 时定位并引用一次 session，执行期间使用该稳定指针。
- session slot 中使用引用计数拥有者。关闭时移除 slot 中的引用并增加 generation，已经受理的查询保有自己的引用；即使 slot 被复用，旧查询也不会操作新 session。
- Rust 句柄收发使用共享借用及各自的锁，发送锁只覆盖一帧。终止时先杀掉并回收子进程、唤醒请求，再等待 Rust 回调退出，最后释放句柄；不会在回调仍借用对象时创建独占所有权。

## 有界等待

每个在途请求初始允许 worker 发送一帧。共享端取走该帧后归还一份额度；worker 再发送下一帧。额度耗尽时，只有该执行线程等待，统一收包器继续处理其他请求。每个请求另有一个完成帧位置，异常结束不会因为数据槽已满而无法发送 `D`。

客户端读得慢时，沿用现有 Rust MySQL writer 的分批发送及阻塞等待。该请求消费 IPC 帧变慢，其额度随之耗尽。请求不会在分发器里无限积攒结果，也不会让分发器等某个客户端腾出空间。关闭和 ping 在 worker 主线程处理，无需等待 SQL 执行线程空闲。

请求表按需增长并复用槽位，不做哈希查找，也不按最大连接数预分配。当前原型每个 worker 最多 32 个在途请求，普通 SQL 最多占 31 个，留出控制操作的位置；控制请求之间仍可能需要等待。若关闭请求最终无法送达，会使该 worker 激活失效，避免留下无法再释放的远端 session。

单帧仍限制 256 KiB。这是帧和在途请求的上限，不是提前分配的缓冲，也不是总进程内存上限；总量还包含正在处理/编码的帧、SQL 工作区、原有 MySQL 缓冲和内核管道缓冲。本轮没有增加对象缓存或复制 session/schema 缓存。

## 运行与验收

使用本分支编译的二进制，在仓库根目录执行：

```bash
SEEKDB_FORK_PROTOTYPE_TEST_ROOT=/tmp \
python3 tools/obtest/namespace_sql_worker_prototype.py \
  --binary build_release/src/observer/seekdb

python3 tools/obtest/namespace_worker_handles_prototype.py \
  --binary build_release/src/observer/seekdb
```

worker 默认两个 SQL 执行线程。原型可用 `SEEKDB_NAMESPACE_SQL_WORKER_THREADS=2` 配置，接受 1–8；变量由共享进程传给子进程。它是实验入口，不是最终产品的资源配置方案。32 是在途请求上限，连接 session 数可以大于该数。

MySQL 脚本覆盖 V11 变量、字符集、数据库切换、槽位回收及旧连接拒绝，并增加同 worker 慢/快查询、16 次交错表扫描、两条在途查询同时遭遇 worker 强杀，以及不读取约 6 MiB 结果的 TCP 客户端。IPC 探针覆盖有限并发、同 session 重复执行拒绝、不给某请求归还额度时其他请求和关闭仍能进行、在途关闭/复用，以及旧 generation 的额度不能唤醒新请求。

本轮验收全部通过：

- [离线 release 编译](/data/1/tmp/namespace-v12-build-dispatch.log)，二进制 SHA-256：`46ac8d7e45ba90ee3a4e98457b53404f5a61eefabcf6b7f18843ab78f4c37fa9`。
- [真实 MySQL 流程](/data/1/tmp/namespace-v12-flow-verified.log)：同 namespace 的 `SLEEP(3)` 查询尚未完成时，另一连接的点查在约 7.6 ms 内返回；16 次交错扫描结果正确。客户端停止读取约 6.1 MiB 结果时，另一连接的聚合查询约 7.3 ms 返回；断开慢客户端后 session 回收，worker 继续可用。两条查询同时遭遇 worker 强杀时都及时失败，另一个 namespace 继续读表，新 worker 的旧连接拒绝测试通过。以上为功能用例中的单次计时，不是性能压测。
- [IPC 探针](/data/1/tmp/namespace-v12-handles-verified.log)：并发完成顺序、同 session 重复执行拒绝、额度阻塞期间控制操作继续、在途关闭/复用、旧请求 generation 不能授予新请求额度等断言通过，最终 active=0。
- [关闭 worker 开关后的原有路径回归](/data/1/tmp/namespace-v12-native-verified.log)：20 轮 fork/访问/drop、重启与最终元数据页数为 0，PASS。
- [Rust Clippy](/data/1/tmp/namespace-v12-clippy-verified.log)：离线 release `cargo clippy -- -D warnings` 通过；Python 语法检查和 `git diff --check` 通过。

真实流程实例 `/tmp/namespace_fork_PROTOTYPE_concurrency_v12_nvbzeu78`，公共端口 54185；回归实例 `/tmp/namespace_fork_PROTOTYPE_metadata_gc_v9_lifecycle_6t7ft9vl`。实验进程均已停止，数据保存在各自的 `data.tar.gz`。两个 worker 各 15 个线程（包含本轮两个 SQL 执行线程），私有页约 20.2/18.5 MiB；沿用的小数据私有页小于 64 MiB 检查通过。本轮没有重新做一体化内存对照。

## 保留的限制

这是有限并发的只读功能原型，SQL 执行线程仍会在等待存储或结果额度时占用线程。执行线程用完后，后续任务在有上限的队列中等待；共享端的请求执行线程也仍在同步等待。尚未实现 SQL 挂起/恢复、Mio IPC、取消 SQL 或多流公平调度，不能据此推导海量并发性能。

IPC 等待保留 30 秒上限。worker 等结果额度超时可结束该请求；共享端等待协议进展超时仍会使整次 worker 激活失效。完整的超时、取消和故障隔离协议仍待补齐。

本轮保持 V11 的 root、只读 SQL、简单表和结果类型范围，未增加写事务或连接重置。worker 的 schema 副本、512 MiB 实验预算、最多两个 namespace 激活和空闲进程退出问题仍按之前的范围保留。只在当前 Linux 环境验收，跨平台实际运行仍待验证。

下一步建议：补请求级取消与超时，让一条查询超时能释放自己的执行槽位和扫描，而无需终止整个 worker。
