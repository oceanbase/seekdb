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
#include "lib/hash/ob_hashmap.h"
#include "lib/allocator/ob_jemalloc.h"
#include "lib/allocator/ob_malloc.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#define OBMALLOC_ATTR(s) __attribute__((s))
#define OBMALLOC_EXPORT __attribute__((visibility("default")))
#define OBMALLOC_ALLOC_SIZE(s) __attribute__((alloc_size(s)))
#define OBMALLOC_NOTHROW __attribute__((nothrow))
#define LIBC_ALIAS(fn) __attribute__((alias (#fn), used))

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;
typedef void* (*MemsetPtr)(void*, int, size_t);
MemsetPtr memset_ptr = nullptr;

uint64_t up_align(uint64_t x, uint64_t align)
{
  return (x + (align - 1)) & ~(align - 1);
}

struct Header
{
  static const uint32_t MAGIC_CODE = 0XA1B2C3D1;
  static const uint32_t SIZE;
  Header(uint32_t size, bool from_mmap)
    : magic_code_(MAGIC_CODE),
      data_size_(size),
      offset_(0),
      from_mmap_(from_mmap)
  {}
  bool check_magic_code() const { return MAGIC_CODE == magic_code_; }
  void mark_unused() { magic_code_ &= ~0x1; }
  static Header *ptr2header(void *ptr) { return reinterpret_cast<Header*>((char*)ptr - SIZE); }
  uint32_t magic_code_;
  uint32_t data_size_;
  uint32_t offset_;
  uint8_t from_mmap_;
  char padding_[3];
  char data_[0];
} __attribute__((aligned (16)));

const uint32_t Header::SIZE = offsetof(Header, data_);

void *ob_malloc_retry(size_t size)
{
  void *ptr = nullptr;
  do {
    ObMemAttr attr = ObMallocHookAttrGuard::get_tl_mem_attr();
    ptr = ob_malloc(size, attr);
    if (OB_ISNULL(ptr)) {
      ::usleep(10000);  // 10ms
    }
  } while (OB_ISNULL(ptr) && 0 != size);
  return ptr;
}

static inline void *ob_mmap(void *addr, size_t length, int prot, int flags, int fd, loff_t offset)
{
  void *ptr = (void*)syscall(SYS_mmap, addr, length, prot, flags, fd, offset);
  if (OB_UNLIKELY(!UNMAMAGED_MEMORY_STAT.is_disabled()) && OB_LIKELY(MAP_FAILED != ptr)) {
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

namespace
{

void write_stderr(const char *str)
{
  if (nullptr != str) {
    size_t len = 0;
    while ('\0' != str[len]) {
      ++len;
    }
    if (len > 0) {
      static_cast<void>(write(STDERR_FILENO, str, len));
    }
  }
}

[[noreturn]] void fail_malloc_hook_initialization(const ObMallocBackend backend)
{
  const char *configured_backend = std::getenv(ob_malloc_backend_env_name());
  write_stderr("seekdb: invalid malloc backend '");
  write_stderr(nullptr != configured_backend && '\0' != configured_backend[0]
      ? configured_backend
      : ob_malloc_backend_name(backend));
  write_stderr("'; MALLOC_BACKEND supports only obmalloc and jemalloc\n");
  _exit(127);
}

inline ObMallocBackend checked_malloc_backend()
{
  const ObMallocBackend backend = get_ob_malloc_backend();
  if (OB_UNLIKELY(OB_MALLOC_BACKEND_UNKNOWN == backend)) {
    fail_malloc_hook_initialization(backend);
  }
  return backend;
}

} // namespace

void init_malloc_hook()
{
  memset_ptr = memset;
  static_cast<void>(checked_malloc_backend());
}

#ifndef OB_USE_ASAN
static void __attribute__((constructor(101))) init_malloc_hook_at_startup()
{
  init_malloc_hook();
}
#endif

EXTERN_C_BEGIN

OBMALLOC_EXPORT
void OBMALLOC_NOTHROW *
OBMALLOC_ATTR(malloc) OBMALLOC_ALLOC_SIZE(1)
malloc(size_t size)
{
  if (OB_LIKELY(OB_MALLOC_BACKEND_JEMALLOC == checked_malloc_backend())) {
    return jemalloc_malloc(size);
  }

  void *ptr = nullptr;
  abort_unless(size <= UINT32_MAX - Header::SIZE);
  size_t real_size = size + Header::SIZE;
  void *tmp_ptr = nullptr;
  bool from_mmap = false;
  if (OB_UNLIKELY(in_hook())) {
    if (MAP_FAILED == (tmp_ptr = ob_mmap(nullptr, real_size, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))) {
      tmp_ptr = nullptr;
    }
    from_mmap = true;
  } else {
    bool in_hook_bak = in_hook();
    in_hook() = true;
    tmp_ptr = ob_malloc_retry(real_size);
    in_hook() = in_hook_bak;
  }
  if (OB_LIKELY(tmp_ptr != nullptr)) {
    Header *header = new (tmp_ptr) Header((uint32_t)size, from_mmap);
    ptr = header->data_;
  }
  return ptr;
}

OBMALLOC_EXPORT void OBMALLOC_NOTHROW
free(void *ptr)
{
  const int saved_errno = errno;
  if (OB_LIKELY(ptr != nullptr)) {
    if (OB_LIKELY(OB_MALLOC_BACKEND_JEMALLOC == checked_malloc_backend())) {
      jemalloc_free(ptr);
    } else {
      Header *header = Header::ptr2header(ptr);
      abort_unless(header->check_magic_code());
      header->mark_unused();
      void *orig_ptr = (char*)header - header->offset_;
      if (OB_UNLIKELY(header->from_mmap_)) {
        ob_munmap(orig_ptr, header->data_size_ + Header::SIZE + header->offset_);
      } else {
        bool in_hook_bak = in_hook();
        in_hook() = true;
        ob_free(orig_ptr);
        in_hook() = in_hook_bak;
      }
    }
  }
  errno = saved_errno;
}

OBMALLOC_EXPORT
void OBMALLOC_NOTHROW *
OBMALLOC_ALLOC_SIZE(2)
realloc(void *ptr, size_t size)
{
  if (0 == size && nullptr != ptr) {
    free(ptr);
    return nullptr;
  } else if (nullptr == ptr) {
    return malloc(size);
  } else if (OB_LIKELY(OB_MALLOC_BACKEND_JEMALLOC == checked_malloc_backend())) {
    return jemalloc_realloc(ptr, size);
  }

  void *nptr = nullptr;
  abort_unless(size <= UINT32_MAX - Header::SIZE);
  size_t real_size = size + Header::SIZE;
  void *tmp_ptr = nullptr;
  bool from_mmap = false;
  if (OB_UNLIKELY(in_hook())) {
    if (MAP_FAILED == (tmp_ptr = ob_mmap(nullptr, real_size, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))) {
      tmp_ptr = nullptr;
    }
    from_mmap = true;
  } else {
    bool in_hook_bak = in_hook();
    in_hook() = true;
    DEFER(in_hook() = in_hook_bak);
    tmp_ptr = ob_malloc_retry(real_size);
  }
  if (OB_LIKELY(tmp_ptr != nullptr)) {
    Header *header = new (tmp_ptr) Header((uint32_t)size, from_mmap);
    nptr = header->data_;
    if (ptr != nullptr) {
      Header *old_header = Header::ptr2header(ptr);
      abort_unless(old_header->check_magic_code());
      memmove(nptr, ptr, MIN(old_header->data_size_, size));
      free(old_header->data_);
    }
  }
  return nptr;
}

OBMALLOC_EXPORT
void OBMALLOC_NOTHROW *
OBMALLOC_ATTR(malloc)
memalign(size_t alignment, size_t size)
{
  if (OB_LIKELY(OB_MALLOC_BACKEND_JEMALLOC == checked_malloc_backend())) {
    return jemalloc_memalign(alignment, size);
  }

  void *ptr = nullptr;
  abort_unless(alignment <= UINT32_MAX / 2);
  {
    size_t a = 8;
    while (a < alignment) {
      a <<= 1;
    }
    alignment = a;
  }
  abort_unless(size <= UINT32_MAX - 2 * MAX(alignment, Header::SIZE));
  size_t real_size = 2 * MAX(alignment, Header::SIZE) + size;
  void *tmp_ptr = nullptr;
  bool from_mmap = false;
  if (OB_UNLIKELY(in_hook())) {
    if (MAP_FAILED == (tmp_ptr = ob_mmap(nullptr, real_size, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))) {
      tmp_ptr = nullptr;
    }
    from_mmap = true;
  } else {
    bool in_hook_bak = in_hook();
    in_hook() = true;
    DEFER(in_hook() = in_hook_bak);
    tmp_ptr = ob_malloc_retry(real_size);
  }
  if (OB_LIKELY(tmp_ptr != nullptr)) {
    char *start = (char *)tmp_ptr + Header::SIZE;
    char *align_ptr = (char *)up_align(reinterpret_cast<int64_t>(start), alignment);
    char *pheader = align_ptr - Header::SIZE;
    size_t offset = pheader - (char*)tmp_ptr;
    Header *header = new (pheader) Header((uint32_t)size, from_mmap);
    header->offset_ = (uint32_t)offset;
    ptr = header->data_;
  }
  return ptr;
}

void *ob_mmap_hook(void *addr, size_t length, int prot, int flags, int fd, loff_t offset)
{
  return ob_mmap(addr, length, prot, flags, fd, offset);
}

int ob_munmap_hook(void *addr, size_t length)
{
  return ob_munmap(addr, length);
}

__attribute__((visibility("default"))) void *mmap(void *addr, size_t, int, int, int, loff_t)
    __attribute__((weak, alias("ob_mmap_hook")));
__attribute__((visibility("default"))) void *mmap64(void *addr, size_t, int, int, int, loff_t)
    __attribute__((weak, alias("ob_mmap_hook")));
__attribute__((visibility("default"))) int munmap(void *addr, size_t length)
    __attribute__((weak, alias("ob_munmap_hook")));

OBMALLOC_EXPORT size_t OBMALLOC_NOTHROW
malloc_usable_size(void *ptr)
{
  size_t ret = 0;
  if (OB_LIKELY(nullptr != ptr)) {
    if (OB_LIKELY(OB_MALLOC_BACKEND_JEMALLOC == checked_malloc_backend())) {
      ret = jemalloc_usable_size(ptr);
    } else {
      Header *header = Header::ptr2header(ptr);
      abort_unless(header->check_magic_code());
      ret = header->data_size_;
    }
  }
  return ret;
}

void *__libc_malloc(size_t size) LIBC_ALIAS(malloc);
void *__libc_realloc(void* ptr, size_t size) LIBC_ALIAS(realloc);
void __libc_free(void* ptr) LIBC_ALIAS(free);
void *__libc_memalign(size_t align, size_t size) LIBC_ALIAS(memalign);

EXTERN_C_END
