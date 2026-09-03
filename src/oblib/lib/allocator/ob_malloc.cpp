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

#include "ob_malloc.h"
#include <cstdlib>
#include <cstring>
#if defined(__linux__)
#include <malloc.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#elif defined(_WIN32)
#include <malloc.h>
#endif
#ifdef __OB_MTRACE__
#include <execinfo.h>
#endif

#if defined(ENABLE_SANITY)
extern "C" {
const char *je_malloc_conf =
    "background_thread:false,dirty_decay_ms:1000,muzzy_decay_ms:0";
}
#elif defined(OB_HAVE_BUNDLED_JEMALLOC)
extern "C" {
const char *je_malloc_conf =
    "background_thread:true,dirty_decay_ms:1000,muzzy_decay_ms:0";
}
#endif

namespace oceanbase
{
namespace common
{
static constexpr int64_t MEMORY_USAGE_TRACKER_RESOLVER_COUNT = 1024;
std::atomic<MemoryUsageTrackerResolver>
    g_memory_usage_tracker_resolvers[MEMORY_USAGE_TRACKER_RESOLVER_COUNT];

namespace
{

#if defined(__APPLE__) && defined(OB_HAVE_BUNDLED_JEMALLOC)
malloc_zone_t *find_malloc_zone(const char *name)
{
  malloc_zone_t **zones = nullptr;
  unsigned int count = 0;
  malloc_zone_t *result = nullptr;
  if (KERN_SUCCESS == malloc_get_all_zones(
          mach_task_self(), nullptr,
          reinterpret_cast<vm_address_t **>(&zones), &count)) {
    for (unsigned int i = 0; i < count && nullptr == result; ++i) {
      if (nullptr != zones[i]->zone_name
          && 0 == std::strcmp(zones[i]->zone_name, name)) {
        result = zones[i];
      }
    }
  }
  return result;
}

malloc_zone_t *first_malloc_zone(unsigned int &count)
{
  malloc_zone_t **zones = nullptr;
  count = 0;
  malloc_zone_t *result = nullptr;
  if (KERN_SUCCESS == malloc_get_all_zones(
          mach_task_self(), nullptr,
          reinterpret_cast<vm_address_t **>(&zones), &count)
      && count > 0) {
    result = zones[0];
  }
  return result;
}

bool promote_malloc_zone(malloc_zone_t *target)
{
  bool promoted = false;
  unsigned int count = 0;
  malloc_zone_t *current = first_malloc_zone(count);
  if (nullptr != target && nullptr != current) {
    if (current != target) {
      malloc_zone_unregister(target);
      malloc_zone_register(target);
      for (unsigned int i = 0;
           i <= count && nullptr != current && current != target;
           ++i) {
        malloc_zone_unregister(current);
        malloc_zone_register(current);
        current = first_malloc_zone(count);
      }
    }
    promoted = current == target;
  }
  return promoted;
}
#endif

} // namespace

bool restore_allocator_after_fork()
{
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  // Forked children do not inherit background threads, so jemalloc resets the
  // state to disabled; re-enable it after returning to normal child execution.
  return jemalloc_enable_background_threads();
#else
  return true;
#endif
}

#if defined(__APPLE__) && defined(OB_HAVE_BUNDLED_JEMALLOC)
bool configure_darwin_malloc_zone()
{
  malloc_zone_t *jemalloc_zone = find_malloc_zone("jemalloc_zone");
  return promote_malloc_zone(jemalloc_zone);
}
#endif

void set_memory_usage_tracker_resolver(const int64_t ctx_id,
                                       MemoryUsageTrackerResolver resolver)
{
  if (ctx_id >= 0 && ctx_id < MEMORY_USAGE_TRACKER_RESOLVER_COUNT) {
    g_memory_usage_tracker_resolvers[ctx_id].store(resolver, std::memory_order_release);
  }
}

MemoryUsageTracker *resolve_memory_usage_tracker(const int64_t ctx_id)
{
  MemoryUsageTrackerResolver resolver = nullptr;
  if (ctx_id >= 0 && ctx_id < MEMORY_USAGE_TRACKER_RESOLVER_COUNT) {
    resolver = g_memory_usage_tracker_resolvers[ctx_id].load(std::memory_order_acquire);
  }
  return nullptr != resolver ? resolver(ctx_id) : nullptr;
}

} // namespace common
} // namespace oceanbase

int64_t oceanbase::common::ob_malloc_usable_size(void *ptr)
{
  int64_t usable_size = 0;
  if (nullptr != ptr) {
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
    usable_size = static_cast<int64_t>(jemalloc_usable_size(ptr));
#elif defined(__linux__)
    usable_size = static_cast<int64_t>(::malloc_usable_size(ptr));
#elif defined(__APPLE__)
    usable_size = static_cast<int64_t>(::malloc_size(ptr));
#elif defined(_WIN32)
    usable_size = static_cast<int64_t>(::_msize(ptr));
#endif
  }
  return usable_size;
}


int oceanbase::common::ObMemBuf::ensure_space(const int64_t size, const lib::ObLabel &label)
{
  int ret         = OB_SUCCESS;
  char *new_buf   = NULL;
  int64_t buf_len = size > buf_size_ ? size : buf_size_;

  if (size <= 0 || (NULL != buf_ptr_ && buf_size_ <= 0)) {
    _OB_LOG(WARN, "invalid param, size=%ld, buf_ptr_=%p, "
              "buf_size_=%ld",
              size, buf_ptr_, buf_size_);
    ret = OB_ERROR;
  } else if (NULL == buf_ptr_ || (NULL != buf_ptr_ && size > buf_size_)) {
    new_buf = static_cast<char *>(ob_malloc(buf_len, label));
    if (NULL == new_buf) {
      _OB_LOG(ERROR, "Problem allocate memory for buffer");
      ret = OB_ERROR;
    } else {
      if (NULL != buf_ptr_) {
        ob_free(buf_ptr_);
        buf_ptr_ = NULL;
      }
      buf_size_ = buf_len;
      buf_ptr_ = new_buf;
      label_ = label;
    }
  }

  return ret;
}

void *oceanbase::common::ob_malloc_align(const int64_t alignment, const int64_t nbyte,
                                         const lib::ObLabel &label)
{
  ObMemAttr attr;
  attr.label_ = label;
  return ob_malloc_align(alignment, nbyte, attr);
}

void *oceanbase::common::ob_malloc_align(const int64_t align, const int64_t nbyte,
                                         const ObMemAttr &attr)
{
  return ObAllocAlign::alloc_align(nbyte, align,
      [](const int64_t size, const ObMemAttr &attr){ return ob_malloc(size, attr); }, attr);
}

void oceanbase::common::ob_free_align(void *ptr)
{
  ObAllocAlign::free_align(ptr, [](void *ptr){ ob_free(ptr); });
}


void *ob_zalloc(const int64_t nbyte)
{
  return ::oceanbase::common::ob_malloc(nbyte, ::oceanbase::common::ObModIds::OB_ZLIB);
}

void ob_zfree(void *ptr)
{
  ::oceanbase::common::ob_free(ptr);
}
