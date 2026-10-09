---
title: 内存管理
---

# 简介
内存管理是所有大型 C++ 工程中最重要的模块之一。通常，一个良好的内存管理模块需要考虑以下几个问题：

- 易用。设计的接口比较容器理解和使用，否则代码会很难阅读和维护，也会更容易出现内存错误；
- 高效。高效的内存分配器对性能影响至关重大，尤其是在高并发场景下；
- 诊断。随着代码量的增长，BUG在所难免。常见的内存错误，比如内存泄露、内存越界、野指针等问题让开发和运维都很头疼，如何编写一个能够帮助我们避免或排查这些问题的功能，也是衡量内存管理模块优劣的重要指标。

本文介绍 seekdb 当前的内存分配接口、组件额度、诊断方式和内存管理习惯用法。

## 运行时内存预算

`memory_budget` 是用于计算缓存和缓冲区大小的逻辑内存预算。默认值为 `0M`，表示根据 cgroup 限制和物理内存中较小的有效容量自动计算。自动值以 80% 为目标，在条件允许时至少为系统预留 1 GiB，并且不会小于 1 GiB。

显式非零值不得小于 1 GiB。主要派生参数的默认规则如下：

| 参数 | 设置为 `0M` 时的默认行为 |
| --- | --- |
| `kvcache_memory_limit` | `min(1 TiB, memory_budget 的 40%)` |
| `memstore_memory_limit` | `memory_budget 的 50%` |
| `vector_memory_limit` | `effective memory（物理内存与有限 cgroup memory limit 的较小值）的 50%` |

TxShare 统计 Memstore、TxData 和 MDS 的内存用量。`memory_budget` 不超过 6.4 GiB 时，其额度为预算的 `11/16`，其余情况为预算的 `13/16`。Vector 内存不参与 TxShare 的统计和限速；内存申请独立按当前 `vector_memory_limit` 检查，配置 reload 后直接使用新额度。这些独立额度不构成进程总内存上限。

`memory_limit` 仅作为已废弃的兼容参数保留。配置值仍会被接受和持久化，但当前内存计算与控制会忽略它。新配置应使用 `memory_budget`。当前不存在 `memory_reserved` 配置项。

# OceanBase seekdb 内存管理常用接口与方式
seekdb 针对不同场景，提供了不同的内存分配器。另外为了提高程序执行效率，有一些约定的实现，比如reset/reuse等。

## ob_malloc

seekdb 提供 libc 风格的 `ob_malloc`、`ob_free` 和 `ob_realloc` facade。
进程分配器在构建期唯一确定：受支持的 Linux、macOS 非 ASAN 构建使用
Cargo.lock 锁定的 bundled jemalloc；ASAN、Windows 和 Android 使用各自的
平台分配器，不再提供运行时 allocator 切换。

`ObMemAttr` 继续作为源码层分配契约。需要硬额度的组件为对应 ctx ID 注册
自己的 tracker 和 quota；label 以及未注册的 ctx ID 只保留描述作用，不再重建
旧的全进程 label 统计平台。

`ob_realloc` 遵循常规失败语义：非零 resize 失败时原指针仍然有效。
MemoryContext 的 freeable allocation 会记录真实 owner，因此可从另一个 context
handle 调用匹配的 free/realloc。

```cpp
inline void *ob_malloc(const int64_t nbyte, const ObMemAttr &attr = default_memattr);
inline void ob_free(void *ptr);
inline void *ob_realloc(void *ptr, const int64_t nbyte, const ObMemAttr &attr);
```

## OB_NEWx
与 ob_malloc 类似，OB_NEW提供了一套"C++"的接口，在分配释放内存的同时会调用对象的构造析构函数。

## ObArenaAllocator
设计特点是多次申请一次释放，只有reset或者析构才真正释放内存，在这之前申请的内存即使主动调用free也不会有任何效用。
ObArenaAllocator 适用于很多小内存申请，短时间内存会释放的场景。比如一次SQL请求中，会频繁申请很多小内存，并且这些小内存的生命周期会持续整个请求期间。通常情况下，一次SQL的请求处理时间也非常短。这种内存分配方式对于小内存和避免内存泄露上非常有效。在seekdb的代码中如果遇到只有申请内存却找不到释放内存的地方，不要惊讶。

> 代码参考 `page_arena.h`

## ObMemAttr 介绍

seekdb 使用 `ObMemAttr` 来标记一段内存。

```cpp
struct ObMemAttr
{
  uint64_t    tenant_id_;  // 租户
  ObLabel     label_;      // 标签、模块
  uint64_t    ctx_id_;     // 参考 ob_mod_define.h；指定组件会注册 tracker/quota
  uint64_t    sub_ctx_id_; // 忽略
  ObAllocPrio prio_;       // 优先级
};
```

> 参考文件 alloc_struct.h

**tenant_id**

tenant ID 继续作为 `ObMemAttr` 携带的源码级归因信息。allocator facade 不再提供
进程级、按租户的通用统计或 quota 层级；硬额度由下文所述的各组件 quota 管理。

**label**

在最开始，seekdb 使用预定义的方式为各个模块创建内存标签。但是随着代码量的增长，预定义标签的方式不太适用，当前改用直接使用常量字符串的方式构造ObLabel。在使用ob_malloc时，也可以直接传入常量字符串当做ObLabel参数。

**ctx_id**

ctx id 在 `alloc_struct.h` 中预定义。除非组件明确拥有 tracker 或 quota，否则应使用
`DEFAULT_CTX_ID`。KVCache、SQL WorkArea、Vector 和 Meta Object 分别拥有自己的
admission、wash/spill/GC 和错误策略；共享 quota 原语只负责原子的
reserve/reconcile/rollback 记账。

## 组件内存诊断

`V$OB_COMPONENT_MEMORY`（底层表为
`__all_virtual_component_memory_stat`）在每个 server 固定返回
`KV_CACHE`、`SQL_WORKAREA`、`VECTOR`、`META_OBJECT` 四行。八个字段为
`SVR_IP`、`SVR_PORT`、`COMPONENT_NAME`、`LIMIT_BYTES`、
`COMMITTED_BYTES`、`RESERVED_BYTES`、`REJECT_COUNT` 和
`RECLAIM_COUNT`。

数值字段分别进行原子采样，同一行没有多字段联合线性化点，不能当作事务一致快照。
该表用于容量趋势和组件归因；需要验证严格不变量时，必须先停止并 join 组件 worker、
等待回收完成，并确认没有 in-flight reservation。

`REJECT_COUNT` 统计每次失败的 quota reserve/reconcile 尝试，包括随后被组件重试
挽救的尝试。`RECLAIM_COUNT` 统计实际回收了至少一个字节或对象的成功
wash/spill/cleanup/GC 批次；失败尝试只记录日志，不计入该字段。对 SQL
WorkArea，一次成功的正向物理 spill 写回调计为一次；后续负向统计调整
不增加该计数。

obmalloc 专用的 `V$OB_MEMORY`、`__all_virtual_memory_info`、
`__all_virtual_ctx_memory_info`、`__all_virtual_malloc_sample_info`，以及
`DUMP ENTITY`、`DUMP CHUNK`、`ALTER SYSTEM REFRESH MEMORY STAT` 命令已删除。
Vector 的 `RAW_MALLOC_SIZE` 兼容列保留为 deprecated，并返回 `NULL`；Vector
汇总改读组件 tracker。

bundled jemalloc 构建只把 `MALLOC_BACKEND=jemalloc` 接受为 deprecated no-op；
`MALLOC_BACKEND=obmalloc`、未知值，以及平台 allocator 构建中的任意非空值，都会在
启动早期失败。以下参数继续支持加载和持久化，但为无效果的兼容项：
`cache_wash_threshold`、`memory_chunk_cache_size`、
`_min_malloc_sample_interval`、`_max_malloc_sample_interval`、
`_ctx_memory_limit`、`_enable_memleak_light_backtrace`。

**prio**

当前定义了 Normal 和 High 两种内存分配优先级，默认为 Normal。具体定义参见 `alloc_struct.h` 中的 `enum ObAllocPrio`，精确行为以当前分配器实现为准。不要再使用 `memory_reserved` 配置项解释高优先级路径，因为当前配置面中不存在该参数。

## init/destroy/reset/reuse

缓存是提升程序性能的重要手段之一，对象重用也是缓存的一种方式，一方面减少内存申请释放的频率，另一方面可以减少一些构造析构的开销。seekdb 中有大量的对象重用，并且形成了一些约定，比如reset和reuse函数。

**reset**

用于重置对象。把对象的状态恢复成构造函数或者init函数执行后的状态。比如 `ObNewRow::reset`。

**reuse**

相较于reset，更加轻量。尽量不去释放一些开销较大的资源，比如 `PageArena::reuse`。

seekdb 中还有两个常见的接口是`init`和`destroy`。在构造函数中仅做一些非常轻量级的初始化工作，比如指针初始化为`nullptr`。

## SMART_VAR/HEAP_VAR
SMART_VAR是定义局部变量的辅助接口，使用该接口的变量总是优先从栈上分配，当栈内存不足时退化为从堆上分配。对于那些不易优化的大型局部变量（>8K），该接口即保证了常规场景的性能，又能将栈容量安全地降下来。接口定义如下：

```cpp
SMART_VAR(Type, Name, Args...) {
  // do...
}
```

满足以下条件时从栈上分配，否则从堆上分配
```cpp
sizeof(T) < 8K || (stack_used < 256K && stack_free > sizeof(T) + 64K) 
```

> SMART_VAR 的出现是为了解决历史问题。尽量减少大内存对象占用太多的栈内存。

HEAP_VAR 类似于 SMART_VAR，只是它一定会在堆上申请内存。

## SMART_CALL
SMART_CALL用于"准透明化"的解决那些在栈非常小的线程上可能会爆栈的递归函数调用。该接口接受一个函数调用为参数，函数调用前会自动检查当前栈的使用情况，一旦发现栈可用空间不足立即在本线程上新建一个栈执行函数，函数结束后继续回到原始栈。即保证了栈足够时的性能，也可以兜底爆栈场景。

```cpp
SMART_CALL(func(args...))
```

注意：
1. func返回值必须是表征错误码的int类型
2. SMART_CALL会返回错误码，这个可能是内部机制的也可能是func调用的
3. 支持栈级联扩展，每次扩展出一个2M栈（有一个写死的总上限，10M）

SMART_CALL 相对于直接调用多了 `check_stack_overflow` 栈移除检查。
