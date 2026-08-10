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
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/statistic_event/ob_stat_event.h"
#include "ob_kvcache_store.h"

namespace oceanbase
{
using namespace lib;
namespace common
{

ObKVMemBlockHandle* mb_handles;


uint32_t handle_index_of(ObKVMemBlockHandle* mb_handle)
{
  return mb_handle - mb_handles;
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
  } else {
    if (OB_FAIL(key.deep_copy(reinterpret_cast<char *>(kvpair->key_), key_size, kvpair->key_))) {
    } else if (OB_FAIL(value.deep_copy(reinterpret_cast<char *>(kvpair->value_), value_size, kvpair->value_))) {
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

  if (OB_FAIL(alloc_kvpair_without_retry(key_size, value_size, kvpair, hazptr_holder, policy))) {
  }

  return ret;
}

ObKVCacheStore::ObKVCacheStore()
    : inited_(false),
      cur_mb_num_(0),
      max_mb_num_(0),
      block_size_(0),
      block_payload_size_(0),
      mb_handles_(NULL),
      mb_handles_pool_(),
      active_mb_handles_{NULL},
      global_status_(),
      wash_out_lock_(common::ObLatchIds::WASH_OUT_LOCK),
      washbale_size_info_(),
      tmp_washbale_size_info_(),
      wash_itid_(-1)
{
}

ObKVCacheStore::~ObKVCacheStore()
{
  destroy();
}

int ObKVCacheStore::init(const int64_t max_cache_size, const int64_t block_size)
{
  int ret = OB_SUCCESS;
  void *buf = NULL;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The ObKVCacheStore has been inited, ", K(ret));
  } else if (OB_UNLIKELY(max_cache_size <= block_size * 3)
      || OB_UNLIKELY(max_cache_size > MAX_CACHE_SIZE)
      || OB_UNLIKELY(block_size <= (int64_t)(sizeof(ObKVStoreMemBlock)))) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid arguments, ", K(max_cache_size),
      K(block_size), K(ret));
  } else {
    max_mb_num_ = compute_mb_handle_num(max_cache_size, block_size);
    if (NULL == (mb_handles_ = static_cast<ObKVMemBlockHandle*>(
                            buf = ob_malloc((sizeof(ObKVMemBlockHandle) + sizeof(ObKVMemBlockHandle*)) * max_mb_num_,
                                ObMemAttr("CACHE_MB_HANDLE", ObCtxIds::DEFAULT_CTX_ID))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(ERROR, "Fail to allocate memory for mb_handles_, ", K_(max_mb_num), K(ret));
    } else if (FALSE_IT(mb_handles = mb_handles_)) {
    } else if (OB_FAIL(mb_handles_pool_.init(max_mb_num_, (char*)(buf) + sizeof(ObKVMemBlockHandle) * max_mb_num_))) {
    } else {
      MEMSET(buf, 0, sizeof(ObKVMemBlockHandle) * max_mb_num_);
      block_size_ = block_size;
      block_payload_size_ = block_size - sizeof(ObKVStoreMemBlock);
      // prepare memory block handle
      (void)try_supply_mb(SUPPLY_MB_NUM_ONCE);
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(prepare_wash_structs())) {
    } else if (OB_FAIL(mb_list_.init())) {
    }
  }

  if (OB_SUCC(ret)) {
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

  if (NULL != mb_handles_) {
    for (int64_t i = 0; i < max_mb_num_; ++i) {
      if (FREE != mb_handles_[i].status_) {
        free_mb(mb_list_.resource_mgr_, mb_handles_[i].mem_block_);
      }
    }
    // free all mb handles cached by threads
    purge_mb_handle_retire_station();

    ob_free(mb_handles_);
    mb_handles_ = NULL;
    mb_list_.reset();
  }

  mb_handles_pool_.destroy();
  block_size_ = 0;
  block_payload_size_ = 0;

  destroy_wash_structs();
  cur_mb_num_ = 0;
  global_status_.reset();
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
  } else {
    mb_handle->retire();
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
    } else {
      if (OB_FAIL(hazptr_holder.protect(protect_success, mb_handle))) {
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
        } else if (ATOMIC_BCAS((uint64_t*)(&get_curr_mb(policy)), (uint64_t)mb_handle, (uint64_t)new_mb_handle)) {
          if (NULL != mb_handle) {
            mb_handle->set_full(global_status_.base_mb_score_);
          }
        } else if (OB_FAIL(free(new_mb_handle))) {
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
    if (OB_FAIL(hazptr_holder.protect(protect_success, &mb_handles_[i]))) {
    } else if (protect_success) {
      score = mb_handles_[i].score_ * CACHE_SCORE_DECAY_FACTOR + (double)(mb_handles_[i].recent_get_cnt_);
      mb_handles_[i].score_ = score;
      ATOMIC_STORE(&mb_handles_[i].recent_get_cnt_, 0);
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
  int64_t wash_size = 0;

  // Record time cost of every step of wash
  int64_t compute_wash_size_time = 0;
  int64_t wash_time = 0;
  int64_t reclaim_time = 0;
  int64_t start_time = 0;
  int64_t current_time = 0;
  uint64_t reclaimed_size = 0;

  if (-1 == wash_itid_) {
    wash_itid_ = get_itid();
  }
  lib::ObMutexGuard guard(wash_out_lock_);

  // Compute the amount by which KV store blocks exceed their fixed quota.
  start_time = ObTimeUtility::current_time();

  compute_wash_size(wash_size);
  current_time = ObTimeUtility::current_time();
  compute_wash_size_time = current_time - start_time;
  start_time = current_time;

  // compute base_mb_score_ (O(1) global stat update, was in refresh_score())
  const int64_t mb_cnt = ATOMIC_LOAD(&global_status_.lru_mb_cnt_) + ATOMIC_LOAD(&global_status_.lfu_mb_cnt_);
  const int64_t total_hit_cnt = global_status_.total_hit_cnt_.value();
  double avg_hit = 0;
  if (mb_cnt > 0) {
    avg_hit = double(total_hit_cnt - global_status_.last_hit_cnt_) / (double)mb_cnt;
  }
  global_status_.last_hit_cnt_ = total_hit_cnt;
  global_status_.base_mb_score_ = global_status_.base_mb_score_ * CACHE_SCORE_DECAY_FACTOR + avg_hit;

  tmp_washbale_size_info_.reuse();

  wash_heap_.mb_cnt_ = 0;
  int64_t heap_size = wash_size / block_size_;
  wash_heap_.heap_size_ = std::min(heap_size, WASH_HEAP_SIZE);

  // refresh score and sort mb_handles to wash in a single pass
  HazptrHolder hazptr_holder;
  bool protect_success = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < cur_mb_num_; ++i) {
    do {
      ret = hazptr_holder.protect(protect_success, &mb_handles_[i]);
    } while (OB_UNLIKELY(OB_ALLOCATE_MEMORY_FAILED == ret));
    if (OB_FAIL(ret)) {
    } else if (protect_success) {
      // refresh score inline (merged from refresh_score() to halve hazptr ops)
      double score = mb_handles_[i].score_ * CACHE_SCORE_DECAY_FACTOR + (double)(mb_handles_[i].recent_get_cnt_);
      mb_handles_[i].score_ = score;
      ATOMIC_STORE(&mb_handles_[i].recent_get_cnt_, 0);

      enum ObKVMBHandleStatus status = mb_handles_[i].get_status();
      if (FULL == status) {
        bool washed = false;
        // wash out all blocks with score at or below threshold
        if (score <= WASH_OUT_SCORE_THRESHOLD) {
          wash_mb(&mb_handles_[i]);
          washed = true;
          if (wash_heap_.heap_size_ > 0) {
            wash_heap_.heap_size_--;
          }
        }
        if (!washed) {
          if (OB_TMP_FAIL(tmp_washbale_size_info_.add_washable_size(
                  mb_handles_[i].mem_block_->get_hold_size()))) {
          }
          wash_heap_.add(&mb_handles_[i]);
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

  // Wash memory selected by the process-wide heap.
  if (wash_heap_.mb_cnt_ > 0) {
    wash_mbs(wash_heap_);
    COMMON_LOG(INFO, "Wash KV cache to fixed limit, ",
        K(wash_size),
        "wash_heap_cnt", wash_heap_.mb_cnt_);
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
      K(wash_time),
      K(reclaim_time),
      K(reclaimed_size));

  return reclaimed_size > 0;
}

int ObKVCacheStore::get_washable_size(int64_t &washable_size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(washbale_size_info_.get_size(washable_size))) {
  }

  return ret;
}

void ObKVCacheStore::flush_washable_mbs()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheStore has not been inited", K(ret));
  } else {
    {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(flush_washable_mbs(false))) {
      }
    }
  }

}

int ObKVCacheStore::flush_washable_mbs(const bool force_flush)
{
  int ret = OB_SUCCESS;

  ObICacheWasher::ObCacheMemBlock *flush_blocks = nullptr;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheStore has not been inited", K(ret));
  } else if (force_flush) {
    lib::ObMutexGuard guard(wash_out_lock_);
    if (OB_FAIL(try_flush_washable_mb(flush_blocks, INT64_MAX, force_flush))) {
    }
  } else if (OB_FAIL(try_flush_washable_mb(flush_blocks, INT64_MAX, force_flush))) {
  }

  return ret;
}

int ObKVCacheStore::sync_wash_mbs(const int64_t size_to_wash,
                                  ObICacheWasher::ObCacheMemBlock *&wash_blocks)
{
  int ret = OB_SUCCESS;

  if (!inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "not init", K(ret));
  } else if (size_to_wash <= 0) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments", K(ret), K(size_to_wash));
  } else if (OB_FAIL(try_flush_washable_mb(wash_blocks, size_to_wash))) {
    if (ret != OB_CACHE_FREE_BLOCK_NOT_ENOUGH) {
      COMMON_LOG(WARN, "Fail to try flush mb", K(ret));
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
  } else if (size_washed_ >= size_to_wash_) {
    store_.free_mb(store_.mb_list_.resource_mgr_, buf);
  } else {
    ObICacheWasher::ObCacheMemBlock* wash_block = new (buf) ObICacheWasher::ObCacheMemBlock();
    size_washed_ += hold_size;
    wash_block->next_ = wash_blocks_;
    wash_blocks_ = wash_block;
  }
}

void ObKVCacheStore::free_mbs(lib::ObResourceMgrHandle& resource_handle,
    lib::ObICacheWasher::ObCacheMemBlock* wash_blocks)
{
  ObICacheWasher::ObCacheMemBlock* wash_block = wash_blocks;
  ObICacheWasher::ObCacheMemBlock* next = NULL;
  while (NULL != wash_block) {
    next = wash_block->next_;
    free_mb(resource_handle, reinterpret_cast<void*>(wash_block));
    wash_block = next;
  }
}

int ObKVCacheStore::try_flush_washable_mb(ObICacheWasher::ObCacheMemBlock*& wash_blocks,
    const int64_t size_to_wash, const bool force_flush)
{
  int ret = OB_SUCCESS;

  ObDLink *head = nullptr;
  if (NULL == (head = &mb_list_.head_)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Memblock list is null", K(ret));
  } else {
    int64_t size_washed = 0;
    const int64_t start = ObTimeUtility::current_time();
    if (OB_FAIL(inner_flush_washable_mb(size_to_wash, size_washed, wash_blocks, force_flush))) {
      COMMON_LOG(WARN,
          "failed to inner flush washable mb",
          K(ret),
          K(size_to_wash),
          K(force_flush));
      free_mbs(mb_list_.resource_mgr_, wash_blocks);
      wash_blocks = nullptr;
    } else if (size_to_wash == INT64_MAX) {
      // flush
      free_mbs(mb_list_.resource_mgr_, wash_blocks);
      wash_blocks = nullptr;
    } else {
      // sync wash
      if (OB_SUCC(ret) && size_washed < size_to_wash) {
        ret = OB_CACHE_FREE_BLOCK_NOT_ENOUGH;
        INIT_SUCC(tmp_ret);
        if (TC_REACH_TIME_INTERVAL(3 * 1000 * 1000 /* 3s */)) {
          if (OB_TMP_FAIL(print_memblock_info(head))) {
          }
        }
        COMMON_LOG(INFO, "can not find enough memory block to wash", K(ret), K(size_washed), K(size_to_wash));
      }
    }

    if (OB_FAIL(ret)) {
      // free memory of memory blocks washed if any error occur
      free_mbs(mb_list_.resource_mgr_, wash_blocks);
      wash_blocks = nullptr;
    }

    COMMON_LOG(INFO,
        "ObKVCache try flush washable memblock details",
        K(ret),
        K(force_flush),
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

  // Retire memblocks and reclaim until enough memory is washed, the list has
  // been scanned, or the synchronous wash times out.
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
        } else if (force_flush && protect_success) {
          ret = OB_ERR_UNEXPECTED;
          COMMON_LOG(WARN,
              "Can not synchronously wash an active memblock",
              K(ret),
              KPC(handle),
              K(status));
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
      const int64_t start_time = ObTimeUtility::current_time();
      SyncWashCallBack callback(*this, retire_list, wash_blocks, size_washed, size_to_wash);
      HazardDomain::get_instance().reclaim(callback);
      const int64_t reclaim_time = ObTimeUtility::current_time() - start_time;
      COMMON_LOG(INFO, "KVCache sync wash / flush reclaim", K(size_washed), K(size_to_wash), K(reclaim_time));
      if (size_to_wash == INT64_MAX) {
        break;
      } else if (size_washed < size_to_wash) {
        size_to_retire = (size_to_wash - size_washed) * retire_wash_retry_ratio;
        size_retired = 0;
      }
    }
  }
  retire_mb_handles(retire_list, true /* do retire */);
  return ret;
}

int ObKVCacheStore::inner_push_memblock_info(const ObKVMemBlockHandle &handle, ObIArray<ObKVCacheStoreMemblockInfo> &memblock_infos)
{
  INIT_SUCC(ret);
  ObKVStoreMemBlock* memblock = handle.mem_block_;
  ObKVCacheStoreMemblockInfo mb_info;
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
  }

  return ret;
}

void ObKVCacheStore::purge_mb_handle_retire_station()
{
  HazardList reclaim_list;
  get_retire_station().purge(reclaim_list);
  reuse_mb_handles(reclaim_list);
}

int ObKVCacheStore::get_memblock_info(ObIArray<ObKVCacheStoreMemblockInfo> &memblock_infos)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheStore is not inited", K(ret));
  } else {
    bool protect_success;
    HazptrHolder hazptr_holder;
    for (int i = 0; OB_SUCC(ret) && i < cur_mb_num_; ++i) {
      ObKVMemBlockHandle& handle = mb_handles_[i];
      if (OB_FAIL(hazptr_holder.protect(protect_success, &handle))) {
      } else if (!protect_success) {
      } else if (OB_FAIL(inner_push_memblock_info(handle, memblock_infos))) {
      }
      if (protect_success) {
        hazptr_holder.release();
      }
    }
  }

  return ret;
}

int ObKVCacheStore::print_memblock_info(ObDLink* head)
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
    param.set_mem_attr(ObModIds::OB_TEMP_VARIABLES);
    CREATE_WITH_TEMP_CONTEXT(param) {
      static const int64_t BUFLEN = 1 << 18;
      char *buf = (char *)ctxalp(BUFLEN);
      HazardList retire_list;
      int64_t ctx_pos = 0;
      if (nullptr == buf) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        COMMON_LOG(WARN, "Fail to allocate memory for memblock diagnostics", K(ret), KP(buf));
      } else {
        QClockGuard guard(get_qclock());
        ObKVMemBlockHandle *handle = static_cast<ObKVMemBlockHandle *>(link_next(head));
        HazptrHolder hazptr_holder;
        bool protect_success;
        while (OB_SUCC(ret) && head != handle) {
          if (OB_FAIL(hazptr_holder.protect(protect_success, handle))) {
          } else if (protect_success) {
            if (OB_FAIL(databuff_printf(buf, BUFLEN, ctx_pos, 
                "[CACHE-SYNC-WASH] status=%8d | policy=%8d | kv_cnt=%8ld | get_cnt=%8ld | score=%8lf |\n",
                handle->get_status(),
                handle->policy_,
                handle->kv_cnt_,
                handle->get_cnt_,
                handle->score_))) {
            }
            hazptr_holder.release();
          }
          handle = static_cast<ObKVMemBlockHandle *>(link_next(handle));
        }
      } // qclock guard
      if (OB_SUCC(ret)) {
        HazardDomain::get_instance().print_info();
        _OB_LOG(WARN, "[CACHE-SYNC-WASH] len: %8ld sync wash failed, cache memblock info: \n%s", ctx_pos, buf);
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
    
    if (OB_FAIL(do_wash_mb(mb_handle, buf, mb_size))) {
    } else {
      free_mb(mb_list_.resource_mgr_, buf);
      if (OB_FAIL(remove_mb_handle(mb_handle, do_retire))) {
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
  bool release_reserved_size = false;
  mb_handle = NULL;
  ObKVStoreMemBlock *mem_block = NULL;
  char *buf = NULL;

  if (!mb_list_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(ERROR, "mb_list_ is invalid", K(ret));
  } else if (OB_FAIL(reserve_store_size(block_size))) {
  } else {
    release_reserved_size = true;
  }

  if (OB_FAIL(ret)) {
  } else if (NULL == (buf = static_cast<char*>(alloc_mb(
            mb_list_.resource_mgr_, block_size)))) {
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
      free_mb(mb_list_.resource_mgr_, mem_block);
      mem_block = NULL;
      buf = NULL;
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
      WEAK_BARRIER();
      ATOMIC_STORE_RLX(&mb_handle->status_, ObKVMBHandleStatus::USING);
      release_reserved_size = false;
    }
  }

  if (OB_SUCC(ret)) {
    ObDLink *head = NULL;
    if (NULL == (head = &mb_list_.head_)) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "mb_list_.head_ is null", K(ret));
    } else if (OB_FAIL(insert_mb_handle(head, mb_handle))) {
    }
  }

  if (release_reserved_size) {
    ATOMIC_SAF(&global_status_.store_size_, block_size);
  }

  return ret;
}

int ObKVCacheStore::reserve_store_size(const int64_t block_size)
{
  int ret = OB_SUCCESS;
  const int64_t cache_memory_limit = GMEMCONF.get_kvcache_memory_limit();
  const int64_t cache_limit = compute_fixed_cache_limit(cache_memory_limit, block_size_);

  if (block_size <= 0 || cache_limit <= 0 || block_size > cache_limit) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (!try_reserve_store_size(block_size, cache_limit)) {
    const int64_t cache_size = ATOMIC_LOAD(&global_status_.store_size_);
    const int64_t wash_size = cache_size >= cache_limit
        ? cache_size - cache_limit + block_size
        : block_size - (cache_limit - cache_size);
    int tmp_ret = OB_SUCCESS;
    ObICacheWasher::ObCacheMemBlock *wash_blocks = nullptr;
    if (OB_TMP_FAIL(sync_wash_mbs(wash_size, wash_blocks))) {
      if (OB_CACHE_FREE_BLOCK_NOT_ENOUGH != tmp_ret
          && OB_SYNC_WASH_MB_TIMEOUT != tmp_ret) {
        COMMON_LOG(WARN, "Fail to synchronously wash KV cache before allocating memblock",
            K(tmp_ret), K(block_size), K(cache_size), K(cache_limit), K(wash_size));
      }
    } else {
      free_mbs(mb_list_.resource_mgr_, wash_blocks);
    }

    if (!try_reserve_store_size(block_size, cache_limit)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  }

  return ret;
}

bool ObKVCacheStore::try_reserve_store_size(
    const int64_t block_size,
    const int64_t cache_limit)
{
  bool reserved = false;
  int64_t cache_size = ATOMIC_LOAD(&global_status_.store_size_);
  while (!reserved && can_reserve_cache_size(cache_size, block_size, cache_limit)) {
    reserved = ATOMIC_BCAS(
        &global_status_.store_size_, cache_size, cache_size + block_size);
    if (!reserved) {
      cache_size = ATOMIC_LOAD(&global_status_.store_size_);
    }
  }
  return reserved;
}

void ObKVCacheStore::compute_wash_size(int64_t &wash_size)
{
  const int64_t cache_memory_limit = GMEMCONF.get_kvcache_memory_limit();
  const int64_t cache_size = ATOMIC_LOAD(&global_status_.store_size_);
  const int64_t aligned_cache_limit =
      compute_fixed_cache_limit(cache_memory_limit, block_size_);
  wash_size = compute_fixed_wash_size(cache_size, cache_memory_limit, block_size_);
  COMMON_LOG(INFO, "Compute fixed KV cache wash size", K(cache_memory_limit),
             K(aligned_cache_limit), K(cache_size), K(wash_size));
}

void ObKVCacheStore::wash_mbs(WashHeap &heap)
{
  uint64_t total_retired_size = 0;
  if (OB_NOT_NULL(heap.heap_) && OB_LIKELY(heap.mb_cnt_ > 0)) {
    ObLink* head = nullptr;
    ObLink* tail = nullptr;
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
}

void ObKVCacheStore::wash_mb(ObKVMemBlockHandle* mb_handle)
{
  if (OB_NOT_NULL(mb_handle)) {
    mb_handle->retire();
  }
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
  if (inited_) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "init twice", K(ret));
  } else if (OB_FAIL(init_wash_heap(wash_heap_, WASH_HEAP_SIZE))) {
  }

  return ret;
}

void ObKVCacheStore::destroy_wash_structs()
{
  wash_heap_.reset();
  washbale_size_info_.destroy();
  tmp_washbale_size_info_.destroy();
}

void *ObKVCacheStore::alloc_mb(ObResourceMgrHandle &resource_handle,
    const int64_t block_size)
{
  void *ptr = NULL;
  int ret = OB_SUCCESS;
  if (!resource_handle.is_valid() || block_size <= 0 || block_size < block_size_) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments", K(ret), "handle valid", resource_handle.is_valid(), K(block_size), K_(block_size));
  } else if (NULL == (ptr = resource_handle.get_memory_mgr()->alloc_cache_mb(block_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(WARN, "failed to alloc cache mem block", K(block_size));
  }
  return ptr;
}

void ObKVCacheStore::free_mb(ObResourceMgrHandle &resource_handle, void *ptr)
{
  if (NULL != ptr) {
    if (!resource_handle.is_valid()) {
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
    }
  }
}

bool ObKVCacheStore::try_supply_mb(const int64_t mb_count)
{
  int ret = OB_SUCCESS;
  bool bool_ret = true;
  const int64_t old_num = ATOMIC_LOAD(&cur_mb_num_);
  if (old_num >= max_mb_num_) {
    bool_ret = false;
  } else {
    const int64_t new_num = (old_num + mb_count <= max_mb_num_ ? old_num + mb_count : max_mb_num_);
    if (ATOMIC_BCAS(&cur_mb_num_, old_num, new_num)) {
      for (int64_t i = old_num; OB_SUCCESS == ret && i < new_num; i++) {
        if (OB_FAIL(mb_handles_pool_.push(&(mb_handles_[i])))) {
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
  if (NULL != mb_handle && NULL != heap_
      && (mb_cnt_ < heap_size_ || (mb_cnt_ > 0 && mb_cmp(mb_handle, heap_[0])))) {
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
