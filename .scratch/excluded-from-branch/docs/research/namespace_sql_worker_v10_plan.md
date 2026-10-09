# V10 草案：统一端口与跨平台 SQL worker

日期：2026-09-14。状态：架构目标；[V10 功能原型](namespace_sql_worker_v10.md)已跑通 Linux 的只读进程闭环，但本文的 Mio 双通道、高并发与完整跨平台方案尚未实现。V1–V9 都在一个 seekdb 进程中运行；已有 fork、快照和 GC 验收不能证明进程隔离或 IPC 的性能。

## 目标与最小范围

一个公共 MySQL 端口连接两个固定 namespace 的 SQL-only worker，共用一个事务/存储引擎。先支持已有原型简单表的只读查询，验证 worker 单独退出、另一分支继续查询、重启 worker 后重新连接并读取持久 namespace。

worker 运行真实 SQL 解析、优化和执行路径，拥有自己的 session、schema 和 plan；共享引擎拥有真实事务、存储及其后台线程。沿用 V9 的 namespace 目录、B-tree 和快照语义。SQL-only 启动需要拆开当前初始化依赖，不能每个 worker 都初始化完整存储底座。

## 统一入口与连接归属

共享进程拥有公共端口、客户端 socket、TLS、协议传输状态和有界缓冲。登录时取得 namespace，绑定对应 worker 的激活代次；后续命令在该连接上保持绑定。数据库名仍表示 namespace 内的数据库，`USE` 不切换 namespace。连接池需要按 namespace 区分连接。

namespace 的登录编码尚未定稿。讨论中的 `alice#ns_b` 是候选约定，不是已有能力；需明确转义、现有用户名兼容和默认 namespace。入口向 worker 交付登录信息、握手随机数、原始客户端地址以及 TLS 验证信息，由 worker 按目标 namespace 完成认证。现有原型尚未实现完整分支用户/权限，初期受限账号验证不代表该能力已完成。

共享入口与 worker 之间传递登录、命令和响应；worker 与引擎之间传递扫描、批量数据及事务操作。两类消息使用明确类型和流标识。worker 退出后入口关闭其所属客户端连接，引擎清理所属临时操作；重启创建新会话，持久 namespace 保留。删除 namespace 是独立的持久化操作。

MySQL 使用 TLS 时，含用户名的登录响应在 TLS 建立后发送。因此按登录字段选择 namespace 时，入口已持有 TLS 状态；基础路径采用持续报文转发，不依赖 socket 移交。[MySQL TLS 协议](https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_basic_tls.html)

## 各平台共同遵守的协议

- 每个活跃 worker 初期使用控制、数据两条双向字节通道，多会话通过 session/request/stream 标识复用。连接绑定 namespace、engine epoch、worker 激活代次和协议版本。
- 使用现有序列化基础定义值协议：字段宽度、编码和版本明确；跨进程帧不含 C++ 指针、allocator、函数地址、原生 struct 内存或平台句柄。
- 控制帧与数据帧均有长度上限。统一处理分帧、短读写、EOF、错误和取消；超时预算由接收侧单调时钟计时，不比较不同进程的绝对时钟读数。
- 两条通道间用命令序号和完成屏障表达顺序。未来写事务的 commit 必须等待关联写批次执行完成，不能把网络到达顺序当作事务顺序。
- 所有平台使用同一套按流、按 worker 和引擎全局的请求数/在途字节额度；数据拥塞时控制处理仍有保留容量。
- namespace fork 由共享引擎的快照/引用实现，不依赖操作系统 `fork()` 复制进程内存。基础协议完整运行不要求 Linux 的 epoll 事件位、eventfd、memfd、FD 传递或共享内存布局。

## 平台适配范围

| 平台/部署形态 | worker 生命周期候选 | 本机传输候选 | 事件通知 |
| --- | --- | --- | --- |
| Linux 独立服务、SDK 托管 | `posix_spawn`，观察退出并回收进程 | Unix domain stream socket | 现有 Mio / epoll |
| macOS 独立服务、SDK 托管 | `posix_spawn`，观察退出并回收进程 | Unix domain stream socket | 现有 Mio / kqueue |
| Windows 独立服务、SDK 托管 | `CreateProcessW`，等待退出并关闭句柄 | 双向字节模式 Named Pipe，overlapped IO | 现有 Mio / IOCP |
| Android 独立二进制 | 按实际部署权限适配启动与回收 | 本机 socket | Mio / epoll |
| Android APK/AAR 宿主 | Android Service/JNI 承载 SQL runtime，宿主管理绑定和死亡 | 宿主交付本机通信端点 | native Mio 与宿主生命周期事件协作 |

这里只沿用仓库已存在的 Linux/macOS/Windows/Android 平台范围。构建分支、平台 API 和候选路线均不等于 namespace 功能已经在该平台通过验证。

平台差异集中在 worker 启动/停止/退出观察，以及字节通道建立/收发/关闭。SQL、事务和存储使用稳定身份及生命周期事件，避免扩散平台条件分支。取消使用协议消息，强制终止在平台实现内部完成。

复用当前锁定的 Mio 与可分离传输代码，不为此升级依赖。Mio 提供不同系统事件机制的适配；调用方仍需正确处理 WouldBlock 和实际读写结果，不能仅依赖关闭事件位，也不能绕过 Mio 对其句柄直接做 IO。公平调度或流控让处理提前暂停时，需要记录待继续处理状态，在获得执行机会/额度后主动恢复，不能假设一定会再次收到就绪通知。[Mio 可移植性说明](https://docs.rs/mio/latest/mio/struct.Poll.html)

Windows 基础通道采用字节模式并自行分帧；异步采用 overlapped IO，不能把 Named Pipe 的非阻塞等待模式当作异步实现。[Windows Named Pipe 模式](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-type-read-and-wait-modes)

启动凭据通过专用启动通道交付，内部端点设置相应访问权限，两条通道验证相同的启动绑定。SDK/引擎负责端点发现、等待 Ready、退出回收与匹配的二进制打包；用户只配置公共入口，不管理每个 worker 的内部端点。

Android 的 APK 宿主必须单独验证最低 API、进程并发限制、权限、临时文件/插件资源归属和系统回收。不能把 adb 下启动独立二进制的结果当作应用内多进程支持；Android 对 target API 29+ 应用执行可写应用目录文件有限制，worker 的打包与启动需遵循宿主机制。[Android 执行限制](https://developer.android.com/about/versions/10/behavior-changes-10#execute-permission)

## 资源约束与已知代价

共享侧使用固定数量的 IO 线程管理多个 worker；每个活跃 worker 初期一个 IPC 事件循环，SQL 由有限执行线程处理。IO 回调只收发和派发，不能执行长 SQL、等待锁或同步等待对端。

数据按批次发送，流之间轮转。客户端消费慢时，入口收紧该结果流额度，worker 暂停继续取数，引擎不调度下一批扫描。控制请求保持可处理，排队超限有明确失败路径。每个平台同时统计用户态缓冲与额外的内核/传输适配缓冲。

讨论中的 64 KiB 数据块、8 MiB worker 侧 IPC 数据缓冲仅为待试验预算，不是实测最优值或 worker 总内存。缓冲按需分配；还需计算对端缓冲、内核缓冲、请求元数据、线程栈和 SQL 工作区。

异步 IPC 不会自动使现有同步 SQL 执行器具备挂起/恢复能力。第一步限制实际执行数；大量等待中的查询释放执行线程属于后续适配。活跃 worker 数仍带来进程基础开销，按需启动和满足会话条件后的空闲退出需要独立生命周期规则。

共享内存等大批次传输优化只在瓶颈证据明确后考虑，其区域身份、租约及异常退出回收仍须跨平台实现；基础协议保持可独立运行。

## 实现与验收顺序

1. 先确定统一连接绑定、字节帧和 worker 生命周期，拆出 SQL-only 启动与批量只读扫描。复用现有网络基础，新增真实 IPC 派发，不跨进程传当前 C ABI 指针。
2. Linux 上完成一个公共端口、两个 worker、一个引擎的登录/SELECT/中途终止/重启重连闭环，并验证慢客户端、有限排队和拥塞下取消。
3. 使用同一套协议用例验证 macOS/Windows 的半包、部分写入、断链、流控恢复、启动半途失败及进程回收；在实际平台完成公共入口到真实 SQL 的测试后才标记相应支持。无法访问的平台明确保留待验证状态。
4. Android 独立部署与 APK 宿主分别验收，包含系统回收和重新绑定。SDK 关闭不得误停其他使用者，worker 退出不得误删持久 namespace。
5. 用相同 SQL 对照单进程与拆分实现，记录线程数、缓冲峰值、CPU 和延迟；Linux 结果不能代替其他平台性能结论。只读闭环完成后再扩展写入、提交和回滚。

## 当前代码依据

- [现有构建平台范围](../../CMakeLists.txt)：Linux x86_64/aarch64、macOS x86_64/arm64、Windows x64、Android arm64-v8a 的兼容构建分支。
- [现有传输分支](../../rust/sql-nio/src/transport.rs)：Tcp/Unix/Pipe 和 Mio 注册；当前管道创建方式不能直接作为内部接口权限已满足的证明。
- [现有网络回调](../../src/oblib/rpc/obmysql/ob_sql_nio_server.cpp)：握手、TLS 和进程内 C++ 派发的复用位置。
- [V9 已完成范围与证据](namespace_metadata_gc_v9.md)。
