---
title: 内存管理
---

# 简介
内存管理是所有大型 C++ 工程中最重要的模块之一。seekdb 的内存管理需要同时兼顾进程级分配、业务缓存预算以及不同生命周期对象的回收。通常，一个良好的内存管理模块需要考虑以下几个问题：

- 易用。设计的接口比较容器理解和使用，否则代码会很难阅读和维护，也会更容易出现内存错误；
- 高效。高效的内存分配器对性能影响至关重大，尤其是在高并发场景下；
- 诊断。随着代码量的增长，BUG在所难免。常见的内存错误，比如内存泄露、内存越界、野指针等问题让开发和运维都很头疼，如何编写一个能够帮助我们避免或排查这些问题的功能，也是衡量内存管理模块优劣的重要指标。

内存预算、KV cache、meta pool 和 MemoryContext 等业务组件分别维护自身的容量或生命周期策略；进程级分配器本身不提供按 tenant/context 的硬隔离。

本篇文章将会介绍seekdb 中常用的内存分配接口与内存管理相关的习惯用法，关于内存管理的技术细节，请参考[内存管理](https://open.oceanbase.com/blog/8501613072)(中文版）。

## 运行时内存预算

`memory_budget` 是用于计算缓存和缓冲区大小的逻辑内存预算。默认值为 `0M`，表示根据 cgroup 限制和物理内存中较小的有效容量自动计算。自动值以 80% 为目标，在条件允许时至少为系统预留 1 GiB，并且不会小于 1 GiB。

显式非零值不得小于 1 GiB。主要派生参数的默认规则如下：

| 参数 | 设置为 `0M` 时的默认行为 |
| --- | --- |
| `kvcache_memory_limit` | `min(1 TiB, memory_budget 的 40%)` |
| `memstore_memory_limit` | `memory_budget 的 50%` |
| `vector_memory_limit` | `memory_budget 的 50%` |

`memory_limit` 仅作为已废弃的兼容参数保留。配置值仍会被接受和持久化，但当前内存计算与控制会忽略它。新配置应使用 `memory_budget`。当前不存在 `memory_reserved` 配置项。

# OceanBase seekdb 内存管理常用接口与方式
seekdb 针对不同场景，提供了不同的内存分配器。另外为了提高程序执行效率，有一些约定的实现，比如reset/reuse等。

## ob_malloc

`ob_malloc`、`ob_free` 和 `ob_realloc` 是保留给现有调用方的 libc 风格接口。受支持的 Linux/macOS 生产构建固定转发到随包构建的 jemalloc；sanitizer、Windows 和 Android 构建使用平台分配器。具体后端在构建时确定，不支持运行时切换。

`ObMemAttr` 参数继续作为调用接口的一部分保留，也可供上层 allocator 和错误日志使用；进程级分配器不会根据其中的 tenant/context 创建独立内存池或实施硬限制。`ob_realloc` 直接遵循构建时后端的 realloc 语义，返回地址可能不变。

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
  uint64_t    ctx_id_;     // 参考 ob_mod_define.h，供有需要的上层组件识别上下文
  uint64_t    sub_ctx_id_; // 兼容字段
  ObAllocPrio prio_;       // 优先级
};
```

> 参考文件 alloc_struct.h

**tenant_id**

兼容字段。进程级分配器不按 tenant 维护统计或硬限制；业务组件需要自行维护容量策略。

**label**

在最开始，seekdb 使用预定义的方式为各个模块创建内存标签。但是随着代码量的增长，预定义标签的方式不太适用，当前改用直接使用常量字符串的方式构造 `ObLabel`。在使用 `ob_malloc` 时，也可以直接传入常量字符串当做 `ObLabel` 参数。标签仍可被上层 allocator 和错误日志使用，但进程级分配器不按标签维护独立内存池。

**ctx_id**

ctx id 是预定义的，可以参考 `alloc_struct.h`。通常使用 `DEFAULT_CTX_ID`；MemoryContext 等上层组件可以按 ctx id 接入自己的轻量统计。进程级分配器不会为每个 tenant/ctx 创建 allocator，也不再提供旧的周期性 context 统计和硬限制。

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
