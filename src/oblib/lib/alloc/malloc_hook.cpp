/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "malloc_hook.h"
#include "lib/alloc/alloc_struct.h"
#include "lib/allocator/ob_jemalloc.h"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#if !defined(OB_HAVE_BUNDLED_JEMALLOC)
#error "The Linux malloc hook requires bundled jemalloc"
#endif

#define ALLOC_HOOK_ATTR(s) __attribute__((s))
#define ALLOC_HOOK_EXPORT __attribute__((visibility("default")))
#define ALLOC_HOOK_ALLOC_SIZE(s) __attribute__((alloc_size(s)))
#define ALLOC_HOOK_NOTHROW __attribute__((nothrow))
#define LIBC_ALIAS(fn) __attribute__((alias (#fn), used))

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;

typedef void* (*MemsetPtr)(void*, int, size_t);
MemsetPtr memset_ptr = nullptr;

static inline void *ob_mmap(void *addr, size_t length, int prot, int flags,
                            int fd, loff_t offset)
{
  void *ptr = reinterpret_cast<void *>(
      syscall(SYS_mmap, addr, length, prot, flags, fd, offset));
  if (OB_UNLIKELY(!UNMAMAGED_MEMORY_STAT.is_disabled())
      && OB_LIKELY(MAP_FAILED != ptr)) {
    UNMAMAGED_MEMORY_STAT.inc(length);
  }
  return ptr;
}

static inline int ob_munmap(void *addr, size_t length)
{
  if (OB_UNLIKELY(!ObUnmanagedMemoryStat::is_disabled())) {
    UNMAMAGED_MEMORY_STAT.dec(length);
  }
  return syscall(SYS_munmap, addr, length);
}

void init_malloc_hook()
{
  memset_ptr = memset;
}

#ifndef OB_USE_ASAN
static void __attribute__((constructor(101))) init_malloc_hook_at_startup()
{
  init_malloc_hook();
}
#endif

extern "C" {

ALLOC_HOOK_EXPORT
void ALLOC_HOOK_NOTHROW *
ALLOC_HOOK_ATTR(malloc) ALLOC_HOOK_ALLOC_SIZE(1)
malloc(size_t size)
{
  return jemalloc_malloc(size);
}

ALLOC_HOOK_EXPORT void ALLOC_HOOK_NOTHROW
free(void *ptr)
{
  const int saved_errno = errno;
  jemalloc_free(ptr);
  errno = saved_errno;
}

ALLOC_HOOK_EXPORT
void ALLOC_HOOK_NOTHROW *
ALLOC_HOOK_ALLOC_SIZE(2)
realloc(void *ptr, size_t size)
{
  if (0 == size && nullptr != ptr) {
    free(ptr);
    return nullptr;
  } else if (nullptr == ptr) {
    return malloc(size);
  }
  return jemalloc_realloc(ptr, size);
}

ALLOC_HOOK_EXPORT
void ALLOC_HOOK_NOTHROW *
ALLOC_HOOK_ATTR(malloc)
memalign(size_t alignment, size_t size)
{
  return jemalloc_memalign(alignment, size);
}

void *ob_mmap_hook(void *addr, size_t length, int prot, int flags, int fd,
                   loff_t offset)
{
  return ob_mmap(addr, length, prot, flags, fd, offset);
}

int ob_munmap_hook(void *addr, size_t length)
{
  return ob_munmap(addr, length);
}

__attribute__((visibility("default"))) void *mmap(
    void *addr, size_t, int, int, int, loff_t)
    __attribute__((weak, alias("ob_mmap_hook")));
__attribute__((visibility("default"))) void *mmap64(
    void *addr, size_t, int, int, int, loff_t)
    __attribute__((weak, alias("ob_mmap_hook")));
__attribute__((visibility("default"))) int munmap(void *addr, size_t length)
    __attribute__((weak, alias("ob_munmap_hook")));

ALLOC_HOOK_EXPORT size_t ALLOC_HOOK_NOTHROW
malloc_usable_size(void *ptr)
{
  return nullptr == ptr ? 0 : jemalloc_usable_size(ptr);
}

void *__libc_malloc(size_t size) LIBC_ALIAS(malloc);
void *__libc_realloc(void *ptr, size_t size) LIBC_ALIAS(realloc);
void __libc_free(void *ptr) LIBC_ALIAS(free);
void *__libc_memalign(size_t align, size_t size) LIBC_ALIAS(memalign);

} // extern "C"
