---
title: Memory Management
---

# Introduction
Memory management is one of the most important modules in any large C++ project. seekdb's memory design combines process-wide allocation, business cache budgets, and object-lifetime management. Generally, a good memory management module needs to consider the following issues:

- Easy to use. The designed interface must be understood and used by the container, otherwise the code will be difficult to read and maintain, and memory errors will be more likely to occur;
- Efficient. An efficient memory allocator has a crucial impact on performance, especially in high-concurrency scenarios;
- Diagnosis. As the amount of code increases, bugs are inevitable. Common memory errors, such as memory leaks, memory out-of-bounds, wild pointers and other problems cause headaches for development and operation and maintenance. How to write a function that can help us avoid or troubleshoot these problems is also an important indicator to measure the quality of the memory management module.

Business components such as the KV cache, meta pool, and MemoryContext maintain their own capacity or lifetime policies. The process allocator itself does not provide hard isolation by tenant or context.

This article will introduce the commonly used memory allocation interfaces and memory management related idioms in seekdb. For technical details of memory management, please refer to [Memory Management](https://open.oceanbase.com/blog/8501613072)( In Chinese).

## Runtime memory budget

`memory_budget` is the logical budget used to size caches and buffers. Its default is `0M`, which selects an automatic value based on the smaller effective cgroup or physical-memory capacity. The automatic calculation targets 80%, reserves at least 1 GiB for the system when possible, and never produces less than 1 GiB.

An explicit non-zero value must be at least 1 GiB. The main derived defaults are:

| Parameter | Default behavior when set to `0M` |
| --- | --- |
| `kvcache_memory_limit` | `min(1 TiB, 40% of memory_budget)` |
| `memstore_memory_limit` | `50% of memory_budget` |
| `vector_memory_limit` | `50% of memory_budget` |

`memory_limit` is retained only as a deprecated compatibility parameter. Its configured value is accepted and persisted, but current memory sizing and control ignore it. Use `memory_budget` for new configurations. There is no `memory_reserved` configuration parameter.

# Common Interfaces and Methods of OceanBase seekdb Memory Management
seekdb provides different memory allocators for different scenarios. In addition, in order to improve program execution efficiency, there are some conventional implementations, such as reset/reuse, etc.

## ob_malloc

`ob_malloc`, `ob_free`, and `ob_realloc` are libc-style compatibility interfaces retained for existing callers. Supported Linux/macOS production builds always forward them to the bundled jemalloc. Sanitizer, Windows, and Android builds use the platform allocator. The concrete backend is selected at build time and cannot be switched at runtime.

`ObMemAttr` remains part of the interface and can be consumed by higher-level allocators and error logging. The process allocator does not create per-tenant or per-context pools or enforce hard limits from these attributes. `ob_realloc` follows the realloc semantics of the build-time backend and may retain the original address.

```cpp
inline void *ob_malloc(const int64_t nbyte, const ObMemAttr &attr = default_memattr);
inline void ob_free(void *ptr);
inline void *ob_realloc(void *ptr, const int64_t nbyte, const ObMemAttr &attr);
```

## OB_NEW/OB_NEWx
Similar to ob_malloc, OB_NEW provides a set of "C++" interfaces that call the object's constructor and destructor when allocating and releasing memory.

```cpp
/// T is the type, label is the memory label and it can be a const string
#define OB_NEW(T, label, ...)
#define OB_NEW_ALIGN32(T, label, ...)
#define OB_DELETE(T, label, ptr)
#define OB_DELETE_ALIGN32(T, label, ptr)

/// T is the type, pool is the memory pool allocator
#define OB_NEWx(T, pool, ...)
#define OB_DELETEx(T, pool, ptr)
```


## ObArenaAllocator

The design feature is to allocate release multiple times and only release once. Only reset or destruction can truly release the memory. The memory allocated before will not have any effect even if `free` is actively called.

ObArenaAllocator is suitable for scenarios where many small memory allocates are released in a short period of time. For example, in a SQL request, many small block memories will be frequently allocated, and the life cycle of these small memories will last for the entire request period. Usually, the processing time of an SQL request is also very short. This memory allocation method is very effective for small memory and avoiding memory leaks. In seekdb's code, don't be surprised if you see there is only apply for memory but cannot find a place to release it.

> Code reference `page_arena.h`

## ObMemAttr Introduction

seekdb uses `ObMemAttr` to mark a section of memory.

```cpp
struct ObMemAttr
{
  ObLabel     label_;      // label or module
  uint64_t    ctx_id_;     // refer to ob_mod_define.h; higher layers may use it to identify a context
  uint64_t    sub_ctx_id_; // compatibility field
  ObAllocPrio prio_;       // priority
};
```

> reference file alloc_struct.h

**label**

At the beginning, seekdb used predefined memory labels for each module. As the codebase grew, direct construction of `ObLabel` from constant strings became the preferred approach. You can pass a constant string to `ob_malloc`, for example `buf = ob_malloc(disk_addr.size_, "ReadBuf");`. Higher-level allocators and error logs can still use the label, but the process allocator does not maintain a separate pool for it.

**ctx_id**

Context IDs are predefined in `alloc_struct.h`; `DEFAULT_CTX_ID` is normally used. Higher-level components such as MemoryContext can attach lightweight tracking to selected context IDs. The process allocator no longer creates an allocator per tenant/context and no longer provides the old periodic context statistics or hard limits.

**prio**

Two allocation priorities are currently defined: Normal and High. Normal is the default. See `enum ObAllocPrio` in `alloc_struct.h` and the current allocator implementation for the exact behavior. Do not describe the high-priority path in terms of a `memory_reserved` configuration item, because that parameter is not part of the current configuration surface.

## init/destroy/reset/reuse

Caching is one of the important methods to improve program performance. Object reuse is also a way of caching. On the one hand, it reduces the frequency of memory allocate and release, and on the other hand, it can reduce some construction and destruction overhead. There is a lot of object reuse in seekdb, and some conventions have been formed, such as the reset and reuse functions.

**reset**

Used to reset objects. Restore the object's state to the state after the constructor or init function was executed. For example `ObNewRow::reset`.

**reuse**

Compared with reset, it is more lightweight. Try not to release some expensive resources, such as `PageArena::reuse`.

**init/destroy**

There are two other common interfaces in seekdb: `init` and `destroy`. `init` is used to initizalize object and `destory` to release resources. Only do some very lightweight initialization work in the constructor, such as initializing the pointer to `nullptr`.

## SMART_VAR/HEAP_VAR

SMART_VAR is an auxiliary interface for defining local variables. Variables using this interface are always allocated from the stack first. When the stack memory is insufficient, they will be allocated from the heap. For those large local variables (>8K) that are not easy to optimize, this interface not only ensures the performance of regular scenarios, but also safely reduces the stack capacity. The interface is defined as follows:

```cpp
SMART_VAR(Type, Name, Args...) {
  // do...
}
```

It allocate from the stack when the following conditions are met, otherwise allocate from the heap
```cpp
sizeof(T) < 8K || (stack_used < 256K && stack_free > sizeof(T) + 64K)
```

> SMART_VAR was created to solve historical problems. It try to reduce the amount of stack memory occupied by large memory objects.

HEAP_VAR is similar to SMART_VAR, except that it must allocate memory on the heap.

## SMART_CALL

SMART_CALL is used to "quasi-transparently" resolve recursive function calls that may explode the stack on threads with very small stacks. This interface accepts a function call as a parameter. It will automatically check the current stack usage before calling the function. Once it is found that the available stack space is insufficient, a new stack execution function will be created on this thread immediately. After the function ends, it will continue to return to the original stack. This ensures performance when the stack is sufficient, and can also avoid stack explosion scenarios.

```cpp
SMART_CALL(func(args...))
```

Notice:
1. The return value of func must be an int type representing the error code.
2. SMART_CALL will return an error code. This may be an internal mechanism or a func call.
3. It supports stack cascade expansion, each time a 2M stack is expanded (there is a hard-coded total upper limit of 10M)

Compared with direct calling, SMART_CALL only call `check_stack_overflow` to check stack.
