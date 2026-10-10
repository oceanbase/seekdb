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

#if defined(_WIN32)
#define _WINSOCKAPI_
#endif
#include "ob_malloc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__linux__)
#include <malloc.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#elif defined(_WIN32)
#include <malloc.h>
#include <windows.h>
#include <psapi.h>
#endif
#ifdef __OB_MTRACE__
#include <execinfo.h>
#endif
#include "lib/alloc/malloc_hook.h"
#include "lib/utility/ob_platform_utils.h"
#include "lib/utility/ob_tracepoint.h"

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
ObIAllocator *global_default_allocator = nullptr;
static constexpr int64_t MEMORY_USAGE_TRACKER_RESOLVER_COUNT = ObCtxIds::MAX_CTX_ID;
std::atomic<MemoryUsageTrackerResolver>
    g_memory_usage_tracker_resolvers[MEMORY_USAGE_TRACKER_RESOLVER_COUNT];
std::atomic<MemoryQuotaResolver>
    g_memory_quota_resolvers[MEMORY_USAGE_TRACKER_RESOLVER_COUNT];

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

void __attribute__((constructor(MALLOC_INIT_PRIORITY))) init_global_memory_pool()
{
  static_cast<void>(EventTable::instance());
  in_hook() = true;
  global_default_allocator = lib::ObMallocAllocator::get_instance();
  in_hook() = false;
}

int64_t get_virtual_memory_used(int64_t *resident_size)
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
    if (nullptr != resident_size) {
      *resident_size = static_cast<int64_t>(counters.WorkingSetSize);
    }
    return static_cast<int64_t>(counters.PagefileUsage);
  }
  if (nullptr != resident_size) {
    *resident_size = 0;
  }
  return 0;
#elif defined(__APPLE__)
  mach_task_basic_info_data_t task_info_value = {};
  mach_msg_type_number_t task_info_count = MACH_TASK_BASIC_INFO_COUNT;
  if (KERN_SUCCESS == task_info(
          mach_task_self(), MACH_TASK_BASIC_INFO,
          reinterpret_cast<task_info_t>(&task_info_value),
          &task_info_count)) {
    if (nullptr != resident_size) {
      *resident_size = static_cast<int64_t>(task_info_value.resident_size);
    }
    return static_cast<int64_t>(task_info_value.virtual_size);
  }
  if (nullptr != resident_size) {
    *resident_size = 0;
  }
  return 0;
#else
  static const ssize_t page_size = lib::ob_get_page_size();
  int64_t page_count = 0;
  int64_t resident_page_count = 0;
  FILE *statm = fopen("/proc/self/statm", "r");
  if (nullptr != statm) {
    static_cast<void>(fscanf(statm, "%ld %ld", &page_count,
                             &resident_page_count));
    fclose(statm);
  }
  if (nullptr != resident_size) {
    *resident_size = resident_page_count * page_size;
  }
  return page_count * page_size;
#endif
}

bool get_allocator_process_stats(AllocatorProcessStats &stats)
{
  stats = AllocatorProcessStats();
  stats.virtual_memory_ = get_virtual_memory_used(&stats.process_resident_);
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  uint64_t epoch = 1;
  size_t epoch_size = sizeof(epoch);
  size_t value_size = sizeof(size_t);
  size_t allocated = 0;
  size_t active = 0;
  size_t resident = 0;
  size_t mapped = 0;
  if (0 == je_mallctl("epoch", &epoch, &epoch_size, &epoch, sizeof(epoch))
      && 0 == je_mallctl("stats.allocated", &allocated, &value_size, nullptr, 0)
      && (value_size = sizeof(size_t),
          0 == je_mallctl("stats.active", &active, &value_size, nullptr, 0))
      && (value_size = sizeof(size_t),
          0 == je_mallctl("stats.resident", &resident, &value_size, nullptr, 0))
      && (value_size = sizeof(size_t),
          0 == je_mallctl("stats.mapped", &mapped, &value_size, nullptr, 0))) {
    stats.allocator_stats_available_ = true;
    stats.allocated_ = static_cast<uint64_t>(allocated);
    stats.active_ = static_cast<uint64_t>(active);
    stats.resident_ = static_cast<uint64_t>(resident);
    stats.mapped_ = static_cast<uint64_t>(mapped);
  }
#endif
  return stats.allocator_stats_available_;
}

void ob_print_mod_memory_usage(bool print_to_std,
                               bool print_glibc_malloc_stats)
{
  UNUSED(print_glibc_malloc_stats);
  AllocatorProcessStats stats;
  const bool available = get_allocator_process_stats(stats);
  const uint64_t allocated = stats.allocated_;
  const uint64_t active = stats.active_;
  const uint64_t allocator_resident = stats.resident_;
  const uint64_t mapped = stats.mapped_;
  const int64_t virtual_memory = stats.virtual_memory_;
  const int64_t process_resident = stats.process_resident_;
  LIB_LOG(INFO, "allocator process memory", K(available), K(allocated),
          K(active), K(allocator_resident), K(mapped), K(virtual_memory),
          K(process_resident));
  if (print_to_std) {
    fprintf(stderr,
            "allocator_process_memory available=%d allocated=%llu active=%llu "
            "allocator_resident=%llu mapped=%llu virtual_memory=%lld "
            "process_resident=%lld\n",
            available ? 1 : 0, static_cast<unsigned long long>(allocated),
            static_cast<unsigned long long>(active),
            static_cast<unsigned long long>(allocator_resident),
            static_cast<unsigned long long>(mapped),
            static_cast<long long>(virtual_memory),
            static_cast<long long>(process_resident));
  }
}

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

void set_memory_quota_resolver(const int64_t ctx_id,
                               MemoryQuotaResolver resolver)
{
  if (ctx_id >= 0 && ctx_id < MEMORY_USAGE_TRACKER_RESOLVER_COUNT) {
    g_memory_quota_resolvers[ctx_id].store(resolver, std::memory_order_release);
  }
}

MemoryQuota *resolve_memory_quota(const int64_t ctx_id)
{
  MemoryQuotaResolver resolver = nullptr;
  if (ctx_id >= 0 && ctx_id < MEMORY_USAGE_TRACKER_RESOLVER_COUNT) {
    resolver = g_memory_quota_resolvers[ctx_id].load(std::memory_order_acquire);
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
