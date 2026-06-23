# glibc malloc 内存碎片排查笔记

日期：2026-06-19

## 背景

seekdb 从分布式数据库裁剪而来，当前重点关注单进程启动后的 `GLIBC` context 内存，尤其是 `glibc_malloc` 标签的 used/hold 差值。排查前需要先执行：

```sql
ALTER SYSTEM REFRESH MEMORY STAT;
```

再查询：

```sql
SELECT CTX_NAME, MOD_NAME, COUNT, HOLD, USED, HOLD - USED AS FRAG
FROM oceanbase.GV$OB_MEMORY
WHERE CTX_NAME = 'GLIBC'
ORDER BY FRAG DESC, USED DESC;
```

一次典型观测结果：

```text
CTX_NAME  MOD_NAME       COUNT  HOLD     USED     FRAG
GLIBC     glibc_malloc   12735  6154912  3618507  2536405
GLIBC     Buffer         2942   735552   136632   598920
```

`__all_virtual_malloc_sample_info` 聚合结果：

```text
CTX_NAME  MOD_NAME       stack_cnt  alloc_count  alloc_bytes
GLIBC     glibc_malloc   947        12735        3618507
GLIBC     Buffer         569        2942         136632
```

## 主要来源

### parser non-reserved keyword trie

现象：

- 启动阶段构造 parser non-reserved keyword trie。
- 原始实现中大量小 node 分散使用 glibc 分配，导致 `glibc_malloc` hold 明显偏高。

已处理：

- 将 trie 改为先统计 node 数，再一次性 `calloc` 连续 node buffer 构造。
- 分配次数从约 6842 次下降到 3 次大块分配。

剩余判断：

- 这部分主要是实际常驻内存，不再是大量小块碎片问题。
- 如果继续优化，需要重新设计 trie 数据结构或压缩表达，不属于低风险碎片优化。

### VSAG static initialization / std_hashtable

现象：

- `std_hashtable` 类栈约 71 个 stack group。
- 典型聚合：`alloc_count=4828`，`alloc_bytes=463488`。
- 通过 `nm -anC` 映射 `_GLOBAL__sub_I_*.cpp` 后，来源集中在 VSAG 相关 translation unit，例如：
  - `constants.cpp`
  - `engine.cpp`
  - `diskann.cpp`
  - `hnsw.cpp`
  - `ivf.cpp`
  - `pyramid.cpp`
  - `sindi.cpp`
  - 各类 quantizer / datacell / parameter 文件

原因：

- `deps/oblib/src/lib/vector/ob_vsag_adaptor.cpp` 中存在 VSAG 初始化与 Factory/Options 等引用。
- `ob_vector_util.cpp` 和 `ob_vsag_adaptor.cpp` 在 unity build 下合并为同一个 object：

```text
build_debug/deps/oblib/src/lib/CMakeFiles/oblib_lib.dir/Unity/unity_oblib_lib_ob_vector_util/0_cxx.cxx
```

- 链接命令中 `libvsag_static.a` 和 `libdiskann.a` 在 `--no-whole-archive` 之后，不是被 whole-archive 强行拉入。
- 但因为 `ob_vsag_adaptor` 对 VSAG 符号有真实引用，linker 会从 `libvsag_static.a/libdiskann.a` 拉入相关 object，这些 object 的 C++ 静态初始化会构造 registry / factory / map，产生 glibc 小块分配和碎片。

尝试过但放弃的方向：

- 在 `BUILD_EMBED_MODE` 下复用 `OB_BUILD_CDC_DISABLE_VSAG` 可以让 wrapper no-op，并消除 VSAG 静态初始化趋势。
- 但 VSAG 功能是必要功能，不能删除或禁用，因此该方向已回退。

当前判断：

- `vsag::Allocator` 主要通过 `Factory::CreateIndex`、`SearchParam`、`Resource/Dataset` 等运行期接口传入。
- 头文件中未发现进程级 `set global allocator before init` 一类接口。
- 因此当前可用 allocator 不能覆盖 VSAG 自身静态 registry 初始化阶段的 glibc 分配。

后续可选方向：

- 使用 `libvsag.so` 做 `dlopen` 延迟加载：保留 VSAG 功能，但未使用 vector index 前不加载 VSAG，不运行其静态初始化。
- 修改 VSAG 库自身：将全局 registry 改为懒初始化，或给 registry 使用紧凑 allocator。
- 不建议做全局 `malloc/operator new` wrap，影响面会扩散到 C++ runtime 和其它第三方库。

### OpenSSL / Buffer

现象：

- `GLIBC/Buffer` 一次典型观测：`count=2942`，`hold=735552`，`used=136632`。
- 通过 OpenSSL allocator hook 记录到的 live request bytes 约 89560，peak live count 约 2942。
- live size 分布以小块为主：

```text
24B x 2902 = 69648
48B x 10   = 480
56B x 5    = 280
32B x 4    = 128
16KB x 1   = 16384
```

原因：

- OpenSSL 初始化和后续 EVP/HMAC/X509/SSL 等路径都会经过 `CRYPTO_set_mem_functions` 安装的 allocator。
- 当前 `Buffer` 标签里的 OpenSSL 分配大多集中在启动早期，且一部分长期存活。

为什么不好直接处理：

- OpenSSL allocator hook 是进程级的，不只覆盖初始化，也覆盖后续正常 OpenSSL 业务。
- 不能简单把 free 变成 no-op，否则运行期 OpenSSL 使用会泄漏。
- 如果做专用 allocator，需要支持通用 malloc/free/realloc 生命周期，并区分初始化常驻对象和运行期对象，复杂度偏高。

可选方向：

- 做一个 OpenSSL 专用小对象 allocator，free 回收到 freelist，不直接还给 glibc。
- 只适合在确认 OpenSSL 小块分配模式稳定后实现。
- 当前收益约为几百 KB hold 级别，优先级低于更大来源。

### SQLite

现象：

- `sqlite` 相关栈约 40 个 stack group。
- 典型聚合：`alloc_count=117`，`alloc_bytes=149440`。

判断：

- 属于第三方库初始化/运行期结构。
- 规模不大，且修改 SQLite allocator 影响面较大。
- 暂不作为优先优化项。

### gcc exception handling / libgcc

现象：

- `gcc_eh_alloc` 典型聚合：`alloc_count=1`，`alloc_bytes=72720`。

判断：

- 属于 C++ exception/runtime 支撑结构。
- 规模小且不建议干预。

### Lua

现象：

- `lua_allocator` 典型聚合：`alloc_count=1`，`alloc_bytes=8303`。

判断：

- 规模很小。
- 不值得为碎片单独处理。

## CoStack 碎片补充

日期：2026-06-20

背景：

- `stack_size` 默认已调整到 256KB。
- 实际 CoStack 单个对象约 `236608B used`，在 allocator 中占 `29 * 8KB = 237568B`。
- 一个 2MB chunk 最多放 8 个 CoStack object，尾部固定剩余约 `22 * 8KB = 180224B`。

一次 `memory_dump` 观测：

```text
CO_STACK chunks=13
hold=26.000MiB
used_hold=15.859MiB
free_hold=9.938MiB
costack_objs=70
fully_free_chunks=0
unreleasable_chunks=13
```

结论：

- CoStack 碎片不是完整空闲 chunk 被缓存导致。
- 所有 chunk 都至少有一个 live CoStack object，因此无法整体释放。
- 碎片主要是 2MB chunk 内部的 free block，被少量长生命周期 stack 钉住。

尝试过的 allocator 选择策略：

- 原策略：`BlockSet::get_free_block()` 使用 best-fit，先找最小的 `nblocks >= cls`。
- 尝试 1：只在同 size class 内优先选择 `chunk->using_cnt_` 最大的 free block。
- 尝试 2：在所有可容纳的 free block 中优先选择 `chunk->using_cnt_` 最大的 block，同等情况下保留较小 size class。

验证结果：

```text
原策略:
  chunks=13
  free_hold≈9.94MiB
  unreleasable_chunks=13

同 size class 内优先 using_cnt_:
  chunks=13
  free_hold=11.52MiB
  unreleasable_chunks=13

跨所有可容纳 size class 优先 using_cnt_:
  chunks=11
  free_hold=6.42MiB
  unreleasable_chunks=11
```

判断：

- 跨 size class 的 `using_cnt_` 策略有一定效果，但收益有限。
- 它只能改善后续分配的聚集度，不能移动已经存活的 stack，也不能解决长生命周期线程分散钉住多个 chunk 的问题。
- 该方向不是根本解，若继续优化，优先级应低于减少启动期临时线程数量、线程复用或 CoStack 专用 fixed-slot allocator。

后续方向：

- 追启动期和运行期频繁创建/释放线程的来源。
- 优先关注 `DiskCB`、`IO_SYNC_CH`、`LogIOCb`、`ApplySrv`、`LogSharedQueueThread`、`ReplaySrv`、`DagWorker` 等线程组。
- 如果这些线程属于短生命周期任务线程，优先考虑线程池复用或延迟启动，而不是继续调通用 allocator 策略。

## CoStack 线程创建/释放来源

日期：2026-06-20

现象：

- 使用 `[COSTACK_TRACE]` 统计最近一次启动前 3 分钟，线程创建/释放主要集中在动态队列线程池。
- `DiskCB`：`allocated=75`，`created=75`，`released=77`。
- `IO_SYNC_CH`：`allocated=70`，`created=70`，`released=72`。
- `ApplySrv` / `LogIOCb` / `LogSharedQueueThread`：各约 `17` 次创建和释放。
- 长日志累计中，`IO_SYNC_CH`、`DiskCB`、`ApplySrv`、`LogIOCb`、`LogSharedQueueThread` 的创建和释放次数基本相等，说明这批 CoStack 主要来自线程池动态扩缩容，而不是泄漏。

源码路径：

- `DiskCB`：`src/share/io/ob_io_struct.cpp`，`ObIOCallbackManager::init()` 直接初始化 `ObLinkQueueThreadPool`。
- `IO_SYNC_CH`：`src/share/io/ob_io_struct.cpp`，`ObSyncIOChannel::start_thread()` 直接初始化 `ObSimpleThreadPool`。
- `LogIOCb`：`src/share/ob_thread_define.h`，`TG_DEF(LogIOTaskCbThreadPool, LogIOCb, LINK_QUEUE_THREAD, ...)`。
- `ApplySrv`：`src/share/ob_thread_define.h`，`TG_DEF(ApplyService, ApplySrv, LINK_QUEUE_THREAD, 1, ...)`。
- `LogSharedQueueThread`：`src/share/ob_thread_define.h`，`TG_DEF(LogSharedQueueTh, LogSharedQueueThread, QUEUE_THREAD, ...)`。
- `ReplaySrv`：`src/share/ob_thread_define.h`，`TG_DEF(ReplayService, ReplaySrv, LINK_QUEUE_THREAD, 1, ...)`。

机制判断：

- `ObSimpleThreadPoolBase::push()` 在 `idle_count() == 0` 时调用 `try_expand_one(max_thread_cnt_)`，任务突发时会快速创建 worker。
- `ObSimpleThreadPoolBase::run1()` 在空闲超过 `SHRINK_TIMEOUT_US` 后调用 `try_shrink_one(min_thread_cnt_)`。
- 当前 `SHRINK_TIMEOUT_US = 1s`，`QUEUE_WAIT_TIME = 1s`，因此启动期短突发任务会很快创建线程，又在约 1 秒空闲后收缩。
- `ObSimpleThreadPoolBase::init()` 如果没有提前设置 `min_thread_cnt_`，默认把 `min_thread_cnt_` 设为 `0`。
- `TG_QUEUE_THREAD_IMPL::set_handler()` 只有在 `min_thread_num_ != max_thread_num_` 时才调用 `set_adaptive_thread(min, max)`。因此 `TG_DEF(..., 1, ...)` 这种看起来固定 1 个线程的队列线程池，实际没有把 `min=1` 传下去，最终仍会按 `min=0` 收缩到 0。
- `DiskCB` 和 `IO_SYNC_CH` 不走 `TG_QUEUE_THREAD_IMPL`，它们直接调用 `set_thread_count()` + `init()`，同样没有设置最小线程数，因此默认也会收缩到 0。

结论：

- 启动期临时线程多，不是单纯 allocator 选择策略导致，而是动态队列线程池的默认收缩策略导致。
- 当前策略对短突发任务非常激进：有任务时扩容，无任务 1 秒后缩容，后台 `qth_mgr` 再回收 stopped worker，进而频繁释放 CoStack。
- `using_cnt_` 优先分配只能缓解部分 chunk 聚集问题，不能解决创建/释放频率高导致的 CoStack 分散问题。

可验证方向：

- 对高频线程组设置非 0 的最小线程数，例如 `DiskCB`、`IO_SYNC_CH` 保留少量 worker，避免每次启动突发任务都重新申请 CoStack。
- 修正 `TG_QUEUE_THREAD_IMPL::set_handler()`，即使 `min_thread_num_ == max_thread_num_` 也把 min/max 传给底层 `ObSimpleThreadPoolBase`，避免 `TG_DEF(..., 1, ...)` 被默认 `min=0` 覆盖语义。
- 对直接使用 `ObSimpleThreadPool` / `ObLinkQueueThreadPool` 的路径，在 `init()` 前显式调用 `set_adaptive_thread(min, max)`。
- 如果担心常驻线程数增加过多，可以只对 `DiskCB`、`IO_SYNC_CH`、`ApplySrv`、`LogIOCb`、`LogSharedQueueThread` 做白名单实验，或者把 `SHRINK_TIMEOUT_US` 从 `1s` 提高到更长时间进行对比。

## 已完成的相关改动

- 单机/embed 场景跳过 gRPC 初始化，避免 grpc/protobuf 在不需要时产生额外 glibc 使用。
- parser non-reserved keyword trie 改为连续 buffer 构造，减少大量小块 glibc 分配。
- 增加过临时 malloc/free/stack/OpenSSL allocator 日志，用于定位 `glibc_malloc` 和 `Buffer` 来源。
- 修复栈地址输出，去掉相对偏移，记录原始 PC，便于 `addr2line/nm` 归因。
- 调大 syslog 相关配置，避免排查期间日志被过早刷掉。

## 当前结论

剩余 `GLIBC` 碎片主要来自几个第三方或运行时边界：

- VSAG 静态初始化：功能必要，不能禁用；要优化需要延迟加载或改 VSAG 本身。
- OpenSSL 小块分配：可做专用 allocator，但复杂度和风险都高于当前收益。
- SQLite / gcc EH / Lua：规模较小，不建议优先处理。

因此，短期内不建议继续在 OceanBase/seekdb 外围代码里为这些碎片做低层 allocator hack。更合理的后续方向是：

- 如果目标是减少启动期 glibc 碎片，优先验证 VSAG `dlopen` 延迟加载。
- 如果目标是进一步降低 hold-used 差值，可以单独评估 OpenSSL 小对象 freelist allocator。
- 其它小项先记录，不作为当前裁剪工作的主线。

## 后续任务：删除 tenant unit

日期：2026-06-20

背景：

- seekdb 当前已经不再从 RS/unit table 获取动态 unit 信息。
- `ObUnitInfoGetter::get_server_tenant_configs()` 只 mock 出单 sys tenant、单 unit。
- 运行期真正需要的是从配置派生出的 CPU、memory、log disk、IO 等资源参数。
- 继续保留周期性 tenant unit refresh 会导致无变化时重复写 `OB_REDO_LOG_UPDATE_TENANT_UNIT`，进而触发 server slog IO 和 `DiskCB` callback。

已做的过渡处理：

- 在 `ObMultiTenant::update_tenant_unit_no_lock()` 中增加 no-op 判断。
- 当 `old_unit == allowed_new_unit` 时，不再写 `SERVER_STORAGE_META_PERSISTER.update_tenant_unit()`。
- 配置真实变化时仍保留原有持久化和 replay 语义。

后续目标：

- 移除 seekdb 对 tenant unit 的运行期依赖。
- 将 sys tenant 的 CPU、memory、log disk、IO 等资源值统一改为从配置或配置派生函数读取。
- 保留必要的 tenant meta/replay 兼容字段，或设计迁移方案后删除 `ObTenantMeta::unit_`。
- 最终取消依赖 5 秒 NodeBalancer refresh 来同步 unit 配置的机制。

注意事项：

- 不建议一次性把所有 `tenant->unit_min_cpu()` / `tenant->unit_max_cpu()` 调用点直接改成读配置，改动面大且容易产生资源视图不一致。
- 更稳妥的方案是先引入统一的“配置派生资源快照”接口，再逐步替换 unit 语义。
