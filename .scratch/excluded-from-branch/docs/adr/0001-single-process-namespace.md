# 单进程化：namespace 从独立进程改为进程内对象

namespace fork 的第一版架构是"共享存储进程 + 每 ns 一个 worker 进程"（进程天然绑定 ns，模块零感知）。sysbench 实测点查性能仅为单体的 1/7（8t 7384 vs 53231 QPS），IPC 往返与 tx 序列化占火焰图 56.6%，优化到基线后确认**进程边界本身是性能根因**。在"不要故障隔离、不要资源隔离、v1 不跨机"的前提下，我们回到单进程：namespace 成为进程内对象（`Namespace`/`NamespaceRuntime`/`NamespaceRegistry` 三件套），worker 进程模式整体删除。

## Considered Options

- 继续优化 IPC（共享内存、fd 移交、字节流代理调优）：实测优化后仍差一个数量级，且引入多平台兼容负担（AF_UNIX/named_pipe/fd 传递），放弃。
- 单进程 + 模块内感知 tenant_id（OB MTL 老路）：开发难度与维护代价高，违反不变式 2，放弃。

## Consequences

- TLS、多平台 IPC 等 worker 模式的设计负担整体消失（单体 MySQL TLS 直接可用）。
- 性能验收硬门槛：`oltp_point_select` 追平单体；fork→可用 < 1s 并向 100ms 收敛。
