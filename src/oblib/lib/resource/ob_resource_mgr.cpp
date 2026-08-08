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

#define USING_LOG_PREFIX COMMON


#include "ob_resource_mgr.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/utility/utility.h"
#include <cstdlib>

namespace oceanbase
{
using namespace common;
namespace lib
{
ObMemoryMgr::ObMemoryMgr()
  : cache_washer_(NULL),
    limit_(INT64_MAX), hard_limit_(INT64_MAX), sum_hold_(0),
    cache_hold_(0), cache_item_count_(0)
{
  for (uint64_t i = 0; i < common::ObCtxIds::MAX_CTX_ID; i++) {
    ATOMIC_STORE(&(hold_bytes_[i]), 0);
    ATOMIC_STORE(&(limit_bytes_[i]), INT64_MAX);
    ATOMIC_STORE(&(hard_limit_bytes_[i]), INT64_MAX);
  }
}
void ObMemoryMgr::set_cache_washer(ObICacheWasher &cache_washer)
{
  cache_washer_ = &cache_washer;
}

AChunk *ObMemoryMgr::alloc_chunk(const int64_t size, const ObMemAttr &attr)
{
  AChunk *chunk = NULL;
  int ret = OB_SUCCESS;
  if (size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid size", K(ret), K(size));
  } else {
    const int64_t hold_size = static_cast<int64_t>(CHUNK_MGR.hold(static_cast<uint64_t>(size)));
    bool reach_ctx_limit = false;
    if (update_hold(hold_size, attr.ctx_id_, attr.label_, reach_ctx_limit, OB_HIGH_ALLOC == attr.prio_)) {
      chunk = alloc_chunk_(size, attr);
      if (NULL == chunk) {
        update_hold(-hold_size, attr.ctx_id_, attr.label_, reach_ctx_limit);
      }
    }
    BASIC_TIME_GUARD_CLICK("ALLOC_CHUNK_END");
    if (!reach_ctx_limit && NULL != cache_washer_ && NULL == chunk && hold_size < cache_hold_) {
      // try wash memory from cache
      ObICacheWasher::ObCacheMemBlock *washed_blocks = NULL;
      int64_t wash_size = hold_size + LARGE_REQUEST_EXTRA_MB_COUNT * INTACT_ACHUNK_SIZE;
      while (!reach_ctx_limit && OB_SUCC(ret) && NULL == chunk && wash_size < cache_hold_) {
        if (OB_FAIL(cache_washer_->sync_wash_mbs(wash_size, washed_blocks))) {
        } else {
          // should return back to os, then realloc again
          ObMemAttr cache_attr;
          
          cache_attr.label_ = ObNewModIds::OB_KVSTORE_CACHE_MB;
          ObICacheWasher::ObCacheMemBlock *next = NULL;
          while (NULL != washed_blocks) {
            AChunk *chunk = ptr2chunk(washed_blocks);
            next = washed_blocks->next_;
            free_chunk(chunk, cache_attr);
            chunk = NULL;
            washed_blocks = next;
          }

          if (update_hold(static_cast<int64_t>(hold_size), attr.ctx_id_, attr.label_,
                          reach_ctx_limit, OB_HIGH_ALLOC == attr.prio_)) {
            chunk = alloc_chunk_(size, attr);
            if (NULL == chunk) {
              update_hold(-hold_size, attr.ctx_id_, attr.label_, reach_ctx_limit);
            }
          }
        }
      }

      if (OB_FAIL(ret)) {
      }
      BASIC_TIME_GUARD_CLICK("WASH_KVCACHE_END");
    }
  }
  return chunk;
}

void ObMemoryMgr::free_chunk(AChunk *chunk, const ObMemAttr &attr)
{
  if (NULL != chunk) {
    bool reach_ctx_limit = false;
    const int64_t hold_size = static_cast<int64_t>(chunk->hold());
    update_hold(-hold_size, attr.ctx_id_, attr.label_, reach_ctx_limit);
    free_chunk_(chunk, attr);
  }
}

void *ObMemoryMgr::alloc_cache_mb(const int64_t size)
{
  void *ptr = NULL;
  const ObMallocBackend backend = get_ob_malloc_backend();
  if (OB_LIKELY(common::is_ob_malloc_backend(backend))) {
    AChunk *chunk = NULL;
    ObMemAttr attr;

    attr.prio_ = OB_NORMAL_ALLOC;
    attr.label_ = ObNewModIds::OB_KVSTORE_CACHE_MB;
    if (NULL != (chunk = alloc_chunk(size, attr))) {
      ptr = chunk->data_;
    }
  } else if (OB_LIKELY(common::is_jemalloc_backend(backend) && size > 0)) {
    ptr = common::jemalloc_malloc(static_cast<size_t>(size));
  }
  return ptr;
}

void ObMemoryMgr::free_cache_mb(void *ptr)
{
  if (NULL != ptr) {
    const ObMallocBackend backend = get_ob_malloc_backend();
    if (OB_LIKELY(common::is_ob_malloc_backend(backend))) {
      ObMemAttr attr;

      attr.prio_ = OB_NORMAL_ALLOC;
      attr.label_ = ObNewModIds::OB_KVSTORE_CACHE_MB;
      AChunk *chunk = ptr2chunk(ptr);
      free_chunk(chunk, attr);
    } else if (OB_LIKELY(common::is_jemalloc_backend(backend))) {
      common::jemalloc_free(ptr);
    }
  }
}

int ObMemoryMgr::set_ctx_hard_limit(const uint64_t ctx_id, const int64_t hard_limit)
{
  int ret = OB_SUCCESS;
  if (ctx_id >= ObCtxIds::MAX_CTX_ID || hard_limit <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguemnt", K(ret), K(ctx_id), K(hard_limit));
  } else {
    hard_limit_bytes_[ctx_id] = hard_limit;
  }
  return ret;
}

int ObMemoryMgr::set_ctx_limit(const uint64_t ctx_id, const int64_t limit)
{
  int ret = OB_SUCCESS;
  if (ctx_id >= ObCtxIds::MAX_CTX_ID || limit <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguemnt", K(ret), K(ctx_id), K(limit));
  } else {
    limit_bytes_[ctx_id] = limit;
  }
  return ret;
}

int ObMemoryMgr::get_ctx_limit(const uint64_t ctx_id, int64_t &limit) const
{
  int ret = OB_SUCCESS;
  if (ctx_id >= ObCtxIds::MAX_CTX_ID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguemnt", K(ret), K(ctx_id));
  } else {
    limit = limit_bytes_[ctx_id];
  }
  return ret;
}

int ObMemoryMgr::get_ctx_hold(const uint64_t ctx_id, int64_t &hold) const
{
  int ret = OB_SUCCESS;
  if (ctx_id >= ObCtxIds::MAX_CTX_ID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguemnt", K(ret), K(ctx_id));
  } else {
    hold = hold_bytes_[ctx_id];
    if (ObCtxIds::KVSTORE_CACHE_ID == ctx_id) {
      hold += get_cache_hold();
    }
  }
  return ret;
}

void ObMemoryMgr::update_cache_hold(const int64_t size)
{
  if (0 != size) {
    ATOMIC_AAF(&cache_hold_, size);
    ATOMIC_AAF(&cache_item_count_, size > 0 ? 1 : -1);
  }
}

bool ObMemoryMgr::update_hold(const int64_t size, const uint64_t ctx_id,
                                    const lib::ObLabel &label, bool &reach_ctx_limit, bool high_prio)
{
  bool updated = true;
  reach_ctx_limit = false;
  const int64_t limit = high_prio ? INT64_MAX : hard_limit_;
  const int64_t nvalue = ATOMIC_AAF(&sum_hold_, size); 
  if (size > 0 && nvalue > limit) {
    ATOMIC_AAF(&sum_hold_, -size);
    updated = false;
    auto &afc = g_alloc_failed_ctx();
    afc.reason_ = MEMORY_HOLD_REACH_LIMIT;
    afc.alloc_size_ = size;
    
    afc.memory_hold_ = get_sum_hold();
    afc.memory_limit_ = hard_limit_;
  } else if (label != ObNewModIds::OB_KVSTORE_CACHE_MB) {
    if (!update_ctx_hold(ctx_id, size, high_prio)) {
      ATOMIC_AAF(&sum_hold_, -size);
      updated = false;
      reach_ctx_limit = true;
    }
  } else {
    update_cache_hold(size);
  }
  return updated;
}

bool ObMemoryMgr::update_ctx_hold(const uint64_t ctx_id, const int64_t size, bool high_prio)
{
  bool updated = false;
  if (ctx_id < ObCtxIds::MAX_CTX_ID) {
    volatile int64_t &hold = hold_bytes_[ctx_id];
    const int64_t limit = high_prio ? INT64_MAX : hard_limit_bytes_[ctx_id];
    if (size <= 0) {
      ATOMIC_AAF(&hold, size);
      updated = true;
    } else {
      if (hold + size <= limit) {
        const int64_t nvalue = ATOMIC_AAF(&hold, size);
        if (size > 0 && nvalue > limit) {
          ATOMIC_AAF(&hold, -size);
        } else {
          updated = true;
        }
      }
    }
    if (!updated) {
      auto &afc = g_alloc_failed_ctx();
      afc.reason_ = CTX_HOLD_REACH_LIMIT;
      afc.alloc_size_ = size;
      afc.ctx_id_ = ctx_id;
      afc.ctx_hold_ = hold;
      afc.ctx_limit_ = limit;
    }
  } else {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "invalid ctx_id", K(ctx_id));
  }
  return updated;
}

AChunk *ObMemoryMgr::ptr2chunk(void *ptr)
{
  AChunk *chunk = NULL;
  if (NULL != ptr) {
    chunk = reinterpret_cast<AChunk *>(reinterpret_cast<char *>(ptr) - ACHUNK_PURE_HEADER_SIZE);
  }
  return chunk;
}

AChunk *ObMemoryMgr::alloc_chunk_(const int64_t size, const ObMemAttr &attr)
{
  AChunk *chunk = nullptr;
  if (OB_UNLIKELY(attr.ctx_id_ == ObCtxIds::CO_STACK)) {
    chunk = CHUNK_MGR.alloc_co_chunk(static_cast<uint64_t>(size));
  } else {
    chunk = CHUNK_MGR.alloc_chunk(static_cast<uint64_t>(size), OB_HIGH_ALLOC == attr.prio_);
  }
  return chunk;
}

void ObMemoryMgr::free_chunk_(AChunk *chunk, const ObMemAttr &attr)
{
  if (OB_UNLIKELY(attr.ctx_id_ == ObCtxIds::CO_STACK)) {
    CHUNK_MGR.free_co_chunk(chunk);
  } else {
    CHUNK_MGR.free_chunk(chunk);
  }
}

ObResourceState::ObResourceState()
  : memory_mgr_(), ref_cnt_(0)
{
}


ObResourceState::~ObResourceState()
{
  
  ref_cnt_ = 0;
}

ObResourceMgrHandle::ObResourceMgrHandle()
  : owner_(NULL),
    state_(NULL)
{
}

ObResourceMgrHandle::~ObResourceMgrHandle()
{
  reset();
}

int ObResourceMgrHandle::init(ObResourceMgr *owner, ObResourceState *state)
{
  // can't invoke reset here, because init is invoked with read lock acquired,
  // reset will invoke dec_ref which may try to acquire write lock, leading to
  // recursive lock
  int ret = OB_SUCCESS;
  if (is_valid()) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (NULL == owner || NULL == state) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(owner), KP(state));
  } else {
    owner_ = owner;
    state_ = state;
    owner_->inc_ref(state_);
  }
  return ret;
}

bool ObResourceMgrHandle::is_valid() const
{
  return NULL != owner_ && NULL != state_;
}

void ObResourceMgrHandle::reset()
{
  if (is_valid()) {
    owner_->dec_ref(state_);
    owner_ = NULL;
    state_ = NULL;
  }
}

ObMemoryMgr *ObResourceMgrHandle::get_memory_mgr()
{
  ObMemoryMgr *memory_mgr = NULL;
  if (NULL != state_) {
    memory_mgr = &state_->memory_mgr_;
  }
  return memory_mgr;
}

const ObMemoryMgr *ObResourceMgrHandle::get_memory_mgr() const
{
  const ObMemoryMgr *memory_mgr = NULL;
  if (NULL != state_) {
    memory_mgr = &state_->memory_mgr_;
  }
  return memory_mgr;
}

ObResourceMgr::ObResourceMgr()
  : inited_(false), cache_washer_(NULL), lock_(), state_(NULL)
{
  lock_.enable_record_stat(false);
  lock_.set_latch_id(common::ObLatchIds::RESOURCE_MGR_LIST_LOCK);
}

ObResourceMgr::~ObResourceMgr()
{
}

int ObResourceMgr::init()
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

void ObResourceMgr::destroy()
{
  if (inited_) {
    cache_washer_ = NULL;
    state_ = NULL;
    inited_ = false;
  }
}

ObResourceMgr &ObResourceMgr::get_instance()
{
  static ObResourceMgr resource_mgr;
  if (!resource_mgr.inited_) {
    // use the lock to avoid concurrent init of resource mgr
    ObDisableDiagnoseGuard disable_diagnose_guard;
    SpinWLockGuard guard(resource_mgr.lock_);
    if (!resource_mgr.inited_) {
      int ret = OB_SUCCESS;
      if (OB_FAIL(resource_mgr.init())) {
      }
    }
  }
  return resource_mgr;
}

int ObResourceMgr::set_cache_washer(ObICacheWasher &cache_washer)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    cache_washer_ = &cache_washer;
    ObDisableDiagnoseGuard disable_diagnose_guard;
    SpinWLockGuard guard(lock_);
    if (NULL != state_) {
      state_->memory_mgr_.set_cache_washer(cache_washer);
    }
  }
  return ret;
}

int ObResourceMgr::get_handle(ObResourceMgrHandle &handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObDisableDiagnoseGuard disable_diagnose_guard;
    ObResourceState *resource_state = NULL;
    {
      SpinRLockGuard guard(lock_);
      if (OB_FAIL(get_state_unsafe(resource_state))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("get_state_unsafe failed", K(ret));
        } else {
          ret = OB_SUCCESS;
        }
      } else if (OB_FAIL(handle.init(this, resource_state))) {
      }
    }

    if (OB_SUCC(ret) && !handle.is_valid()) {
      SpinWLockGuard guard(lock_);
      // maybe other thread create, so retry get here
      if (OB_FAIL(get_state_unsafe(resource_state))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("get_state_unsafe failed", K(ret));
        } else {
          ret = OB_SUCCESS;
          if (OB_FAIL(create_state_unsafe(resource_state))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(handle.init(this, resource_state))) {
        }
      }
    }
  }
  return ret;
}

void ObResourceMgr::inc_ref(ObResourceState *resource_state)
{
  if (NULL != resource_state) {
    ATOMIC_AAF(&resource_state->ref_cnt_, 1);
  }
}

void ObResourceMgr::dec_ref(ObResourceState *resource_state)
{
  if (NULL != resource_state) {
    int64_t ref_cnt = 0;
    if (0 == (ref_cnt = ATOMIC_SAF(&resource_state->ref_cnt_, 1))) {
      ObDisableDiagnoseGuard disable_diagnose_guard;
      SpinWLockGuard guard(lock_);
      if (0 == ATOMIC_LOAD(&resource_state->ref_cnt_)) {
        int ret = OB_SUCCESS;
        if (OB_FAIL(remove_state_unsafe())) {
        }
      }
    } else if (ref_cnt < 0) {
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "ref_cnt negative", K(ref_cnt));
    }
  }
}

int ObResourceMgr::get_state_unsafe(ObResourceState *&resource_state)
{
  int ret = OB_SUCCESS;
  resource_state = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    resource_state = state_;
    if (NULL == resource_state) {
      ret = OB_ENTRY_NOT_EXIST;
    }
  }
  return ret;
}

int ObResourceMgr::create_state_unsafe(ObResourceState *&resource_state)
{
  int ret = OB_SUCCESS;

  resource_state = NULL;
  void *ptr = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    static char buf[sizeof(ObResourceState)] __attribute__((__aligned__(16)));
    ptr = buf;
  }
  if (OB_SUCC(ret)) {
    resource_state = new (ptr) ObResourceState();
    if (NULL != cache_washer_) {
      resource_state->memory_mgr_.set_cache_washer(*cache_washer_);
    }
    state_ = resource_state;
  }
  return ret;
}

int ObResourceMgr::remove_state_unsafe()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObResourceState *resource_state = state_;
    state_ = NULL;
    if (NULL == resource_state) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("resource state not exist", K(ret));
    } else {
      resource_state->~ObResourceState();
      
      resource_state = NULL;
    }
  }
  return ret;
}

}//end namespace common
}//end namespace oceanbase
