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
#include "lib/allocator/ob_mem_leak_checker.h"
#include "common/ob_smart_var.h"
#include "lib/alloc/ob_malloc_sample_struct.h"
#include "lib/utility/ob_tracepoint.h"
#include "lib/alloc/ob_malloc_time_monitor.h"
#include "lib/resource/ob_affinity_ctrl.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;

bool ObMallocAllocator::is_inited_ = false;

static bool g_malloc_v2_enabled = false;

void enable_malloc_v2(bool enable)
{
  g_malloc_v2_enabled = enable;
}
bool is_malloc_v2_enabled()
{
  return g_malloc_v2_enabled;
}
namespace oceanbase
{
namespace lib
{

ObMallocAllocator::ObMallocAllocator()
  : allocator_(NULL),
    reserved_(0), max_used_tenant_id_(0), create_on_demand_(false)
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
#ifdef OB_USE_ASAN
  UNUSED(attr);
  return ::realloc(const_cast<void *>(ptr), size);
#else
  SANITY_DISABLE_CHECK_RANGE(); // prevent sanity_check_range
  // Won't create tenant allocator!!
  void *nptr = NULL;
  ObMemAttr inner_attr = attr;
  inner_attr.tenant_id_ = OB_SYS_TENANT_ID;
  inner_attr.use_malloc_v2_ = is_malloc_v2_enabled();
  ObTenantCtxAllocatorGuard allocator = NULL;
  if (OB_ISNULL(allocator = get_tenant_ctx_allocator(inner_attr.tenant_id_, inner_attr.ctx_id_))) {
    // do nothing
  } else if (OB_ISNULL(nptr = allocator->realloc(ptr, size, inner_attr))) {
    // do nothing
  }
  return nptr;
#endif
}

void ObMallocAllocator::free(void *ptr)
{
#ifdef OB_USE_ASAN
  ::free(ptr);
#else
  SANITY_DISABLE_CHECK_RANGE(); // prevent sanity_check_range
  // directly free object instead of using tenant allocator.
  ObTenantCtxAllocator::common_free(ptr);
#endif // OB_USE_ASAN
}

ObTenantCtxAllocatorGuard ObMallocAllocator::get_tenant_ctx_allocator(uint64_t tenant_id,
                                                                      uint64_t ctx_id) const
{
  abort_unless(allocator_ != NULL);
  ObTenantCtxAllocator *ctx_allocator = allocator_[ctx_id].get_allocator();
  return ObTenantCtxAllocatorGuard(ctx_allocator, false);
}

int ObMallocAllocator::create_and_add_tenant_allocator(uint64_t tenant_id)
{
  return OB_SUCCESS;
}

int ObMallocAllocator::create_tenant_allocator(uint64_t tenant_id, void *buf,
                                               ObTenantCtxAllocatorV2 *&allocator)
{
  int ret = OB_SUCCESS;
  allocator = NULL;

  if (tenant_id > max_used_tenant_id_) {
    UNUSED(ATOMIC_BCAS(&max_used_tenant_id_, max_used_tenant_id_, tenant_id));
  }
  ObTenantCtxAllocatorV2 *ctx_allocator = (ObTenantCtxAllocatorV2*)buf;
  ObTenantCtxAllocator *tmp_allocator = (ObTenantCtxAllocator*)(&ctx_allocator[ObCtxIds::MAX_CTX_ID]);
  for (int ctx_id = 0; OB_SUCC(ret) && ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
    new (&ctx_allocator[ctx_id])
        ObTenantCtxAllocatorV2(tenant_id, ctx_id, &tmp_allocator[ctx_id]);
    if (OB_FAIL(ctx_allocator[ctx_id].set_tenant_memory_mgr())) {
        LOG_ERROR("set_tenant_memory_mgr failed", K(ret));
    } else if (ObCtxIds::DO_NOT_USE_ME == ctx_id) {
      ctx_allocator[ctx_id].set_limit(256L<<20);
    }
    new (ctx_allocator[ctx_id].get_allocator())
          ObTenantCtxAllocator(ctx_allocator[ctx_id], tenant_id, ctx_id);
  }
  if (OB_SUCC(ret)) {
    allocator = ctx_allocator;
  }
  return ret;
}

void ObMallocAllocator::set_root_allocator()
{
  int ret = OB_SUCCESS;
  const int64_t BUF_LEN = (sizeof(ObTenantCtxAllocator) + sizeof(ObTenantCtxAllocatorV2)) * ObCtxIds::MAX_CTX_ID;
  static char buf[BUF_LEN] __attribute__((__aligned__(16)));
  ObTenantCtxAllocatorV2 *allocator = NULL;
  abort_unless(OB_SUCCESS == create_tenant_allocator(OB_SYS_TENANT_ID, buf, allocator));
  allocator_ = allocator;
}

ObMallocAllocator *ObMallocAllocator::get_instance()
{
  static ObMallocAllocator instance;
  return &instance;
}

int ObMallocAllocator::with_resource_handle_invoke(uint64_t tenant_id, InvokeFunc func)
{
  int ret = OB_SUCCESS;
  ObTenantResourceMgrHandle resource_handle;
  if (OB_FAIL(ObResourceMgr::get_instance().get_tenant_resource_mgr(
      OB_SYS_TENANT_ID, resource_handle))) {
    LIB_LOG(ERROR, "get_tenant_resource_mgr failed", K(ret), K(tenant_id));
  } else if (!resource_handle.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LIB_LOG(ERROR, "resource_handle is invalid", K(tenant_id));
  } else {
    ret = func(resource_handle.get_memory_mgr());
  }
  return ret;
}
int ObMallocAllocator::set_tenant_limit(uint64_t tenant_id, int64_t bytes)
{
  if (OB_SYS_TENANT_ID != tenant_id) return OB_SUCCESS;
  return with_resource_handle_invoke(tenant_id, [bytes](ObTenantMemoryMgr *mgr) {
      mgr->set_limit(bytes);
      return OB_SUCCESS;
    });
}

int64_t ObMallocAllocator::get_tenant_limit(uint64_t tenant_id)
{
  int64_t limit = 0;
  with_resource_handle_invoke(tenant_id, [&limit](ObTenantMemoryMgr *mgr) {
      limit = mgr->get_limit();
      return OB_SUCCESS;
    });
  return limit;
}

int64_t ObMallocAllocator::get_tenant_hold(uint64_t tenant_id)
{
  int64_t hold = 0;
  with_resource_handle_invoke(tenant_id, [&hold](ObTenantMemoryMgr *mgr) {
      hold = mgr->get_sum_hold();
      return OB_SUCCESS;
    });
  return hold;
}

int64_t ObMallocAllocator::get_tenant_cache_hold(uint64_t tenant_id)
{
  int64_t cache_hold = 0;
  with_resource_handle_invoke(tenant_id, [&cache_hold](ObTenantMemoryMgr *mgr) {
      cache_hold = mgr->get_cache_hold();
      return OB_SUCCESS;
    });
  return cache_hold;
}

int64_t ObMallocAllocator::get_tenant_remain(uint64_t tenant_id)
{
  int64_t remain = 0;
  with_resource_handle_invoke(tenant_id, [&remain](ObTenantMemoryMgr *mgr) {
      remain = mgr->get_limit() - mgr->get_sum_hold() + mgr->get_cache_hold();
      if (remain < 0) {
        remain = 0;
      }
      return OB_SUCCESS;
    });
  return remain;
}

int64_t ObMallocAllocator::get_tenant_ctx_hold(const uint64_t tenant_id, const uint64_t ctx_id) const
{
  int64_t hold = 0;
  ObTenantCtxAllocatorGuard allocator = NULL;
  if (OB_ISNULL(allocator = get_tenant_ctx_allocator(tenant_id, ctx_id))) {
    // do nothing
  } else {
    hold = allocator->get_hold();
  }
  return hold;
}

void ObMallocAllocator::get_tenant_label_usage(
  uint64_t tenant_id, ObLabel &label, ObLabelItem &item) const
{
  ObTenantCtxAllocatorGuard allocator = NULL;
  for (int64_t i = 0; i < ObCtxIds::MAX_CTX_ID; i++) {
    if (OB_ISNULL(allocator = get_tenant_ctx_allocator(tenant_id, i))) {
      // do nothing
    } else {
      item += allocator->get_label_usage(label);
    }
  }
}

void ObMallocAllocator::print_tenant_ctx_memory_usage(uint64_t tenant_id) const
{
  ObTenantCtxAllocatorGuard allocator = NULL;
  for (int64_t ctx_id = 0; ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
    allocator = get_tenant_ctx_allocator(tenant_id, ctx_id);
    if (OB_LIKELY(NULL != allocator)) {
      allocator->print_memory_usage();
    }
  }
}

void ObMallocAllocator::print_tenant_memory_usage(uint64_t tenant_id) const
{
  int ret = OB_SUCCESS;
  with_resource_handle_invoke(tenant_id, [&](ObTenantMemoryMgr *mgr) {
    static const int64_t BUFLEN = 1 << 16;
    SMART_VAR(char[BUFLEN], buf) {
      int64_t ctx_pos = 0;
      const volatile int64_t *ctx_hold_bytes = mgr->get_ctx_hold_bytes();
      for (uint64_t i = 0; OB_SUCC(ret) && i < ObCtxIds::MAX_CTX_ID; i++) {
        if (ctx_hold_bytes[i] > 0) {
          int64_t limit = 0;
          IGNORE_RETURN mgr->get_ctx_limit(i, limit);
          ret = databuff_printf(buf, BUFLEN, ctx_pos,
              "[MEMORY] ctx_id=%25s hold_bytes=%'15ld limit=%'26ld\n",
              get_global_ctx_info().get_ctx_name(i), ctx_hold_bytes[i], limit);
        }
      }
      buf[std::min(ctx_pos, BUFLEN - 1)] = '\0';
      allow_next_syslog();
      _LOG_INFO("[MEMORY] tenant: %lu, limit: %'lu hold: %'lu cache_hold: %'lu "
                "cache_used: %'lu cache_item_count: %'lu \n%s",
          tenant_id,
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

int ObMallocAllocator::set_tenant_ctx_idle(const uint64_t tenant_id,
                                           const uint64_t ctx_id,
                                           const int64_t size,
                                           const bool reserve /*=false*/)
{
  if (OB_SYS_TENANT_ID != tenant_id) return OB_SUCCESS;
  int ret = OB_SUCCESS;
  auto allocator = get_tenant_ctx_allocator(tenant_id, ctx_id);
  if (NULL == allocator) {
    ret = OB_TENANT_NOT_EXIST;
    LOG_WARN("tenant or ctx not exist", K(ret), K(tenant_id), K(ctx_id));
  } else {
    allocator->set_idle(size, reserve);
  }
  return ret;
}

int64_t ObMallocAllocator::sync_wash(uint64_t tenant_id, uint64_t from_ctx_id, int64_t wash_size)
{
  int64_t washed_size = 0;
  if (tenant_id != OB_SYS_TENANT_ID) return washed_size;
  for (int64_t i = 0;
       washed_size < wash_size && i < ObCtxIds::MAX_CTX_ID;
       i++) {
    int64_t ctx_id = (from_ctx_id + i) % ObCtxIds::MAX_CTX_ID;
    auto allocator = get_tenant_ctx_allocator(tenant_id, ctx_id);
    if (NULL != allocator && !(CTX_ATTR(ctx_id).disable_sync_wash_)) {
      washed_size += allocator->sync_wash(wash_size - washed_size);
    }
  }
  return washed_size;
}

int64_t ObMallocAllocator::sync_wash()
{
  int64_t washed_size = 0;
  for (uint64_t tenant_id = 1; tenant_id <= max_used_tenant_id_; ++tenant_id) {
    washed_size += sync_wash(tenant_id, 0, INT64_MAX);
  }
  return washed_size;
}

ObTenantCtxAllocatorGuard ObMallocAllocator::get_tenant_ctx_allocator_unrecycled(
  uint64_t tenant_id, uint64_t ctx_id) const
{
  return ObTenantCtxAllocatorGuard();
}

int ObMallocAllocator::recycle_tenant_allocator(uint64_t tenant_id)
{
  return OB_SUCCESS;
}


#ifdef ENABLE_SANITY
int ObMallocAllocator::get_chunks(ObTenantCtxAllocatorV2 *ta, AChunk **chunks, int cap, int &cnt)
{
  int ret = OB_SUCCESS;
  for (int64_t ctx_id = 0; OB_SUCC(ret) && ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
    ObTenantCtxAllocatorV2 *ctx_allocator = &ta[ctx_id];
    ctx_allocator->get_chunks(chunks, cap, cnt);
    if (cnt >= cap) {
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

void ObMallocAllocator::modify_tenant_memory_access_permission(ObTenantCtxAllocatorV2 *ta, bool accessible)
{
  AChunk *chunks[1024] = {nullptr};
  int chunk_cnt = 0;
  get_chunks(ta, chunks, sizeof(chunks)/sizeof(chunks[0]), chunk_cnt);
  for (int i = 0; i < chunk_cnt; i++) {
    AChunk *chunk = chunks[i];
    if (chunk != nullptr) {
      if (accessible) {
        SANITY_UNPOISON(chunk, chunk->aligned());
      } else {
        SANITY_POISON(chunk, chunk->aligned());
      }
    }
  }
}
#endif
ObMallocHook::ObMallocHook()
  : attr_(OB_SYS_TENANT_ID, "glibc_malloc", ObCtxIds::GLIBC),
    ta_(ObMallocAllocator::get_instance()->get_tenant_ctx_allocator(OB_SYS_TENANT_ID, ObCtxIds::GLIBC)),
    mgr_(((ObjectMgr&)(ta_->get_block_mgr())).obj_mgr_v2_)
{
  STRNCPY(label_, "glibc_malloc_v2", AOBJECT_LABEL_SIZE);
  label_[AOBJECT_LABEL_SIZE] = '\0';
}

ObMallocHook &ObMallocHook::get_instance()
{
  static char buffer[sizeof(ObMallocHook)] __attribute__((__aligned__(16)));
  static ObMallocHook *instance = new (buffer) ObMallocHook();
  return *instance;
}

void *ObMallocHook::alloc(const int64_t size)
{
  SANITY_DISABLE_CHECK_RANGE();
  int ret = EventTable::EN_4;
#ifdef ERRSIM
  if (OB_SUCC(ret)) {
    const ObErrsimModuleType type = THIS_WORKER.get_module_type();
    if (is_errsim_module(attr_.tenant_id_, type.type_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  }
#endif
  void *ptr = NULL;
  if (OB_FAIL(ret)) {
    AllocFailedCtx &afc = g_alloc_failed_ctx();
    afc.reason_ = AllocFailedReason::ERRSIM_INJECTION;
  } else {
    AObject *obj = NULL;
    static thread_local ObMallocSampleLimiter sample_limiter;
    bool sample_allowed = sample_limiter.try_acquire(size);
    if (OB_UNLIKELY(sample_allowed)) {
      ObMemAttr inner_attr = attr_;
      inner_attr.alloc_extra_info_ = true;
      obj = mgr_.alloc_object(size, inner_attr);
    } else {
      obj = mgr_.alloc_object(size, attr_);
    }
    if (OB_UNLIKELY(NULL == obj)) {
      if (ta_->sync_wash() >= size) {
        if (OB_UNLIKELY(sample_allowed)) {
          ObMemAttr inner_attr = attr_;
          inner_attr.alloc_extra_info_ = true;
          obj = mgr_.alloc_object(size, inner_attr);
        } else {
          obj = mgr_.alloc_object(size, attr_);
        }
      }
    }
    if (OB_LIKELY(NULL != obj)) {
      if (OB_UNLIKELY(sample_allowed)) {
        void *addrs[100] = {nullptr};
        backtrace(addrs, ARRAYSIZEOF(addrs));
        MEMCPY(obj->bt(), (char*)addrs, AOBJECT_BACKTRACE_SIZE);
        obj->on_malloc_sample_ = true;
      }
      MEMCPY(obj->label_, label_, AOBJECT_LABEL_SIZE + 1);
      SANITY_POISON(obj, AOBJECT_HEADER_SIZE);
      SANITY_UNPOISON(obj->data_, obj->alloc_bytes_);
      SANITY_POISON(obj->data_ + obj->alloc_bytes_,
          AOBJECT_TAIL_SIZE + (obj->on_malloc_sample_ ? AOBJECT_BACKTRACE_SIZE : 0));
      ptr = (void*)obj->data_;
    }
  }
  if (OB_UNLIKELY(NULL == ptr)) {
    if (TC_REACH_TIME_INTERVAL(1 * 1000 * 1000)) {
      const char *msg = alloc_failed_msg();
      LOG_DBA_WARN_V2(OB_LIB_ALLOCATE_MEMORY_FAIL, OB_ALLOCATE_MEMORY_FAILED, "[OOPS]: alloc failed reason is that ", msg);
    }
  }
  return ptr;
}

void ObMallocHook::free(void *ptr)
{
  ObDisableDiagnoseGuard disable_diagnose_guard;
  SANITY_DISABLE_CHECK_RANGE(); // prevent sanity_check_range
  if (NULL != ptr) {
    AObject *obj = reinterpret_cast<AObject*>((char*)ptr - AOBJECT_HEADER_SIZE);
    abort_unless(obj->is_valid());
    abort_unless(obj->in_use_);
    SANITY_POISON(obj->data_, obj->alloc_bytes_);
    ABlock *block = obj->block();
    abort_unless(block->is_valid());
    abort_unless(block->in_use_);
    abort_unless(NULL != block->obj_set_);
    block->obj_set_v2_->free_object(obj, block);
  }
}
} // end of namespace lib
} // end of namespace oceanbase
