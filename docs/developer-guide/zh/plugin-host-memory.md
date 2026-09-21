# Rust 宿主分配器：按模块记账、限额与生命周期

日期：2026-09-10。实现已接入现有 host.alloc/free；生产与集成验证结果另见
[实施进度](plugin-implementation-status.md)。这不是完整模型资源管理或进程内存预算。

## 实际改变

原 C++ 宿主回调忽略 host 参数，直接使用 malloc/posix_memalign/free。现在每个
Module generation 的 HostContext 持有 Rust MemoryAccount 的根引用，现有 alloc/free
签名不变。C/C++ GIS、C 示例和 Rust 插件都可使用这对回调；可选 owned-byte SPI
另行提供不依赖 HostContext 寿命的内存 token。

Rust 实现分配记录、实际分配/释放、并发额度检查、关闭与终止回收。C++ loader
继续掌握 generation、lease、stop/deinit、动态库映射和账户根引用的生命周期；
owned token 独立保留账户，最后一个根/token 引用释放后才销毁账户。
没有第二套插件运行时，也没有将插件的全局 allocator 偷换成 query arena。

未使用账户没有 HashMap 分配表容量；账户自身及同步状态仍有固定成本。只有首次
host.alloc 才建立记录表，没有新增线程池或后台任务。这不是已测量的零开销承诺。

## 额度与状态

`ObPluginLoader::init` 新增可选 `PluginMemoryLimits` 参数，分别限制每个新模块
generation 的请求字节总量和存活分配数。默认 UINT64_MAX 保持原来的无限额准入；
零表示禁止该项分配。生产启动现在通过 ObServerOptions / ObServerPluginRuntime
把这两项传入同一个 loader；未接 SQL SET、动态调整或持久化配置。

```sh
seekdb --plugin-memory-limit=64MiB --plugin-allocation-limit=4096
```

两个参数都接受无符号十进制整数或精确小写 `unlimited`；字节参数额外支持
`KiB` / `MiB` / `GiB` / `TiB` 二进制后缀。默认均为 unlimited，0 明确拒绝
非空分配。Rust 无分配解析器检查十进制及乘法溢出；拒绝空值、正负号、空白、
小数、KB 等模糊后缀、尾部垃圾及超过 64 字节的值。无效参数返回错误，不把
部分解析结果写回选项；分配数参数不接受单位。重复的有效选项沿用 CLI 后者生效。

这些是启动时管理员策略，不由插件 manifest、SQL 调用者或扩展安装脚本修改。
同一 loader 中每个新 generation 各自得到相同上限；多个 generation/模块的
总量仍可能超过它。raw host.alloc 与 owned-byte SPI 共用账户。启用插件的构建
在 runtime 初始化日志中记录生效上限；这条日志不是实时用量。关闭实验插件功能
的构建拒绝显式 CLI 参数，嵌入式 init 也拒绝非默认额度，避免悄悄忽略限制。

```cpp
PluginMemoryLimits limits;
limits.bytes_ = 64ULL * 1024 * 1024;
limits.allocations_ = 4096;
// 在普通的 verifier、catalog guard 和 registry 配置之外传入：
loader.init(directory, verifier, activation_guard, disable_guard, registry, limits);
```

额度检查、插入分配记录、用量变化与实际释放由同一账户锁串行化。分配失败返回
NULL；释放一块内存后才能重新使用相应额度。账户之间独立，不以多个查询各自的
局部上限冒充模块共享预算。这里不调用插件代码、不持有调用者查询上下文。

`ObPluginStatusSnapshot::host_memory_` 和 loader 的状态枚举提供当前字节/分配数、
峰值、分配失败次数、无效释放次数及额度。计数器饱和而不回绕；当前用量不会因
失败或错误释放被扣减。这些字段是进程内 runtime 快照；SHOW PLUGINS 继续展示
持久化 catalog 状态，实时内存另由下述只读虚拟表提供。

该字节额度只统计调用者请求的 payload；分配器开销、对齐开销、记录表、Rust Vec/
String、第三方库、GPU 或 mmap 等不包含在内。记录表容量可随历史并发分配保留到
账户销毁，即使当前 payload 为零也不代表进程 RSS 回到零。它不是租户总预算。

## SQL 实时用量快照

新增 `oceanbase.__all_virtual_plugin_memory`，沿既有 schema 生成、虚拟表工厂和
SQL 扫描路径接入。实现与验证状态见 [实施进度](plugin-implementation-status.md)。

```sql
SELECT plugin_id, generation, runtime_state, lease_count,
       used_bytes, peak_bytes, live_allocations, allocation_failures,
       byte_limit, allocation_limit, sample_time
FROM oceanbase.__all_virtual_plugin_memory
ORDER BY plugin_id, generation;
```

除了正常 SQL 对象访问权限，还需要会话的 PROCESS 权限；没有该权限时在采样前
拒绝，不读取全部模块后再过滤。没有自动授予权限或新的管理员身份。

| 字段 | 含义 |
| --- | --- |
| plugin_id / generation / runtime_incarnation | 区分本进程已装载的模块 generation，不与 extension instance 混用 |
| runtime_state / lease_count | 运行时状态与当前执行引用，不是 catalog desired_state |
| used_bytes / peak_bytes | 显式宿主 payload 当前值及账户峰值 |
| live_allocations / peak_allocations | 当前存活分配数及峰值 |
| allocation_failures / invalid_frees | 账户分配拒绝与无效释放累计次数 |
| byte_limit / allocation_limit | 对应账户的上限；UINT64_MAX 数值表示 unlimited，不转换成有符号负数 |
| sample_time | 本次快照采集完成时间；不表示所有账户同时采样 |

同一次扫描只在首行读取时取得一次拥有独立字符串/数值的快照，不在行间重新读取
runtime；空结果同样只采样一次，采样失败不返回半组行。关闭/重新打开扫描或 reset
释放旧快照，下一次扫描重新取值。它不是事务快照，跨模块、状态、lease 和内存
可能来自略有不同的时刻；同一账户的八项内存计数由 Rust 账户锁一起复制。

采样借用正常 SQL 请求的服务器生命周期，loader 锁保护模块枚举，Rust 锁保护账户；
不调用插件代码。随后扫描不再持有 runtime 指针、动态库或 module lease。现有
停止请求准入/排空后再销毁 runtime 的顺序仍是前提，不为独立后台线程提供新的
访问授权。快照不妨碍模块停用，也不在迭代后续行时访问已销毁的 loader。

每个 loader 仍保留的 generation 可以单独出现，包括逻辑停止但仍驻留的 generation；
未装载的 catalog 包不会凭空出现。禁用实验插件的构建返回空表；启用但 runtime
未初始化时返回错误，不用空表掩盖故障。表中不公开库文件路径或任意错误文本。

没有新增后台采样线程，也不写系统表；内存记账仍由 Rust 实现，C++ 只做既有
SQL/虚拟表适配。结果向量及元数据复制有按模块数增长的临时开销，不计入插件
payload 额度，也不是零开销或全进程泄漏扫描器。脱离当前 loader 的账户、插件
私有堆、模型/GPU 资源和租户预算仍需后续统一资源接口。

## 所有权与释放

alloc 要求非零大小及有效二次幂对齐，并检查机器大小/Layout 上限。free 必须使用
分配时的 host、size 和 alignment；NULL free 不做事。账户按地址查找记录后才处理
释放，不通过读取未知指针前面的 header 猜测所有权。

错误账户、大小/对齐不匹配或未登记地址不会释放内存、不会挪用其他账户额度，记录
无效释放次数。公开 free 返回 void，因此不会伪造查询错误回传；插件仍须遵循接口
契约。地址复用的 ABA、任意悬空指针及 native 内存破坏不是该记账表能解决的问题，
也不把受信任的原生插件变成沙箱。

只有正常 deinit/安全的失败回收之后，loader 才关闭新分配。关闭是幂等的，已有块
仍保持有效，可用正确参数释放；失败 stop 或仍有 lease 时不能提前消费账户。
账户由 HostContext 持有，不跨 generation 复用。

逻辑停用不自动释放尚未归还的块，保留其用量便于诊断。根引用只能在原有宿主调用和
raw 字节用户结束后释放，同时关闭新分配；独立 owned token 可继续保留字节。
最后一个引用释放时回收余留 raw 字节，不调用插件对象析构函数。插件仍须在 deinit
前完成自己的代码/任务清理，raw 内存配对 free；持有字节不等于持有执行权限。
现有“未确认安全则保留整个 loader 域”的规则不变。

## 验证范围

Rust 启动解析用例覆盖十进制、各二进制单位、最大值与乘法溢出、长度上限、
空值/NUL/非 ASCII、错误 kind 和 FFI 输出清理。完整 kernel runner 先以实际
seekdb 二进制执行带额度参数的 --help（解析后退出，不启动监听器），并把真实
CLI 单独按禁用插件的配置编译，检查目标文件没有 Rust 解析符号依赖。
这项目标文件检查不是整个禁用插件版本或跨平台构建的证明。

同一个 kernel 还直接调用真实命令行解析器，检查默认值、单位、零/无限额、
错误输入不覆盖选项，以及带解析结果的 Observer runtime 初始化；C 插件 fixture
用同一 Rust 解析器产生 loader 额度，再检验实际拒绝与回收。初始化使用不读 SQL
的夹具，不能据此声称已验证实库安装/恢复、权限或动态配置。

Rust 用例覆盖空账户、字节与次数上限、额度复用、异常大小/对齐、错误账户释放、
配对参数、重复 free、关闭后保留与释放、空句柄和八线程共享额度。桥接用例校验
C/Rust 大小及字段 offset。

`plugin_host_memory` 使用现有 C SQL extension 的真实表函数 cursor：它通过已有
host.alloc 分配，再通过 host.free 关闭。分别验证默认额度、字节拒绝、并发存活
cursor 的数量拒绝、拒绝后原 cursor 仍能执行、关闭后额度复用及 lease 归零。
catalog 激活许可仍来自 fixture，不能作为数据库事务或性能测试的替代。

## Rust SDK 与真实插件使用

SDK 新增 `memory::HostAllocator<'host>` 与 `HostBuffer<'allocator>`。
`from_raw` 是 unsafe 的 native 接入点：调用者须保证宿主表、账户、回调和模块
执行权限在借用期间有效。构造时校验 v1 前缀、ABI major、owner 和 alloc/free
配对；安全 API 不会凭此自动获取 module lease。

`zeroed` 返回已初始化、可写的定长字节，空缓冲区指针也保持显式请求对齐；`copy_from_slice` 和
`copy_from_slices` 直接复制到一块宿主内存，不经过临时 Vec 或重复清零。空结果
不调用宿主 allocator，因此零额度仍允许空结果；非空额度拒绝返回 NO_MEMORY。
大小求和和 Layout 校验在宿主调用前完成，所有输入借用都不被缓冲区保留。

HostBuffer 借用分配器，通过 slice 访问数据。Drop 始终使用原 owner/size/alignment
归还，包括 Result 提前返回与 unwind-mode panic；不提供 Vec::from_raw_parts、
裸所有权转交或全局 allocator 替换。这组 wrapper 不支持 Send/Sync，不能直接
装进跨线程 cursor 或后台任务；长期/异步资源仍需独立的 owned token 与模块固定协议。

Rust text v14 已将 concat3 的标量和 batch 暂存结果从 Vec 迁到该路径。宿主 API
仅在已经持有执行 lease 的回调内借用，高阶生命周期闭包不允许 allocator/buffer
逃逸；deinit 在回调 drain 后清除宿主指针。AtomicPtr 只负责发布，并非生命周期
保障。批量每行在同步 emit 后释放；原有 UTF-8/NULL/空值和 16 MiB 上限保持不变。
这不是其他 String/Vec 或宿主 emit 暂存内存也已纳入模块额度的声明。

新增 SDK 测试覆盖对齐/初始化、独立副本、空缓冲区、额度失败/重试、异常布局、
错误表、提前失败及 unwind 释放，编译失败用例限制借用逃逸与 Send。
`plugin_rust_host_memory` 通过真实 Rust DSO 验证小额度、空结果、标量结果交付失败、
batch 逐行归还，以及后续行额度拒绝时不交付前面的暂存结果。实际运行结果以
[实施进度](plugin-implementation-status.md) 为准，不以新增测试代码代替通过证据。

## 独立 owned-byte token 与跨回调游标

可选 `memory_spi.h` 提供 Host API v3 后缀：保留 v1/v2 精确前缀，独立协商 memory
SPI major；`allocate_owned_bytes` 输出 data/size/alignment、opaque owner 和 release。
成功返回一份独占字节所有权，失败清空描述符；调用仍须在正常 init/执行权限下借用
host。release 是宿主代码，可跨线程执行，不再访问原 HostContext，也不调用插件代码。
嵌入式场景仍须保持承载 release 的宿主二进制映射直到全部 token 释放。

Rust token 与 raw 分配共用模块额度，但分配记录区分两种释放路径。对 owned 数据
误用普通 host.free 会被拒绝且保留记账；只能消费对应 token。账户通过原子引用计数
保留，创建失败退还新增引用和字节；释放最后一个 token 时销毁已无根的账户。
token 自身/跟踪表等元数据仍不包含在 payload 额度中。

SDK 增加 `OwnedHostBuffer`、`owned_zeroed`、`owned_copy_from_slice`，只在 v3 协议
可用时提供，不会把旧借用指针伪造成 `'static`。返回的初始化字节可离开 allocator
作用域，支持 Send/Sync；写访问仍要求独占 `&mut`。没有 Clone、裸 token 所有权转交
或自动模块 code lease。原有 HostBuffer 保持借用/线程绑定语义。

Rust text v15 的 words 系列表函数把持有的 String 改为 owned 字节；打开时校验 UTF-8
并复制，此后不再修改文本。游标原有执行 lease 固定插件代码，byte token 固定内存
账户，两者分别负责生命周期。正常关闭、错误清理及跨宿主 worker 的串行调用可释放
同一账户中的字节。rescan 先构造替代游标再释放原游标，因此其过渡峰值同样受共享
额度约束，不承诺在仅容纳一份输入的额度下总能原位重扫。

新测试包括根账户销毁后的跨线程释放、最后 token 回收、创建失败引用回退、raw/
owned 共享额度和混用释放拒绝；SDK 包括独立于旧 host/table 寿命、对齐/初始化、
旧 ABI 拒绝、畸形结果清理和 unwind。真实 Rust 游标的跨回调/跨 worker 和额度
测试已加入，执行证据继续以实施进度为准。

这只实现字节所有权，不是后台任务/模型句柄/线程池的资源协议。插件代码、外部连接、
GPU context、取消、任务终止及租户归属还需各自定义；不能借此在 deinit 后执行插件。

后续还需：将模块预算接入管理员配置与观测视图；代码/任务/模型等更高层资源 token；
统一模型/连接池按需初始化、取消、并发和预算；查询/租户层级
计费、spill 与真实轻量化测量。动态库 lazy load、全局 hook preload、可选模块恢复
以及完整 SQL catalog/type/index 协议也仍是原设计目标，不由本模块自动完成。
