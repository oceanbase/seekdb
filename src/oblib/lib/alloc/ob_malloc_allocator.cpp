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

#define USING_LOG_PREFIX LIB

#include "ob_malloc_allocator.h"
#include "lib/allocator/ob_jemalloc.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/utility/ob_smart_var.h"
#include <cstdlib>

// ob_backtrace is implemented in ob_backtrace.cpp for Windows

using namespace oceanbase::lib;
using namespace oceanbase::common;

bool ObMallocAllocator::is_inited_ = false;

namespace oceanbase
{
namespace lib
{

ObMallocAllocator::ObMallocAllocator()
  : allocator_(NULL),
    reserved_(0)
{
  set_root_allocator();
  is_inited_ = true;
}

ObMallocAllocator::~ObMallocAllocator()
{
  is_inited_ = false;
}

void *ObMallocAllocator::alloc(const int64_t size)
{
  ObMemAttr attr;
  return alloc(size, attr);
}

void *ObMallocAllocator::alloc(const int64_t size, const oceanbase::lib::ObMemAttr &_attr)
{
  return realloc(NULL, size, _attr);
}

void *ObMallocAllocator::realloc(
  const void *ptr, const int64_t size, const oceanbase::lib::ObMemAttr &attr)
{
  void *old_ptr = const_cast<void *>(ptr);
  void *nptr = NULL;
  const ObMallocBackend backend = get_ob_malloc_backend();
  if (OB_UNLIKELY(oceanbase::common::is_jemalloc_backend(backend))) {
    if (OB_LIKELY(size >= 0)) {
      nptr = oceanbase::common::jemalloc_realloc(old_ptr, static_cast<size_t>(size));
    }
  }
#if defined(OB_USE_ASAN)
  else if (oceanbase::common::is_ob_malloc_backend(backend)) {
    UNUSED(attr);
    nptr = ::realloc(old_ptr, size);
  }
#else
  else if (oceanbase::common::is_ob_malloc_backend(backend)) {
    // Do not create a context allocator here.
    ObMemAttr inner_attr = attr;
    ObCtxAllocatorGuard allocator = NULL;
    if (OB_ISNULL(allocator = get_ctx_allocator(inner_attr.ctx_id_))) {
      // do nothing
    } else if (OB_ISNULL(nptr = allocator->realloc(ptr, size, inner_attr))) {
      // do nothing
    }
  }
#endif
  return nptr;
}

void ObMallocAllocator::free(void *ptr)
{
  const ObMallocBackend backend = get_ob_malloc_backend();
  if (OB_UNLIKELY(oceanbase::common::is_jemalloc_backend(backend))) {
    oceanbase::common::jemalloc_free(ptr);
  }
#if defined(OB_USE_ASAN)
  else if (oceanbase::common::is_ob_malloc_backend(backend)) {
    ::free(ptr);
  }
#else
  else if (oceanbase::common::is_ob_malloc_backend(backend)) {
    // Free the object directly instead of using a context allocator.
    ObCtxAllocator::common_free(ptr);
  }
#endif
}

ObCtxAllocatorGuard ObMallocAllocator::get_ctx_allocator(uint64_t ctx_id) const
{
  abort_unless(allocator_ != NULL);
  ObCtxAllocator *ctx_allocator = ctx_id < ObCtxIds::MAX_CTX_ID
      ? allocator_[ctx_id].get_allocator()
      : NULL;
  return ObCtxAllocatorGuard(ctx_allocator);
}

int ObMallocAllocator::create_allocator(void *buf,
                                               ObCtxAllocatorState *&allocator)
{
  int ret = OB_SUCCESS;
  allocator = NULL;

  ObCtxAllocatorState *ctx_allocator = (ObCtxAllocatorState*)buf;
  ObCtxAllocator *tmp_allocator = (ObCtxAllocator*)(&ctx_allocator[ObCtxIds::MAX_CTX_ID]);
  for (int ctx_id = 0; OB_SUCC(ret) && ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
    new (&ctx_allocator[ctx_id])
        ObCtxAllocatorState(ctx_id, &tmp_allocator[ctx_id]);
    if (OB_FAIL(ctx_allocator[ctx_id].set_memory_mgr())) {
    }
    new (ctx_allocator[ctx_id].get_allocator())
          ObCtxAllocator(ctx_allocator[ctx_id], ctx_id);
  }
  if (OB_SUCC(ret)) {
    allocator = ctx_allocator;
  }
  return ret;
}

void ObMallocAllocator::set_root_allocator()
{
  int ret = OB_SUCCESS;
  const int64_t BUF_LEN = (sizeof(ObCtxAllocator) + sizeof(ObCtxAllocatorState)) * ObCtxIds::MAX_CTX_ID;
  static char buf[BUF_LEN] __attribute__((__aligned__(16)));
  ObCtxAllocatorState *allocator = NULL;
  abort_unless(OB_SUCCESS == create_allocator(buf, allocator));
  allocator_ = allocator;
}

ObMallocAllocator *ObMallocAllocator::get_instance()
{
  static ObMallocAllocator instance;
  return &instance;
}

int ObMallocAllocator::with_resource_handle_invoke(InvokeFunc func)
{
  int ret = OB_SUCCESS;
  ObResourceMgrHandle resource_handle;
  if (OB_FAIL(ObResourceMgr::get_instance().get_handle(
      resource_handle))) {
  } else if (!resource_handle.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LIB_LOG(ERROR, "resource_handle is invalid");
  } else {
    ret = func(resource_handle.get_memory_mgr());
  }
  return ret;
}

int ObMallocAllocator::set_allocator_hard_limit(int64_t bytes)
{
  return with_resource_handle_invoke([bytes](ObMemoryMgr *mgr) {
      mgr->set_hard_limit(bytes);
      return OB_SUCCESS;
    });
}

int64_t ObMallocAllocator::get_allocator_hard_limit()
{
  int64_t limit = 0;
  with_resource_handle_invoke([&limit](ObMemoryMgr *mgr) {
      limit = mgr->get_hard_limit();
      return OB_SUCCESS;
    });
  return limit;
}

int ObMallocAllocator::set_allocator_limit(int64_t bytes)
{
  return with_resource_handle_invoke([bytes](ObMemoryMgr *mgr) {
      mgr->set_limit(bytes);
      return OB_SUCCESS;
    });
}

int64_t ObMallocAllocator::get_total_limit()
{
  int64_t limit = 0;
  with_resource_handle_invoke([&limit](ObMemoryMgr *mgr) {
      limit = mgr->get_limit();
      return OB_SUCCESS;
    });
  return limit;
}

int64_t ObMallocAllocator::get_total_hold()
{
  int64_t hold = 0;
  with_resource_handle_invoke([&hold](ObMemoryMgr *mgr) {
      hold = mgr->get_sum_hold();
      return OB_SUCCESS;
    });
  return hold;
}

int64_t ObMallocAllocator::get_allocator_cache_hold()
{
  int64_t cache_hold = 0;
  with_resource_handle_invoke([&cache_hold](ObMemoryMgr *mgr) {
      cache_hold = mgr->get_cache_hold();
      return OB_SUCCESS;
    });
  return cache_hold;
}

int64_t ObMallocAllocator::get_allocator_remain()
{
  int64_t remain = 0;
  with_resource_handle_invoke([&remain](ObMemoryMgr *mgr) {
      remain = mgr->get_limit() - mgr->get_sum_hold() + mgr->get_cache_hold();
      if (remain < 0) {
        remain = 0;
      }
      return OB_SUCCESS;
    });
  return remain;
}

int64_t ObMallocAllocator::get_ctx_hold(const uint64_t ctx_id) const
{
  int64_t hold = 0;
  ObCtxAllocatorGuard allocator = NULL;
  if (OB_ISNULL(allocator = get_ctx_allocator(ctx_id))) {
    // do nothing
  } else {
    hold = allocator->get_hold();
  }
  return hold;
}

void ObMallocAllocator::get_label_usage(
  ObLabel &label, ObLabelItem &item) const
{
  ObCtxAllocatorGuard allocator = NULL;
  for (int64_t i = 0; i < ObCtxIds::MAX_CTX_ID; i++) {
    if (OB_ISNULL(allocator = get_ctx_allocator(i))) {
      // do nothing
    } else {
      item += allocator->get_label_usage(label);
    }
  }
}

void ObMallocAllocator::print_ctx_memory_usage() const
{
  ObCtxAllocatorGuard allocator = NULL;
  for (int64_t ctx_id = 0; ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
    allocator = get_ctx_allocator(ctx_id);
    if (OB_LIKELY(NULL != allocator)) {
      allocator->print_memory_usage();
    }
  }
}

void ObMallocAllocator::print_memory_usage() const
{
  int ret = OB_SUCCESS;
  with_resource_handle_invoke([&ret](ObMemoryMgr *mgr) {
    static const int64_t BUFLEN = 1 << 16;
    SMART_VAR(char[BUFLEN], buf) {
      int64_t ctx_pos = 0;
      const volatile int64_t *ctx_hold_bytes = mgr->get_ctx_hold_bytes();
      for (uint64_t i = 0; OB_SUCC(ret) && i < ObCtxIds::MAX_CTX_ID; i++) {
        if (ctx_hold_bytes[i] > 0) {
          int64_t limit = 0;
          IGNORE_RETURN mgr->get_ctx_limit(i, limit);
          ret = databuff_printf(buf, BUFLEN, ctx_pos,
#ifdef _WIN32
              "[MEMORY] ctx_id=%25s hold_bytes=%15ld limit=%26ld\n",
#else
              "[MEMORY] ctx_id=%25s hold_bytes=%'15ld limit=%'26ld\n",
#endif
              get_global_ctx_info().get_ctx_name(i), ctx_hold_bytes[i], limit);
        }
      }
      buf[std::min(ctx_pos, BUFLEN - 1)] = '\0';
      allow_next_syslog();
      _LOG_INFO(
#ifdef _WIN32
                "[MEMORY] limit: %lu hold: %lu cache_hold: %lu "
                "cache_used: %lu cache_item_count: %lu \n%s",
#else
                "[MEMORY] limit: %'lu hold: %'lu cache_hold: %'lu "
                "cache_used: %'lu cache_item_count: %'lu \n%s",
#endif
          mgr->get_limit(),
          mgr->get_sum_hold(),
          mgr->get_cache_hold(),
          mgr->get_cache_hold(),
          mgr->get_cache_item_count(),
          buf);
    }
    return ret;
  });
  UNUSED(ret);
}

void ObMallocAllocator::set_reserved(int64_t bytes)
{
  reserved_ = bytes;
}

int64_t ObMallocAllocator::get_reserved() const
{
  return reserved_;
}

int ObMallocAllocator::set_ctx_idle(const uint64_t ctx_id,
                                           const int64_t size,
                                           const bool reserve /*=false*/)
{
  int ret = OB_SUCCESS;
  auto allocator = get_ctx_allocator(ctx_id);
  if (NULL == allocator) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("context allocator does not exist", K(ret), K(ctx_id));
  } else {
    allocator->set_idle(size, reserve);
  }
  return ret;
}

} // end of namespace lib
} // end of namespace oceanbase
