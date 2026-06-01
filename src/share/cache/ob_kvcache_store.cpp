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

#include "lib/ob_errno.h"
#include "share/cache/ob_kvcache_struct.h"
#define USING_LOG_PREFIX COMMON

#include "share/cache/ob_kvcache_store.h"
#include "share/cache/ob_kvcache_hazard_domain.h"
#include "share/config/ob_server_config.h"
#include "lib/stat/ob_diagnose_info.h"
#include "ob_kvcache_store.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

namespace oceanbase
{
using namespace lib;
namespace common
{

uint32_t handle_index_of(ObKVMemBlockHandle* mb_handle)
{
  return mb_handle->handle_idx_;
}

int ObIKVCacheStore::store(
    const ObIKVCacheKey &key,
    const ObIKVCacheValue &value,
    ObKVCachePair *&kvpair,
    HazptrHolder &hazptr_holder,
    const enum ObKVCachePolicy policy)
{
  int ret = common::OB_SUCCESS;
  const int64_t key_size = key.size();
  const int64_t value_size = value.size();
  if (OB_FAIL(alloc_kvpair(key_size, value_size, kvpair, hazptr_holder, policy))) {
    COMMON_LOG(WARN, "failed to alloc", K(ret), K(key_size), K(value_size));
  } else {
    if (OB_FAIL(key.deep_copy(reinterpret_cast<char *>(kvpair->key_), key_size, kvpair->key_))) {
      COMMON_LOG(WARN, "failed to deep copy key", K(ret));
    } else if (OB_FAIL(value.deep_copy(reinterpret_cast<char *>(kvpair->value_), value_size, kvpair->value_))) {
      COMMON_LOG(WARN, "failed to deep copy value", K(ret));
    }
    if (OB_FAIL(ret)) {
      hazptr_holder.release();
      kvpair = nullptr;
    }
  }
  return ret;
}

int ObIKVCacheStore::alloc_kvpair(
    const int64_t key_size,
    const int64_t value_size,
    ObKVCachePair *&kvpair,
    HazptrHolder &hazptr_holder,
    const enum ObKVCachePolicy policy)
{
  int ret = OB_SUCCESS;
  int64_t tenant_id = OB_SYS_TENANT_ID;
  int64_t washed_size;
  if (OB_SUCC(alloc_kvpair_without_retry(key_size, value_size, kvpair, hazptr_holder, policy))) {
  } else if (OB_ALLOCATE_MEMORY_FAILED != ret) {
    COMMON_LOG(WARN, "failed to allocate kvpair", K(key_size), K(value_size), K(policy));
  } else if (0 >= (washed_size = ObMallocAllocator::get_instance()->sync_wash(tenant_id, 0, INT64_MAX))) {
    COMMON_LOG(WARN, "failed to sync wash");
  } else if (OB_FAIL(alloc_kvpair_without_retry(key_size, value_size, kvpair, hazptr_holder, policy))) {
    COMMON_LOG(WARN, "failed to allocate kvpair", K(key_size), K(value_size), K(policy));
  }

  return ret;
}

int ObKVMBHandleArray::init(int64_t max_mb_num)
{
  int ret = OB_SUCCESS;
  max_mb_num_ = max_mb_num;
  max_block_num_ = (max_mb_num_ + HANDLE_BLOCK_SIZE - 1) / HANDLE_BLOCK_SIZE;
  if (NULL == (mb_handle_blocks_ = static_cast<ObKVMemBlockHandle**>(
                        ob_malloc(sizeof(ObKVMemBlockHandle*) * max_block_num_,
                            ObMemAttr(OB_SERVER_TENANT_ID, "CACHE_MB_HANDLE", ObCtxIds::DEFAULT_CTX_ID))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(ERROR, "Fail to allocate memory for mb_handle_blocks_, ", K_(max_block_num), K(ret));
  } else {
    MEMSET(mb_handle_blocks_, 0, sizeof(ObKVMemBlockHandle*) * max_block_num_);
  }
  return ret;
}

void ObKVMBHandleArray::destroy()
{
  if (NULL != mb_handle_blocks_) {
    for (int64_t i = 0; i < max_block_num_; ++i) {
      if (NULL != mb_handle_blocks_[i]) {
        ob_free(mb_handle_blocks_[i]);
        mb_handle_blocks_[i] = NULL;
      }
    }
    ob_free(mb_handle_blocks_);
    mb_handle_blocks_ = NULL;
  }
  max_mb_num_ = 0;
  max_block_num_ = 0;
}

bool ObKVMBHandleArray::ensure_blocks(int64_t start_idx, int64_t end_idx)
{
  int ret = OB_ALLOCATE_MEMORY_FAILED;
  if (start_idx >= end_idx) return true;
  int64_t start_block = start_idx / HANDLE_BLOCK_SIZE;
  int64_t end_block = (end_idx - 1) / HANDLE_BLOCK_SIZE;

  for (int64_t b = start_block; b <= end_block; ++b) {
    if (ATOMIC_LOAD(&mb_handle_blocks_[b]) == NULL) {
      void *new_block = ob_malloc(sizeof(ObKVMemBlockHandle) * HANDLE_BLOCK_SIZE,
                                  ObMemAttr(OB_SERVER_TENANT_ID, "CACHE_MB_HANDLE", ObCtxIds::DEFAULT_CTX_ID));
      if (NULL == new_block) {
        COMMON_LOG(ERROR, "Fail to allocate memory for mb_handle_block", K(b));
        return false;
      }
      MEMSET(new_block, 0, sizeof(ObKVMemBlockHandle) * HANDLE_BLOCK_SIZE);
      ObKVMemBlockHandle *handles = static_cast<ObKVMemBlockHandle*>(new_block);
      for (int64_t i = 0; i < HANDLE_BLOCK_SIZE; ++i) {
        handles[i].handle_idx_ = b * HANDLE_BLOCK_SIZE + i;
      }

      if (!ATOMIC_BCAS(&mb_handle_blocks_[b], NULL, static_cast<ObKVMemBlockHandle*>(new_block))) {
        ob_free(new_block);
      }
    }
  }
  return true;
}

ObKVCacheStore::ObKVCacheStore()
    : inited_(false),
      block_size_(0),
      block_payload_size_(0),
      cur_mb_num_(0),
      mb_handle_array_(),
      mb_handles_pool_(),
      active_mb_handles_{NULL},
      global_status_(),
      wash_out_lock_(common::ObLatchIds::WASH_OUT_LOCK),
      washable_size_allocator_(),
      washbale_size_info_(),
      tmp_washbale_size_info_(),
      tenant_reserve_mem_ratio_(TENANT_RESERVE_MEM_RATIO),
      wash_itid_(-1),
      mem_limit_getter_(NULL)
{
}

ObKVCacheStore::~ObKVCacheStore()
{
  destroy();
}

int ObKVCacheStore::init(const int64_t max_cache_size,
                         const int64_t block_size,
                         const ObITenantMemLimitGetter &mem_limit_getter)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The ObKVCacheStore has been inited, ", K(ret));
  } else if (OB_UNLIKELY(max_cache_size <= block_size * 3)
      || OB_UNLIKELY(block_size <= (int64_t)(sizeof(ObKVStoreMemBlock)))) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid arguments, ", K(max_cache_size),
      K(block_size), K(ret));
  } else {
    int64_t max_mb_num = compute_mb_handle_num(max_cache_size, block_size);
    if (OB_FAIL(mb_handle_array_.init(max_mb_num))) {
      COMMON_LOG(WARN, "Fail to init mb_handle_array_", K(ret));
    } else if (OB_FAIL(mb_handles_pool_.init(max_mb_num, lib::ObMallocAllocator::get_instance(),
        ObMemAttr(OB_SERVER_TENANT_ID, "CACHE_MB_HANDLE", ObCtxIds::DEFAULT_CTX_ID)))) {
      COMMON_LOG(WARN, "Fail to init mb_handles_pool_, ", K(ret));
    } else {
      block_size_ = block_size;
      block_payload_size_ = block_size - sizeof(ObKVStoreMemBlock);
      // prepare memory block handle
      (void)try_supply_mb(SUPPLY_MB_NUM_ONCE);
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(prepare_wash_structs())) {
        COMMON_LOG(WARN, "preapre wash structs failed", K(ret));
    } else if (OB_FAIL(mb_list_.init(OB_SYS_TENANT_ID))) {
      COMMON_LOG(WARN, "mb_list_ init failed", K(ret), K(OB_SYS_TENANT_ID));
    }
  }

  if (OB_SUCC(ret)) {
    mem_limit_getter_ = &mem_limit_getter;
    inited_ = true;
    COMMON_LOG(INFO, "ObKVCacheStore init success", K(max_cache_size), K(block_size));
  }
  if (!inited_) {
    destroy();
  }
  return ret;
}

void ObKVCacheStore::destroy()
{
  int ret = OB_SUCCESS;

  if (!inited_) {
    return;
  }

  for (int64_t i = 0; i < cur_mb_num_; ++i) {
    ObKVMemBlockHandle &handle = mb_handle_array_.get_mb_handle(i);
    if (FREE != handle.status_) {
      free_mb(mb_list_.resource_mgr_, OB_SYS_TENANT_ID, handle.mem_block_);
    }
  }
  // free all mb handles cached by threads
  purge_mb_handle_retire_station();

  mb_handle_array_.destroy();
  mb_list_.reset();

  mb_handles_pool_.destroy();
  block_size_ = 0;
  block_payload_size_ = 0;

  destroy_wash_structs();
  cur_mb_num_ = 0;
  inited_ = false;
}


// implement functions of ObIKVStore<ObKVMemBlockHandle>
int ObKVCacheStore::alloc(const enum ObKVCachePolicy policy,
    const int64_t block_size, ObKVMemBlockHandle *&mb_handle)
{
  return alloc_mbhandle(policy, block_size, mb_handle);
}

int ObKVCacheStore::free(ObKVMemBlockHandle *mb_handle)
{
  int ret = common::OB_SUCCESS;
  if (NULL == mb_handle) {
    ret = common::OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "mb_handle is null", K(ret), KP(mb_handle));
  } else if (FALSE_IT(mb_handle->set_full(0))) {
  } else if (OB_LIKELY(GCONF._enable_kvcache_hazard_pointer)) {
    mb_handle->retire();
  } else {
    if (ATOMIC_BCAS(&mb_handle->status_, FULL, FREE)) {
      ATOMIC_STORE_RLX(&mb_handle->seq_num_, mb_handle->seq_num_ + 1);
      de_handle_ref(mb_handle);
    }
  }
  return ret;
}

int ObKVCacheStore::alloc_kvpair_without_retry(
    const int64_t key_size,
    const int64_t value_size,
    ObKVCachePair *&kvpair,
    HazptrHolder &hazptr_holder,
    const enum ObKVCachePolicy policy)
{
  int ret = common::OB_SUCCESS;
  bool protect_success;
  const int64_t block_size = get_block_size();
  const int64_t block_payload_size = block_size - sizeof(ObKVStoreMemBlock);
  int64_t align_kv_size = ObKVStoreMemBlock::get_align_size(key_size, value_size);
  kvpair = NULL;
  ObKVMemBlockHandle* mb_handle = nullptr;

  if (align_kv_size > block_payload_size) {
    //large kv
    const int64_t big_block_size = align_kv_size + sizeof(ObKVStoreMemBlock);
    if (OB_FAIL(alloc(policy, big_block_size, mb_handle))) {
      COMMON_LOG(WARN, "alloc failed", K(ret), K(big_block_size));
    } else {
      if (OB_FAIL(hazptr_holder.protect(protect_success, mb_handle))) {
        COMMON_LOG(WARN, "protect failed", KP(mb_handle));
      } else if (protect_success) {
        if (OB_FAIL(mb_handle->alloc(key_size, value_size, align_kv_size, kvpair))) {
          hazptr_holder.release();
          COMMON_LOG(WARN, "alloc failed", K(ret));
        } else {
          // success to alloc kv
          mb_handle->set_full(global_status_.base_mb_score_);
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(ERROR, "protect failed", K(ret));
      }
    }
    if (OB_FAIL(ret) && OB_NOT_NULL(mb_handle)) {
      free(mb_handle);
    }
  } else {
    //small kv
    do {
      mb_handle = get_curr_mb(policy);
      if (NULL != mb_handle) {
        if (OB_FAIL(hazptr_holder.protect(protect_success, mb_handle))) {
          COMMON_LOG(WARN, "failed to protect mb handle", KP(mb_handle));
        } else if (protect_success) {
          if (mb_status_match(policy, mb_handle)) {
            if (OB_FAIL(mb_handle->alloc(key_size, value_size, align_kv_size, kvpair))) {
              if (OB_BUF_NOT_ENOUGH != ret) {
                COMMON_LOG(WARN, "alloc failed", K(ret));
              } else {
                ret = OB_SUCCESS;
              }
            } else {
              break;
            }
          }
          hazptr_holder.release();
        }
      }

      if (OB_SUCC(ret)) {
        ObKVMemBlockHandle *new_mb_handle = NULL;
        if (OB_FAIL(alloc(policy, block_size, new_mb_handle))) {
          COMMON_LOG(WARN, "alloc failed", K(ret), K(block_size));
        } else if (ATOMIC_BCAS((uint64_t*)(&get_curr_mb(policy)), (uint64_t)mb_handle, (uint64_t)new_mb_handle)) {
          if (NULL != mb_handle) {
            mb_handle->set_full(global_status_.base_mb_score_);
          }
        } else if (OB_FAIL(free(new_mb_handle))) {
          COMMON_LOG(ERROR, "free failed", K(ret));
        }
      }
    } while (OB_SUCC(ret));
  }

  if (OB_FAIL(ret)) {
    kvpair = NULL;
    hazptr_holder.reset();
  }
  return ret;
}

ObKVMemBlockHandle *&ObKVCacheStore::get_curr_mb(
    const enum ObKVCachePolicy policy)
{
  return active_mb_handles_[policy];
}

bool ObKVCacheStore::mb_status_match(
    const enum ObKVCachePolicy policy, ObKVMemBlockHandle *mb_handle)
{
  return policy == mb_handle->policy_;
}


int ObKVCacheStore::refresh_score()
{
  int ret = OB_SUCCESS;
  int64_t i = 0;
  double score = 0;

  /// refresh base_mb_score_
  const int64_t mb_cnt = ATOMIC_LOAD(&global_status_.lru_mb_cnt_) + ATOMIC_LOAD(&global_status_.lfu_mb_cnt_);
  const int64_t total_hit_cnt = global_status_.total_hit_cnt_.value();
  double avg_hit = 0;
  if (mb_cnt > 0) {
    avg_hit = double (total_hit_cnt - global_status_.last_hit_cnt_) / (double) mb_cnt;
  }
  global_status_.last_hit_cnt_ = total_hit_cnt;
  global_status_.base_mb_score_ = global_status_.base_mb_score_ * CACHE_SCORE_DECAY_FACTOR + avg_hit;

  /// refresh score of every mb_handle
  HazptrHolder hazptr_holder;
  bool protect_success;
  for (i = 0; OB_SUCC(ret) && i < cur_mb_num_; i++) {
    ObKVMemBlockHandle &handle = mb_handle_array_.get_mb_handle(i);
    if (OB_FAIL(hazptr_holder.protect(protect_success, &handle))) {
      COMMON_LOG(WARN, "failed to protect mb_handle");
    } else if (protect_success) {
      score = handle.score_ * CACHE_SCORE_DECAY_FACTOR + (double)(handle.recent_get_cnt_);
      handle.score_ = score;
      ATOMIC_STORE(&handle.recent_get_cnt_, 0);
      hazptr_holder.release();
    }
  }
  return ret;
}

void ObKVCacheStore::WashCallBack::operator()(ObKVMemBlockHandle *mb_handle)
{
  freed_mem_size_ += mb_handle->mem_block_->get_hold_size();
  store_.free_mbhandle(mb_handle, true);
}

bool ObKVCacheStore::wash()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t global_wash_size = 0;

  // Record time cost of every step of wash
  int64_t compute_wash_size_time = 0;
  int64_t refresh_score_time = 0;
  // int64_t wash_sort_time = 0;
  int64_t wash_time = 0;
  int64_t reclaim_time = 0;
  int64_t start_time = 0;
  int64_t current_time = 0;
  uint64_t reclaimed_size = 0;

  if (-1 == wash_itid_) {
    wash_itid_ = get_itid();
  }
  lib::ObMutexGuard guard(wash_out_lock_);

  //compute the wash size of each tenant
  start_time = ObTimeUtility::current_time();

  compute_global_wash_size(global_wash_size);
  current_time = ObTimeUtility::current_time();
  compute_wash_size_time = current_time - start_time;
  start_time = current_time;

  // refresh score of every mb_handle
  // ignore
  refresh_score();
  current_time = ObTimeUtility::current_time();
  refresh_score_time = current_time - start_time;
  start_time = current_time;
  tmp_washbale_size_info_.reuse();

  WashHeap global_wash_heap;
  int64_t heap_size = global_wash_size / block_size_;
  if (heap_size > 0) {
    if (OB_FAIL(init_wash_heap(global_wash_heap, heap_size))) {
      COMMON_LOG(WARN, "init_wash_heap failed", K(ret), K(heap_size));
    }
  }

  //sort mb_handles to wash
  HazptrHolder hazptr_holder;
  bool protect_success = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < cur_mb_num_; ++i) {
    ObKVMemBlockHandle &handle = mb_handle_array_.get_mb_handle(i);
    do {
      ret = hazptr_holder.protect(protect_success, &handle);
    } while (OB_UNLIKELY(OB_ALLOCATE_MEMORY_FAILED == ret));
    if (OB_FAIL(ret)) {
      COMMON_LOG(WARN, "failed to protect mb_handle");
    } else if (protect_success) {
      enum ObKVMBHandleStatus status = handle.get_status();
      if (FULL == status) {
        bool washed = false;
        // wash out all blocks with 0 score
        if (handle.score_ <= WASH_OUT_SCORE_THRESHOLD) {
          wash_mb(&handle);
          washed = true;
          if (global_wash_heap.heap_size_ > 0) {
            global_wash_heap.heap_size_--;
          }
        }
        if (!washed) {
          if (OB_TMP_FAIL(tmp_washbale_size_info_.add_washable_size(
                  OB_SERVER_TENANT_ID,
                  handle.mem_block_->get_hold_size()))) {
            COMMON_LOG(WARN,
                      "Fail to add tenant washable size",
                      K(tmp_ret),
                      K(OB_SERVER_TENANT_ID));
          }
          global_wash_heap.add(&handle);
        }
      }
      //any error should not break washing, so reset ret to OB_SUCCESS
      ret = OB_SUCCESS;
      hazptr_holder.release();
    }
  }
  if (OB_LIKELY(OB_SUCCESS == tmp_ret)) {
    washbale_size_info_.copy_from(tmp_washbale_size_info_);
  }

  //wash memory in tenant wash heap
  if (global_wash_heap.mb_cnt_ > 0) {
    wash_mbs(global_wash_heap);
    COMMON_LOG(INFO, "Wash memory globally, ",
        K(global_wash_size),
        "wash_heap_cnt", global_wash_heap.mb_cnt_);
  }
  wash_time = ObTimeUtility::current_time() - start_time;
  WashCallBack callback(*this, reclaimed_size);
  reclaim_time = ObTimeUtility::current_time();
  HazardDomain::get_instance().reclaim(callback);
  reclaim_time = ObTimeUtility::current_time() - reclaim_time;
  purge_mb_handle_retire_station();
  COMMON_LOG(INFO,
      "Wash time detail, ",
      K(compute_wash_size_time),
      K(refresh_score_time),
      K(wash_time),
      K(reclaim_time),
      K(reclaimed_size));

  return true;
}

int ObKVCacheStore::get_washable_size(const uint64_t tenant_id, int64_t &washable_size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(washbale_size_info_.get_size(tenant_id, washable_size))) {
    COMMON_LOG(WARN, "Fail to get tenant wash info", K(ret), K(tenant_id));
  }
  COMMON_LOG(DEBUG, "get washable size details", K(ret), K(tenant_id), K(washable_size));

  return ret;
}

void ObKVCacheStore::flush_washable_mbs()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheStore has not been inited", K(ret));
  } else {
    ObSEArray<uint64_t, FLUSH_PRESERVE_TENANT_NUM> tenant_ids;
    if (OB_FAIL(mem_limit_getter_->get_all_tenant_id(tenant_ids))) {
      COMMON_LOG(WARN, "Fail to get all tenant ids", K(ret));
    } else {
      uint64_t tenant_id = OB_INVALID_TENANT_ID;
      for (int64_t i = 0 ; i < tenant_ids.count() ; ++i) {
        int tmp_ret = OB_SUCCESS;
        if (OB_FAIL(tenant_ids.at(i, tenant_id))) {
          COMMON_LOG(WARN, "Fail to get tenant id, continue to flush rest tenants", K(ret), K(i));
        } else if (OB_TMP_FAIL(flush_washable_mbs(tenant_id))) {
          COMMON_LOG(WARN, "Fail to flush tenant washable memblock", K(tmp_ret));
        }
      }
    }
  }

}

int ObKVCacheStore::flush_washable_mbs(const uint64_t tenant_id, const bool force_flush)
{
  int ret = OB_SUCCESS;

  ObICacheWasher::ObCacheMemBlock *flush_blocks = nullptr;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheStore has not been inited", K(ret));
  } else if (OB_UNLIKELY(tenant_id <= OB_INVALID_TENANT_ID)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument", K(ret), K(tenant_id));
  } else if (force_flush) {
    lib::ObMutexGuard guard(wash_out_lock_);
    if (OB_FAIL(try_flush_washable_mb(tenant_id, flush_blocks, INT64_MAX, force_flush))) {
      COMMON_LOG(WARN, "Fail to try flush mb", K(ret), K(tenant_id), K(force_flush));
    }
  } else if (OB_FAIL(try_flush_washable_mb(tenant_id, flush_blocks, INT64_MAX, force_flush))) {
    COMMON_LOG(WARN, "Fail to try flush mb", K(ret), K(tenant_id), K(force_flush));
  }

  return ret;
}

bool ObKVCacheStore::add_handle_ref(ObKVMemBlockHandle *mb_handle, const int64_t seq_num) const
{
  bool bret = false;
  if (NULL != mb_handle) {
    if (seq_num != mb_handle->get_seq_num()) {
      bret = false;
    } else {
      ATOMIC_FAA(&mb_handle->ref_cnt_, 1);
      if (seq_num != ATOMIC_LOAD_RLX(&mb_handle->seq_num_)) {
        ATOMIC_SAF(&mb_handle->ref_cnt_, 1);
        bret = false;
      } else {
        bret = true;
      }
    }
  }
  return bret;
}

bool ObKVCacheStore::add_handle_ref(ObKVMemBlockHandle *mb_handle) const
{
  bool bret = false;
  if (NULL != mb_handle) {
    if (FREE == mb_handle->get_status()) {
      bret = false;
    } else {
      ATOMIC_FAA(&mb_handle->ref_cnt_, 1);
      if (FREE == ATOMIC_LOAD_RLX(&mb_handle->status_)) {
        ATOMIC_SAF(&mb_handle->ref_cnt_, 1);
        bret = false;
      } else {
        bret = true;
      }
    }
  }
  return bret;
}


int64_t ObKVCacheStore::de_handle_ref(ObKVMemBlockHandle *mb_handle, const bool do_retire)
{
  int64_t ref_cnt = 0;
  if (0 == (ref_cnt = ATOMIC_SAF(&mb_handle->ref_cnt_, 1))) {
    int tmp_ret = 0;
    if (OB_TMP_FAIL(free_mbhandle(mb_handle, do_retire))) {
      COMMON_LOG_RET(WARN, tmp_ret, "free_mbhandle failed");
    }
  }
  return ref_cnt;
}

int ObKVCacheStore::sync_wash_mbs(const uint64_t tenant_id,
                                  const int64_t size_to_wash,
                                  ObICacheWasher::ObCacheMemBlock *&wash_blocks)
{
  int ret = OB_SUCCESS;

  if (!inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "not init", K(ret));
  } else if (OB_INVALID_ID == tenant_id || size_to_wash <= 0) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments", K(ret), K(tenant_id), K(size_to_wash));
  } else if (OB_FAIL(try_flush_washable_mb(tenant_id, wash_blocks, size_to_wash))) {
    if (ret != OB_CACHE_FREE_BLOCK_NOT_ENOUGH) {
      COMMON_LOG(WARN, "Fail to try flush mb", K(ret), K(tenant_id));
    }
  } else if (OB_ISNULL(wash_blocks)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(ERROR, "wash_blocks is null!");
  }

  return ret;
}

void ObKVCacheStore::SyncWashCallBack::operator()(ObKVMemBlockHandle* handle)
{
  int ret = OB_SUCCESS;
  dl_del(handle);
  retire_list_.push(&handle->retire_link_);
  void* buf;
  int64_t hold_size;
  if (OB_FAIL(store_.do_wash_mb(handle, buf, hold_size))) {
    COMMON_LOG(WARN, "Fail to wash memblock", K(ret));
  } else if (size_washed_ >= size_to_wash_) {
    store_.free_mb(store_.mb_list_.resource_mgr_, OB_SYS_TENANT_ID, buf);
  } else {
    ObICacheWasher::ObCacheMemBlock* wash_block = new (buf) ObICacheWasher::ObCacheMemBlock();
    size_washed_ += hold_size;
    wash_block->next_ = wash_blocks_;
    wash_blocks_ = wash_block;
  }
}

void ObKVCacheStore::free_mbs(lib::ObTenantResourceMgrHandle& resource_handle, int64_t tenant_id,
    lib::ObICacheWasher::ObCacheMemBlock* wash_blocks)
{
  ObICacheWasher::ObCacheMemBlock* wash_block = wash_blocks;
  ObICacheWasher::ObCacheMemBlock* next = NULL;
  while (NULL != wash_block) {
    next = wash_block->next_;
    free_mb(resource_handle, tenant_id, reinterpret_cast<void*>(wash_block));
    wash_block = next;
  }
}

int ObKVCacheStore::try_flush_washable_mb(const uint64_t tenant_id, ObICacheWasher::ObCacheMemBlock*& wash_blocks,
    const int64_t size_to_wash, const bool force_flush)
{
  int ret = OB_SUCCESS;

  ObDLink *head = nullptr;
  if (NULL == (head = &mb_list_.head_)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Tenant memblock list is null", K(ret), K(tenant_id));
  } else {
    int64_t size_washed = 0;
    const int64_t start = ObTimeUtility::current_time();
    if (OB_FAIL(inner_flush_washable_mb(size_to_wash, size_washed, wash_blocks, force_flush))) {
      COMMON_LOG(WARN,
          "failed to inner flush washable mb",
          K(ret),
          K(tenant_id),
          K(size_to_wash),
          K(force_flush));
      free_mbs(mb_list_.resource_mgr_, tenant_id, wash_blocks);
      wash_blocks = nullptr;
    } else if (size_to_wash == INT64_MAX) {
      // flush
      free_mbs(mb_list_.resource_mgr_, tenant_id, wash_blocks);
      wash_blocks = nullptr;
    } else {
      // sync wash
      if (OB_SUCC(ret) && size_washed < size_to_wash) {
        ret = OB_CACHE_FREE_BLOCK_NOT_ENOUGH;
        INIT_SUCC(tmp_ret);
        if (TC_REACH_TIME_INTERVAL(3 * 1000 * 1000 /* 3s */)) {
          if (OB_TMP_FAIL(print_tenant_memblock_info(head))) {
            COMMON_LOG(WARN, "Fail to print tenant memblock info", K(tmp_ret));
          }
        }
        COMMON_LOG(INFO, "can not find enough memory block to wash", K(ret), K(size_washed), K(size_to_wash));
      }
      EVENT_ADD(KVCACHE_SYNC_WASH_TIME, ObTimeUtility::current_time() - start);
      EVENT_INC(KVCACHE_SYNC_WASH_COUNT);
    }

    if (OB_FAIL(ret)) {
      // free memory of memory blocks washed if any error occur
      free_mbs(mb_list_.resource_mgr_, tenant_id, wash_blocks);
      wash_blocks = nullptr;
    }

    COMMON_LOG(INFO,
        "ObKVCache try flush washable memblock details",
        K(ret),
        K(force_flush),
        K(tenant_id),
        K(size_washed),
        K(size_to_wash));
  }

  return ret;
}

int ObKVCacheStore::inner_flush_washable_mb(const int64_t size_to_wash, int64_t& size_washed,
    lib::ObICacheWasher::ObCacheMemBlock*& wash_blocks, bool force_flush)
{
  int ret = OB_SUCCESS;
  constexpr static int64_t check_interval = 512;
  const static int64_t retire_wash_retry_ratio = 2;
  const int64_t start = ObTimeUtility::current_time();
  int64_t size_retired = 0;
  int64_t size_to_retire = size_to_wash;
  int64_t check_idx = 0;
  HazardList retire_list;
  ObDLink* head = &mb_list_.head_;
  int64_t tenant_id = OB_SYS_TENANT_ID;
  if (OB_LIKELY(GCONF._enable_kvcache_hazard_pointer)) {
    // retire memblock and reclaim until
    // 1. wash out enough memory, or
    // 2. iterate over the whole tenant memblock list, or
    // 3. time out
    ObKVMemBlockHandle* handle = nullptr;
    HazptrHolder hazptr_holder;
    bool protect_success;
    while (OB_SUCC(ret) && size_washed < size_to_wash && head != handle) {
      {
        QClockGuard guard(get_qclock());
        handle = static_cast<ObKVMemBlockHandle*>(link_next(head));
        while (OB_SUCC(ret) && size_retired < size_to_retire && head != handle) {
          bool can_try_wash = false;
          int64_t status = -1;
          int64_t size;
          do {
            ret = hazptr_holder.protect(protect_success, handle);
          } while (OB_UNLIKELY(OB_ALLOCATE_MEMORY_FAILED == ret));
          if (OB_FAIL(ret)) {
            COMMON_LOG(WARN, "failed to protect mb_handle", KP(handle));
          }
          if (protect_success) {
            status = handle->get_status();
            if (FULL == status) {
              size = handle->mem_block_->get_size();
              can_try_wash = true;
            }
            hazptr_holder.release();
          }
          if (can_try_wash) {
            if (handle->retire()) {
              size_retired += size;
            }
          } else {
            if (force_flush && protect_success) {
              ret = OB_ERR_UNEXPECTED;
              COMMON_LOG(WARN,
                  "Can not sync wash memblock of erased tenant",
                  K(ret),
                  K(tenant_id),
                  KPC(handle),
                  K(status));
            }
          }
          handle = static_cast<ObKVMemBlockHandle*>(link_next(handle));

          if (!force_flush && check_idx > 0 && 0 == check_idx % check_interval) {
            const int64_t cost = ObTimeUtility::current_time() - start;
            if (cost > SYNC_WASH_MB_TIMEOUT_US) {
              ret = OB_SYNC_WASH_MB_TIMEOUT;
              COMMON_LOG(WARN, "sync wash mb timeout", K(cost), LITERAL_K(SYNC_WASH_MB_TIMEOUT_US));
            }
          }
          ++check_idx;
        }
      }  // qclock guard

      if (OB_FAIL(ret)) {
      } else if (size_retired >= size_to_wash - size_washed || size_to_wash == INT64_MAX) {
        // do recliam if has retired enough memory
        int64_t start_time = ObTimeUtility::current_time();
        SyncWashCallBack callback(*this, retire_list, wash_blocks, size_washed, size_to_wash, tenant_id);
        // avoid reclaiming while holding qclock
        HazardDomain::get_instance().reclaim(callback);
        int64_t reclaim_time = ObTimeUtility::current_time() - start_time;
        COMMON_LOG(INFO, "KVCache sync wash / flush reclaim", K(size_washed), K(size_to_wash), K(reclaim_time));
        if (size_to_wash == INT64_MAX) {
          // flush
          break;
        } else if (size_washed < size_to_wash) {
          size_to_retire = (size_to_wash - size_washed) * retire_wash_retry_ratio;
          size_retired = 0;
        }
      }
    }
  } else {
    // try wash memblock that can be washed until
    // 1. wash out enough memory, or
    // 2. iterate over the whole tenant memblock list, or
    // 3. time out
    QClockGuard guard(get_qclock());
    ObKVMemBlockHandle* handle = static_cast<ObKVMemBlockHandle*>(link_next(head));
    while (OB_SUCC(ret) && size_washed < size_to_wash && head != handle) {
      bool can_try_wash = false, add_ref_success;
      int64_t ref_cnt = -1;
      int64_t status = -1;
      if ((add_ref_success = add_handle_ref(handle))) {
        status = handle->get_status();
        ref_cnt = handle->ref_cnt_;
        if (FULL == status && 2 == ref_cnt) {
          can_try_wash = true;
        }
        if (0 == de_handle_ref(handle)) {
          can_try_wash = false;
        }
      }
      if (can_try_wash) {
        void* buf = nullptr;
        int64_t mb_size = 0;
        if (try_wash_mb(handle, tenant_id, buf, mb_size)) {
          if (nullptr == buf) {
            ret = OB_ERR_UNDEFINED;
            COMMON_LOG(ERROR, "Try wash memblock is null", K(ret), K(tenant_id));
          } else {
            size_washed += mb_size;
            ObICacheWasher::ObCacheMemBlock* mem_block = new (buf) ObICacheWasher::ObCacheMemBlock();
            mem_block->next_ = wash_blocks;
            wash_blocks = mem_block;
          }
          dl_del(handle);
          retire_list.push(&handle->retire_link_);
        } else if (force_flush) {
          ret = OB_ERR_UNEXPECTED;
          COMMON_LOG(WARN,
              "Fail to try wash memblock.",
              K(ret),
              K(tenant_id),
              KPC(handle),
              K(status),
              K(ref_cnt));
        }
      } else if (force_flush && add_ref_success) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN,
            "Can not sync wash memblock of erased tenant",
            K(ret),
            K(tenant_id),
            KPC(handle),
            K(status),
            K(ref_cnt));
      }
      handle = static_cast<ObKVMemBlockHandle*>(link_next(handle));

      if (!force_flush && check_idx > 0 && 0 == check_idx % check_interval) {
        const int64_t cost = ObTimeUtility::current_time() - start;
        if (cost > SYNC_WASH_MB_TIMEOUT_US) {
          ret = OB_SYNC_WASH_MB_TIMEOUT;
          COMMON_LOG(WARN, "sync wash mb timeout", K(cost), LITERAL_K(SYNC_WASH_MB_TIMEOUT_US));
        }
      }
      ++check_idx;
    }
  }
  retire_mb_handles(retire_list, true /* do retire */);
  return ret;
}

int ObKVCacheStore::inner_push_memblock_info(const ObKVMemBlockHandle &handle, ObIArray<ObKVCacheStoreMemblockInfo> &memblock_infos, int64_t tenant_id)
{
  INIT_SUCC(ret);
  ObKVStoreMemBlock* memblock = handle.mem_block_;
  ObKVCacheStoreMemblockInfo mb_info;
  mb_info.ref_count_ = ATOMIC_LOAD_RLX(&handle.ref_cnt_);
  mb_info.using_status_ = handle.get_status();
  mb_info.policy_ = handle.policy_;
  mb_info.kv_cnt_ = handle.kv_cnt_;
  mb_info.get_cnt_ = handle.get_cnt_;
  mb_info.recent_get_cnt_ = handle.recent_get_cnt_;
  mb_info.score_ = handle.score_;
  mb_info.align_size_ = memblock->get_hold_size();
  if (OB_UNLIKELY(0 > snprintf(mb_info.memblock_ptr_, 32, "%p", memblock))) {
    ret = OB_IO_ERROR;
    COMMON_LOG(WARN, "Fail to snprintf memblock pointer", K(ret), K(errno), KERRNOMSG(errno));
  } else if (OB_FAIL(memblock_infos.push_back(mb_info))) {
    COMMON_LOG(WARN, "Fail to push memblock info", K(ret), K(mb_info));
  }

  return ret;
}

void ObKVCacheStore::purge_mb_handle_retire_station()
{
  HazardList reclaim_list;
  get_retire_station().purge(reclaim_list);
  reuse_mb_handles(reclaim_list);
}

int ObKVCacheStore::get_memblock_info(const uint64_t tenant_id, ObIArray<ObKVCacheStoreMemblockInfo> &memblock_infos)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheStore is not inited", K(ret));
  } else {
    bool protect_success;
    HazptrHolder hazptr_holder;
    for (int i = 0; OB_SUCC(ret) && i < cur_mb_num_; ++i) {
      ObKVMemBlockHandle& handle = mb_handle_array_.get_mb_handle(i);
      if (OB_FAIL(hazptr_holder.protect(protect_success, &handle))) {
        COMMON_LOG(WARN, "Failed to protect memblock", K(ret));
      } else if (!protect_success) {
      } else if (OB_FAIL(inner_push_memblock_info(handle, memblock_infos, tenant_id))) {
        COMMON_LOG(WARN, "Failed to inner push memblock info", K(ret));
      }
      if (protect_success) {
        hazptr_holder.release();
      }
    }
  }

  return ret;
}

int ObKVCacheStore::print_tenant_memblock_info(ObDLink* head)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheStore is not inited", K(ret));
  } else if (nullptr == head) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Unexpected nullptr", K(ret), KP(head));
  } else {
    ContextParam param;
    param.set_mem_attr(common::OB_SERVER_TENANT_ID, ObModIds::OB_TEMP_VARIABLES);
    CREATE_WITH_TEMP_CONTEXT(param) {
      static const int64_t BUFLEN = 1 << 18;
      char *buf = (char *)ctxalp(BUFLEN);
      HazardList retire_list;
      int64_t ctx_pos = 0;
      if (nullptr == buf) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        COMMON_LOG(WARN, "Fail to allocate memory for print tenant memblock info", K(ret), KP(buf));
      } else {
        QClockGuard guard(get_qclock());
        ObKVMemBlockHandle *handle = static_cast<ObKVMemBlockHandle *>(link_next(head));
        HazptrHolder hazptr_holder;
        bool protect_success;
        while (OB_SUCC(ret) && head != handle) {
          if (OB_FAIL(hazptr_holder.protect(protect_success, handle))) {
            COMMON_LOG(WARN, "failed to protect mb_handle", KP(handle));
          } else if (protect_success) {
            if (OB_FAIL(databuff_printf(buf, BUFLEN, ctx_pos, 
                "[CACHE-SYNC-WASH] status=%8d | policy=%8d | kv_cnt=%8ld | get_cnt=%8ld | score=%8lf |\n",
                handle->get_status(),
                handle->policy_,
                handle->kv_cnt_,
                handle->get_cnt_,
                handle->score_))) {
              COMMON_LOG(WARN, "Fail to print tenant memblock info", K(ret), K(ctx_pos));
            }
            hazptr_holder.release();
          }
          handle = static_cast<ObKVMemBlockHandle *>(link_next(handle));
        }
      } // qclock guard
      if (OB_SUCC(ret)) {
        HazardDomain::get_instance().print_info();
        _OB_LOG(WARN, "[CACHE-SYNC-WASH] len: %8ld tenant sync wash failed, cache memblock info: \n%s", ctx_pos, buf);
      }
    }
  }
  return ret;
}

int ObKVCacheStore::alloc_mbhandle(const int64_t block_size, ObKVMemBlockHandle *&mb_handle)
{
  int ret = OB_SUCCESS;
  mb_handle = NULL;
  const enum ObKVCachePolicy policy = LRU;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(alloc_mbhandle(policy, block_size, mb_handle))) {
    LOG_WARN("alloc_mbhandle failed", K(ret), K(policy), K(block_size));
  }
  return ret;
}

int ObKVCacheStore::alloc_mbhandle(ObKVMemBlockHandle *&mb_handle)
{
  int ret = OB_SUCCESS;
  mb_handle = NULL;
  const enum ObKVCachePolicy policy = LRU;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(alloc_mbhandle(policy, block_size_, mb_handle))) {
    LOG_WARN("alloc_mbhandle failed", K(ret), K(policy), K_(block_size));
  }
  return ret;
}

int ObKVCacheStore::free_mbhandle(ObKVMemBlockHandle *mb_handle, const bool do_retire)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (NULL == mb_handle) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(mb_handle));
  } else {
    void *buf = NULL;
    int64_t mb_size = 0;
    const uint64_t tenant_id = OB_SYS_TENANT_ID;
    if (OB_FAIL(do_wash_mb(mb_handle, buf, mb_size))) {
      COMMON_LOG(ERROR, "do_wash_mb failed", K(ret));
    } else {
      free_mb(mb_list_.resource_mgr_, tenant_id, buf);
      if (OB_FAIL(remove_mb_handle(mb_handle, do_retire))) {
        COMMON_LOG(WARN, "remove_mb failed", K(ret));
      }
    }
  }
  return ret;
}


int ObKVCacheStore::alloc_mbhandle(
  const enum ObKVCachePolicy policy,
  const int64_t block_size,
  ObKVMemBlockHandle *&mb_handle)
{
  int ret = OB_SUCCESS;
  mb_handle = NULL;
  ObKVStoreMemBlock *mem_block = NULL;
  char *buf = NULL;
  const uint64_t tenant_id = OB_SYS_TENANT_ID;
  const int64_t cache_store_size = ATOMIC_AAF(&global_status_.store_size_, block_size);

  if (!mb_list_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(ERROR, "mb_list_ is invalid", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (NULL == (buf = static_cast<char*>(alloc_mb(
            mb_list_.resource_mgr_, tenant_id, block_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(WARN, "Fail to allocate memory, ", K(block_size), K(ret));
  } else {
    mem_block = new (buf) ObKVStoreMemBlock(buf + sizeof(ObKVStoreMemBlock),
        block_size - sizeof(ObKVStoreMemBlock));
    while (OB_FAIL(mb_handles_pool_.pop(mb_handle))) {
      if (OB_UNLIKELY(!try_supply_mb(SUPPLY_MB_NUM_ONCE))) {
        break;
      }
    }

    if (OB_FAIL(ret)) {
      mem_block->~ObKVStoreMemBlock();
      free_mb(mb_list_.resource_mgr_, tenant_id, mem_block);
      COMMON_LOG(WARN, "Fail to pop mb_handle, ", K(ret));
    } else {
      if (LRU == policy) {
        (void) ATOMIC_AAF(&global_status_.lru_mb_cnt_, 1);
      } else {
        (void) ATOMIC_AAF(&global_status_.lfu_mb_cnt_, 1);
      }
      mb_handle->policy_ = policy;
      mb_handle->mem_block_ = mem_block;
      mb_handle->last_modified_time_us_ = ObTimeUtility::current_time_us();
      if (OB_UNLIKELY(!GCONF._enable_kvcache_hazard_pointer)) {
        ATOMIC_AAF(&mb_handle->ref_cnt_, 1);
      } else {
        WEAK_BARRIER();
      }
      ATOMIC_STORE_RLX(&mb_handle->status_, ObKVMBHandleStatus::USING);
    }
  }

  if (OB_SUCC(ret)) {
    ObDLink *head = NULL;
    if (NULL == (head = &mb_list_.head_)) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "mb_list_.head_ is null", K(ret));
    } else if (OB_FAIL(insert_mb_handle(head, mb_handle))) {
      COMMON_LOG(WARN, "insert_mb_handle failed", K(ret));
    }
  } else {
    ATOMIC_SAF(&global_status_.store_size_, block_size);
  }

  return ret;
}

bool ObKVCacheStore::compute_global_wash_size(int64_t &wash_size)
{
  bool is_wash_valid = false;
  wash_size = 0;

  // seekdb runs with only OB_SYS_TENANT_ID, so washing against the process-wide
  // memory budget is the intended policy after removing tenant-partitioned accounting.
  const int64_t memory_limit = lib::get_memory_limit();
  int64_t reserve_mem = 0;
  if (memory_limit <= 1024L * 1024L * 1024L) {
    reserve_mem = memory_limit / 10;
  } else {
    reserve_mem = log10(static_cast<double>(memory_limit)/(1024.0 * 1024.0 * 1024.0)) * memory_limit / 20
                  + 100L * 1024L * 1024L;
  }
  reserve_mem = MAX(reserve_mem, lib::ob_get_reserved_memory());
  int64_t sys_total_wash_size = MAX(lib::get_memory_used() - memory_limit + reserve_mem, 0);

  if (sys_total_wash_size > 0) {
    wash_size = sys_total_wash_size;
  }

  int64_t total_global_wash_block_count = wash_size / block_size_;
  int64_t global_cache_size = global_status_.store_size_;

  if (is_global_wash_valid(total_global_wash_block_count, global_cache_size)) {
    is_wash_valid = true;
  }

  COMMON_LOG(INFO, "Wash compute global wash size", K(is_wash_valid), K(sys_total_wash_size), K(global_cache_size), K(wash_size));
  return is_wash_valid;
}

bool ObKVCacheStore::is_global_wash_valid(const int64_t total_global_wash_block_count, const int64_t global_cache_size)
{
  int64_t threshold = global_cache_size / block_size_ >> GLOBAL_WASH_THRESHOLD_RATIO;
  if (threshold > MAX_GLOBAL_WASH_THRESHOLD) {
    threshold = MAX_GLOBAL_WASH_THRESHOLD;
  } else if (threshold < MIN_GLOBAL_WASH_THRESHOLD) {
    threshold = MIN_GLOBAL_WASH_THRESHOLD;
  }
  return total_global_wash_block_count >= threshold;
}

void ObKVCacheStore::wash_mbs(WashHeap &heap)
{
  if (OB_LIKELY(GCONF._enable_kvcache_hazard_pointer)) {
    uint64_t retired_mb_sizes[MAX_CACHE_NUM] = {0};
    uint64_t total_retired_size = 0;
    if (OB_NOT_NULL(heap.heap_) && OB_LIKELY(heap.mb_cnt_ > 0)) {
      ObLink* head = nullptr;
      ObLink* tail = nullptr;
      uint32_t seq_num;
      for (int64_t i = 0; i < heap.mb_cnt_; ++i) {
        if (FULL == heap.heap_[i]->get_status() && ATOMIC_BCAS(&heap.heap_[i]->status_, FULL, FREE)) {
          ATOMIC_STORE_RLX(&heap.heap_[i]->seq_num_, heap.heap_[i]->seq_num_ + 1);
          if (OB_ISNULL(tail)) {
            head = tail = &heap.heap_[i]->retire_link_;
          } else {
            tail->next_ = &heap.heap_[i]->retire_link_;
            tail = tail->next_;
          }
          total_retired_size += heap.heap_[i]->mem_block_->get_hold_size();
        }
      }
      if (OB_NOT_NULL(tail)) {
        tail->next_ = nullptr;
        ATOMIC_FAA(&global_status_.retired_size_, total_retired_size);
        HazardDomain::get_instance().retire(head, tail, total_retired_size);
      }
    }
  } else {
    for (int64_t i = 0; i < heap.mb_cnt_; ++i) {
      wash_mb(heap.heap_[i]);
    }
  }
}

void ObKVCacheStore::wash_mb(ObKVMemBlockHandle* mb_handle)
{
  if (OB_NOT_NULL(mb_handle)) {
    if (OB_LIKELY(GCONF._enable_kvcache_hazard_pointer)) {
      mb_handle->retire();
    } else {
      if (ATOMIC_BCAS(&mb_handle->status_, FULL, FREE)) {
        ATOMIC_STORE_RLX(&mb_handle->seq_num_, mb_handle->seq_num_ + 1);
        de_handle_ref(mb_handle);
      }
    }
  }
}

bool ObKVCacheStore::try_wash_mb(ObKVMemBlockHandle *mb_handle, const uint64_t tenant_id, void *&buf, int64_t &mb_size)
{
  int ret = OB_SUCCESS;
  bool block_washed = false;
  uint32_t seq_num;
  if (NULL == mb_handle) {
    COMMON_LOG_RET(ERROR, common::OB_INVALID_ARGUMENT, "invalid arguments", KP(mb_handle));
  } else {
    if (FULL == mb_handle->get_status() && ATOMIC_BCAS(&mb_handle->status_, FULL, FREE)) {
      ATOMIC_STORE_RLX(&mb_handle->seq_num_, mb_handle->seq_num_ + 1);
      if (0 != ATOMIC_SAF(&mb_handle->ref_cnt_, 1)) {
      } else if (OB_FAIL(do_wash_mb(mb_handle, buf, mb_size))) {
        COMMON_LOG(ERROR, "do_wash_mb failed", K(ret));
      } else {
        block_washed = true;
      }
    }
  }
  return block_washed;
}

int ObKVCacheStore::do_wash_mb(ObKVMemBlockHandle *mb_handle, void *&buf, int64_t &mb_size)
{
  int ret = OB_SUCCESS;
  if (NULL == mb_handle) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(ERROR, "mb_handle is null", K(ret));
  } else if (NULL == mb_handle->mem_block_) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(ERROR, "mem_block_ is null", K(ret));
  } else {
    (void) ATOMIC_SAF(&global_status_.store_size_,
                      mb_handle->mem_block_->get_payload_size() + sizeof(ObKVStoreMemBlock));
    if (mb_handle->policy_ == LRU) {
      (void) ATOMIC_SAF(&global_status_.lru_mb_cnt_, 1);
    } else {
      (void) ATOMIC_SAF(&global_status_.lfu_mb_cnt_, 1);
    }
    buf = mb_handle->mem_block_;
    mb_size = mb_handle->mem_block_->get_hold_size();
    mb_handle->mem_block_->~ObKVStoreMemBlock();
    mb_handle->mem_block_ = NULL;
    mb_handle->last_modified_time_us_ = ObTimeUtility::current_time_us();
  }
  return ret;
}

int ObKVCacheStore::init_wash_heap(WashHeap &heap, const int64_t heap_size)
{
  int ret = OB_SUCCESS;
  heap.mb_cnt_ = 0;
  if (heap_size > 0) {
    heap.heap_size_ = heap_size;
    heap.heap_ = static_cast<ObKVMemBlockHandle **>(ob_malloc(heap_size * sizeof(ObKVMemBlockHandle *), ObModIds::OB_KVSTORE_CACHE_WASH_STRUCT));
    if (NULL == heap.heap_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(WARN, "init_wash_heap ob_malloc failed", K(ret), K(heap_size));
    }
  } else {
    heap.heap_size_ = 0;
    heap.heap_ = NULL;
  }
  return ret;
}

int ObKVCacheStore::prepare_wash_structs()
{
  int ret = OB_SUCCESS;
  const int64_t bucket_num = DEFAULT_TENANT_BUCKET_NUM;
  const char *label = ObModIds::OB_KVSTORE_CACHE_WASH_STRUCT;
  washable_size_allocator_.set_label(label);
  if (inited_) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "init twice", K(ret));
  } else if (OB_FAIL(washbale_size_info_.init(1/*tenant_node_size*/, bucket_num, washable_size_allocator_))) {
    COMMON_LOG(WARN, "Fail to init washable size info", K(ret));
  } else if (OB_FAIL(tmp_washbale_size_info_.init(1/*tenant_node_size*/, bucket_num, washable_size_allocator_))) {
    COMMON_LOG(WARN, "Fail to init tmp washable size info", K(ret));
  }

  return ret;
}

void ObKVCacheStore::destroy_wash_structs()
{
  washbale_size_info_.destroy();
  tmp_washbale_size_info_.destroy();
  washable_size_allocator_.reset();
}

void *ObKVCacheStore::alloc_mb(ObTenantResourceMgrHandle &resource_handle,
    const uint64_t tenant_id,
    const int64_t block_size)
{
  void *ptr = NULL;
  int ret = OB_SUCCESS;
  if (!resource_handle.is_valid() || OB_INVALID_ID == tenant_id || block_size <= 0 || block_size < block_size_) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments", K(ret), "handle valid", resource_handle.is_valid(),
      K(tenant_id), K(block_size), K_(block_size));
  } else if (NULL == (ptr = resource_handle.get_memory_mgr()->alloc_cache_mb(block_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(WARN, "failed to alloc cache mem block", K(tenant_id), K(block_size));
  }
  return ptr;
}

void ObKVCacheStore::free_mb(ObTenantResourceMgrHandle &resource_handle,
    const uint64_t tenant_id, void *ptr)
{
  if (NULL != ptr) {
    if (OB_INVALID_ID == tenant_id) {
      COMMON_LOG_RET(ERROR, common::OB_INVALID_ARGUMENT, "invalid tenant_id", K(tenant_id));
    } else if (!resource_handle.is_valid()) {
      COMMON_LOG_RET(ERROR, common::OB_INVALID_ARGUMENT, "invalid resource_handle");
    } else {
      resource_handle.get_memory_mgr()->free_cache_mb(ptr);
    }
  }
}

int ObKVCacheStore::insert_mb_handle(ObDLink *head, ObKVMemBlockHandle *mb_handle)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "not init", K(ret));
  } else if (NULL == head || NULL == mb_handle) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments", K(ret), KP(head), KP(mb_handle));
  } else {
    QClockGuard guard(get_qclock());
    dl_insert_before(head, mb_handle);
  }
  return ret;
}

int ObKVCacheStore::remove_mb_handle(ObKVMemBlockHandle *mb_handle, const bool do_retire)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "not init", K(ret));
  } else if (NULL == mb_handle) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments", K(ret), KP(mb_handle));
  } else {
    if (do_retire) {
      // default
      {
        QClockGuard guard(get_qclock());
        dl_del(mb_handle);
      }
      retire_mb_handle(mb_handle, do_retire);
    } else {
      // sync wash has already get qclock
      dl_del(mb_handle);
    }
  }
  return ret;
}

void ObKVCacheStore::retire_mb_handle(ObKVMemBlockHandle *mb_handle, const bool do_retire)
{
  if (NULL != mb_handle) {
    HazardList retire_list;
    retire_list.push(&mb_handle->retire_link_);
    retire_mb_handles(retire_list, do_retire);
  }
}

void ObKVCacheStore::retire_mb_handles(HazardList &retire_list, const bool do_retire)
{
  if (retire_list.size() > 0) {
    HazardList reclaim_list;
    int64_t retire_limit = do_retire ? RETIRE_LIMIT : INT64_MAX;
    if (wash_itid_ == get_itid()) {  // wash thread should not sync wash
      retire_limit = WASH_THREAD_RETIRE_LIMIT;
    }
    get_retire_station().retire(reclaim_list, retire_list, retire_limit);
    reuse_mb_handles(reclaim_list);
  }
}

void ObKVCacheStore::reuse_mb_handles(HazardList &reclaim_list)
{
  int ret = OB_SUCCESS;
  ObLink *p = NULL;
  // should continue even error occur
  while (NULL != (p = reclaim_list.pop())) {
    ObKVMemBlockHandle *mb_handle = CONTAINER_OF(p, ObKVMemBlockHandle, retire_link_);
    mb_handle->reset();
    if (OB_FAIL(mb_handles_pool_.push(mb_handle))) {
      COMMON_LOG(ERROR, "push mb_handle to pool failed", K(ret));
    }
  }
}

bool ObKVCacheStore::try_supply_mb(const int64_t mb_count)
{
  int ret = OB_SUCCESS;
  bool bool_ret = true;
  const int64_t old_num = ATOMIC_LOAD(&cur_mb_num_);
  const int64_t max_mb_num = mb_handle_array_.get_max_mb_num();
  if (old_num >= max_mb_num) {
    bool_ret = false;
  } else {
    const int64_t new_num = (old_num + mb_count <= max_mb_num ? old_num + mb_count : max_mb_num);
    
    if (!mb_handle_array_.ensure_blocks(old_num, new_num)) {
      bool_ret = false;
    } else if (ATOMIC_BCAS(&cur_mb_num_, old_num, new_num)) {
      for (int64_t i = old_num; OB_SUCCESS == ret && i < new_num; i++) {
        if (OB_FAIL(mb_handles_pool_.push(&(mb_handle_array_.get_mb_handle(i))))) {
          COMMON_LOG(ERROR, "supply mb failed", K(ret));
        }
      }
    } else {
      // other thread may have produced some mem block handles
    }
  }
  return bool_ret;
}

bool ObKVCacheStore::StoreMBHandleCmp::operator ()(
  const ObKVMemBlockHandle *a,
  const ObKVMemBlockHandle *b) const
{
  bool bret = false;
  if (NULL != a && NULL != b) {
    bret = a->score_ < b->score_;
  }
  return bret;
}

ObKVCacheStore::WashHeap::WashHeap()
  : heap_(NULL), heap_size_(0), mb_cnt_(0)
{
}

ObKVCacheStore::WashHeap::~WashHeap()
{
  reset();
}

ObKVMemBlockHandle *ObKVCacheStore::WashHeap::add(ObKVMemBlockHandle *mb_handle)
{
  StoreMBHandleCmp mb_cmp;
  ObKVMemBlockHandle *remove_handle = NULL;
  if (NULL != mb_handle && NULL != heap_ && (mb_cnt_ < heap_size_ || mb_cmp(mb_handle, heap_[0]))) {
    if (mb_cnt_ < heap_size_) {
      heap_[mb_cnt_++] = mb_handle;
    } else {
      std::pop_heap(&heap_[0], &heap_[mb_cnt_], mb_cmp);
      remove_handle = heap_[mb_cnt_ - 1];
      heap_[mb_cnt_ - 1] = mb_handle;
    }
    std::push_heap(&heap_[0], &heap_[mb_cnt_], mb_cmp);
  }
  return remove_handle;
}

void ObKVCacheStore::WashHeap::reset()
{
  if (NULL != heap_) {
    ob_free(heap_);
    heap_ = NULL;
  }
  heap_size_ = 0;
  mb_cnt_ = 0;
}

}//end namespace common
}//end namespace oceanbase
