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

#include "ob_kvcache_map.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/ob_running_mode.h"
#include "lib/time/ob_clock_generator.h"

namespace oceanbase
{
using namespace lib;

namespace common
{

ObKVCacheMap::ObKVCacheMap()
    : is_inited_(false),
      bucket_allocator_(ObMemAttr("CACHE_MAP_BKT", ObCtxIds::DEFAULT_CTX_ID)),
      node_allocator_(),
      bucket_start_pos_(0),
      bucket_num_(0),
      bucket_size_(0),
      buckets_(NULL),
      store_(NULL),
      global_hazard_station_()
{
}

ObKVCacheMap::~ObKVCacheMap()
{
}

int ObKVCacheMap::init(const int64_t bucket_num, ObKVCacheStore *store)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The ObKVCacheMap has been inited, ", K(ret));
  } else if (0 >= bucket_num || NULL == store) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid arguments, ", K(bucket_num), K(store), K(ret));
  } else if (OB_FAIL(bucket_lock_.init(bucket_num,
      ObLatchIds::KV_CACHE_BUCKET_LOCK, ObMemAttr("CACHE_MAP_LOCK", ObCtxIds::DEFAULT_CTX_ID)))) {
    COMMON_LOG(WARN, "Fail to init bucket lock, ", K(bucket_num), K(ret));
  } else if (OB_FAIL(global_hazard_station_.init(HAZARD_STATION_WAITING_THRESHOLD, HAZARD_STATION_SLOT_NUM))) {
    COMMON_LOG(WARN, "Fail to init hazard version, ", K(ret));
  } else if (OB_FAIL(node_allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                          ObMemAttr("CACHE_MAP_NODE"),
                                          1))) {
    COMMON_LOG(WARN, "Fail to init shared node allocator", K(ret));
  } else {
    bucket_size_ = DEFAULT_BUCKET_SIZE;
    if (is_mini_mode()) {
      const int64_t bucket_size_idx = lib::mini_mode_resource_ratio() * BUCKET_SIZE_ARRAY_LEN;
      bucket_size_ = BUCKET_SIZE_ARRAY[MIN(bucket_size_idx, BUCKET_SIZE_ARRAY_LEN - 1)];
    }
    const int64_t bucket_cnt = bucket_num % bucket_size_ == 0 ?
      bucket_num / bucket_size_ : bucket_num / bucket_size_ + 1;
    if (OB_ISNULL(buckets_ = static_cast<Bucket *>(bucket_allocator_.alloc(sizeof(Bucket) * bucket_cnt)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(WARN, "failed to allocate bucket array", K(ret), K(bucket_cnt));
    } else {
      MEMSET(buckets_, 0, sizeof(Bucket) * bucket_cnt);
      Node **all_nodes = static_cast<Node **>(bucket_allocator_.alloc(sizeof(Node *) * bucket_num));
      if (OB_ISNULL(all_nodes)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        COMMON_LOG(WARN, "failed to allocate all nodes", K(ret), K(bucket_num));
      } else {
        memset(all_nodes, 0, sizeof(Node *) * bucket_num);
        for (int64_t i = 0; i < bucket_cnt; ++i) {
          buckets_[i].nodes_ = all_nodes + i * bucket_size_;
        }
      }
    }
    if (OB_SUCC(ret)) {
      bucket_num_ = bucket_num;
      store_ = store;
      is_inited_ = true;
    }
  }

  if (!is_inited_) {
    destroy();
  }
  return ret;
}

void ObKVCacheMap::destroy()
{
  int ret = OB_SUCCESS;
  if (NULL != buckets_) {
    if (is_inited_) {
      ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
      if (OB_UNLIKELY(OB_SUCCESS != hazard_guard.get_ret())) {
        COMMON_LOG_RET(WARN, OB_ERR_UNEXPECTED, "Fail to acquire version", K(hazard_guard.get_ret()));
      } else {
        for (int64_t i = 0; i < bucket_num_; i++) {
          Node *&bucket_ptr = get_bucket_node(i);
          Node *iter = bucket_ptr;
          while (iter != NULL) {
            Node *tmp = iter;
            iter = iter->next_;
            global_hazard_station_.delete_node(hazard_guard.get_slot_id(), tmp);
          }
          iter = NULL;
        }
      }  // hazard version guard
      int tmp_ret = global_hazard_station_.retire();
      if (OB_SUCCESS != tmp_ret) {
        _OB_LOG(WARN, "Fail to retire hazard nodes before destroy, ret=%d", tmp_ret);
      }
    }
    const int64_t bucket_cnt = bucket_size_ == 0 ? 0 : (bucket_num_ % bucket_size_ == 0 ?
      bucket_num_ / bucket_size_ : bucket_num_ / bucket_size_ + 1);
    if (bucket_cnt > 0 && NULL != buckets_[0].nodes_) {
      bucket_allocator_.free(buckets_[0].nodes_);
      buckets_[0].nodes_ = NULL;
    }
    bucket_allocator_.free(buckets_);
    buckets_ = NULL;
  }
  global_hazard_station_.destroy();
  node_allocator_.destroy();
  bucket_lock_.destroy();
  bucket_num_ = 0;
  bucket_size_ = 0;
  store_ = NULL;
  is_inited_ = false;
}

// moved definition to the upper-layer owner cpp(real upper-layer symbol user, declaration remains in the header, transitional state)

int ObKVCacheMap::put(
  ObKVCacheInst &inst,
  const ObIKVCacheKey &key,
  const ObKVCachePair *kvpair,
  HazptrHolder &hazptr_holder,
  bool overwrite)
{
  int ret = OB_SUCCESS;

  Node *iter = NULL;
  Node *prev = NULL;
  uint64_t hash_code = 0;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(NULL == kvpair)
      || OB_UNLIKELY(!hazptr_holder.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", KP(kvpair), K(hazptr_holder), K(ret));
  } else if (OB_FAIL(key.hash(hash_code))) {
    COMMON_LOG(WARN, "Failed to get kvcache key hash", K(ret));
  } else {
    uint64_t bucket_pos = hash_code % bucket_num_;
    hash_code += inst.cache_id_;

    ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
    ObBucketWLockGuard guard(bucket_lock_, bucket_pos);
    if (OB_FAIL(hazard_guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to acquire hazard version", K(ret));
    } else if (OB_FAIL(guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to write lock bucket", K(ret), K(bucket_pos));
    } else {
      Node *&bucket_ptr = get_bucket_node(bucket_pos);
      iter = bucket_ptr;
      bool is_equal = false;
      HazptrHolder holder;
      bool protect_success;
      while (NULL != iter && OB_SUCC(ret)) {
        if (OB_FAIL(holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
          COMMON_LOG(WARN, "protect failed", KP(iter->mb_handle_));
        } else if(!protect_success) {
          (void) ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
          internal_map_erase(hazard_guard, prev, iter, bucket_ptr);
        } else {
          if (OB_NOT_NULL(iter->inst_->node_allocator_) && iter->inst_->node_allocator_->is_fragment(iter)) {
            internal_map_replace(hazard_guard, prev, iter, bucket_ptr);
          }
          if (hash_code == iter->hash_code_) {
            if (OB_FAIL(key.equal(*iter->key_, is_equal))) {
              COMMON_LOG(WARN, "Failed to check kvcache key equal", K(ret));
            } else if (is_equal) {
              if (!overwrite) {
                ret = OB_ENTRY_EXIST;
              }
              holder.release();
              break;
            }
          }
          holder.release();
          prev = iter;
          iter = iter->next_;
        }
      }
      if (OB_SUCC(ret)) {
        Node *new_node = NULL;
        void *buf = NULL;
        if (OB_ISNULL(inst.node_allocator_)) {
          ret = OB_ERR_UNEXPECTED;
          COMMON_LOG(ERROR, "Unexpected null node allocator", K(ret), K(inst.cache_id_));
        } else if (NULL == (buf = inst.node_allocator_->alloc(sizeof(Node)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(ERROR, "Fail to allocate memory, ", K(ret), "size", sizeof(Node));
        } else {
          new_node = new (buf) Node();
          // set new node
          
          new_node->inst_ = &inst;
          new_node->hash_code_ = hash_code;
          new_node->mb_handle_ = hazptr_holder.get_mb_handle();
          new_node->key_ = kvpair->key_;
          new_node->value_ = kvpair->value_;
          new_node->get_cnt_ = 1;
          new_node->seq_num_ = new_node->mb_handle_->get_seq_num_for_node();
          new_node->kvpair_size_ = kvpair->size_;

          // update mb_handle_ and inst
          if (NULL == iter) {
            // put new node
            (void) ATOMIC_AAF(&inst.status_.kv_cnt_, 1);
          }
          (void) ATOMIC_AAF(&new_node->mb_handle_->kv_cnt_, 1);
          (void) ATOMIC_AAF(&new_node->mb_handle_->get_cnt_, 1);
          ++new_node->mb_handle_->recent_get_cnt_;
          inst.status_.total_put_cnt_.inc();
          ATOMIC_AAF(&inst.status_.store_size_, kvpair->size_);

          // add new node to list
          new_node->next_ = bucket_ptr;
          (void) ATOMIC_SET(&bucket_ptr, new_node);

          // erase old node when overwrite
          if (NULL != iter) {
            internal_map_erase(hazard_guard, prev, iter, new_node->next_);
          }

        }
      }
    }  // write guard, hazard version guard
  }

  return ret;
}

int ObKVCacheMap::get(
    const int64_t cache_id,
    const ObIKVCacheKey &key,
    const ObIKVCacheValue *&pvalue,
    HazptrHolder &hazptr_holder)
{
  int ret = OB_SUCCESS;
  uint64_t hash_code = 0;


  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_FAIL(key.hash(hash_code))) {
    COMMON_LOG(WARN, "Failed to get kvcache key hash", K(ret));
  } else {
    uint64_t bucket_pos = hash_code % bucket_num_;
    hash_code += cache_id;

    Node *iter = NULL;
    Node *prev = NULL;
    int64_t iter_get_cnt = 0;
    int64_t mb_get_cnt = 0;
    int64_t mb_handle_kv_cnt = 0;
    ObKVCachePolicy mb_policy = LFU;

    ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
    if (OB_FAIL(hazard_guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to acquire hazard version", K(ret));
    } else {
      Node *&bucket_ptr = get_bucket_node(bucket_pos);
      iter = bucket_ptr;
      bool is_equal = false;
      while (NULL != iter && OB_SUCC(ret)) {
        __builtin_prefetch(iter,0);
        if (hash_code == iter->hash_code_) {
          bool protect_success;
          if (OB_FAIL(hazptr_holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
            COMMON_LOG(WARN, "failed to protect", KP(iter->mb_handle_));
          } else if (protect_success) {
            if (OB_FAIL(key.equal(*iter->key_, is_equal))) {
              COMMON_LOG(WARN, "Failed to check kvcache key equal", K(ret));
            } else if (is_equal) {
              pvalue = iter->value_;

              mb_get_cnt = ATOMIC_AAF(&iter->mb_handle_->get_cnt_, 1);
              mb_handle_kv_cnt = iter->mb_handle_->kv_cnt_;
              ++iter->mb_handle_->recent_get_cnt_;
              iter_get_cnt = ++ iter->get_cnt_;
              iter->inst_->status_.total_hit_cnt_.inc();
              mb_policy = iter->mb_handle_->policy_;

              break;
            }
            hazptr_holder.release();
          }
        }
        iter = iter->next_;
      }

      int tmp_ret = OB_SUCCESS;
      if (OB_FAIL(ret)) {
      } else if (NULL == iter) {
        ret = OB_ENTRY_NOT_EXIST;
      } else if (OB_UNLIKELY(mb_handle_kv_cnt < 0)) {
        tmp_ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(ERROR, "unexpected kv cnt", K(tmp_ret), K(mb_handle_kv_cnt), KPC(iter->mb_handle_));
      } else {
        if (LRU == mb_policy && need_modify_cache(iter_get_cnt, mb_get_cnt, mb_handle_kv_cnt)) {
          ObBucketWLockGuard guard(bucket_lock_, bucket_pos);
          if (OB_TMP_FAIL(guard.get_ret())) {
            COMMON_LOG(WARN, "Fail to write lock bucket, ", K(tmp_ret), K(bucket_pos));
          } else {
            Node *curr = get_bucket_node(bucket_pos);
            bucket_ptr = curr;
            prev = NULL;
            while (nullptr != curr) {
              if (curr == iter) {
                if (OB_TMP_FAIL(internal_data_move(hazard_guard, prev, iter, bucket_ptr))) {
                  COMMON_LOG(WARN, "Fail to move node to LFU block, ", K(tmp_ret));
                }
                break;
              }
              prev = curr;
              curr = curr->next_;
            }
          }
        }
      }
    }  // hazard version guard
  }

  return ret;
}

int ObKVCacheMap::erase(const int64_t cache_id, const ObIKVCacheKey &key)
{
  int ret = OB_SUCCESS;
  uint64_t hash_code = 0;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_FAIL(key.hash(hash_code))) {
    COMMON_LOG(WARN, "Failed to get kvcache key hash", K(ret));
  } else {
    bool found = false;
    uint64_t bucket_pos = hash_code % bucket_num_;
    hash_code += cache_id;
    Node *iter = NULL;
    Node *prev = NULL;

    ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
    ObBucketWLockGuard guard(bucket_lock_, bucket_pos);
    if (OB_FAIL(hazard_guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to acquire hazard version", K(ret));
    } else if (OB_FAIL(guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to write lock bucket, ", K(ret), K(bucket_pos));
    } else {
      Node *&bucket_ptr = get_bucket_node(bucket_pos);
      iter = bucket_ptr;
      bool is_equal = false;
      HazptrHolder hazptr_holder;
      bool protect_success;
      while (NULL != iter && OB_SUCC(ret)) {
        if (OB_FAIL(hazptr_holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
          COMMON_LOG(WARN, "protect failed", KP(iter->mb_handle_));
        } else if (protect_success) {
          if (OB_NOT_NULL(iter->inst_->node_allocator_) && iter->inst_->node_allocator_->is_fragment(iter)) {
            internal_map_replace(hazard_guard, prev, iter, bucket_ptr);
          }
          if (hash_code == iter->hash_code_ && OB_SUCC(key.equal(*iter->key_, is_equal)) && is_equal) {
            (void) ATOMIC_SAF(&iter->mb_handle_->kv_cnt_, 1);
            (void) ATOMIC_SAF(&iter->mb_handle_->get_cnt_, iter->get_cnt_);
            (void) ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
            internal_map_erase(hazard_guard, prev, iter, bucket_ptr);
            found = true;
            break;
          } else {
            prev = iter;
            iter = iter->next_;
          }
          hazptr_holder.release();
        } else {
          (void) ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
          internal_map_erase(hazard_guard, prev, iter, bucket_ptr);
        }
      }
      if (OB_FAIL(ret)) {
        COMMON_LOG(ERROR, "Failed to check kvcache equal", K(ret));
      } else if (!found) {
        ret = OB_ENTRY_NOT_EXIST;
      }
    }  // write guard, hazard version guard
  }

  return ret;
}

int ObKVCacheMap::erase_all()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else {
    Node *iter = NULL;
    Node *erase_node = NULL;
    ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
    if (OB_FAIL(hazard_guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to acquire hazard version", K(ret));
    } else {
      HazptrHolder hazptr_holder;
      bool protect_success;
      for (int64_t i = 0; i < bucket_num_ && OB_SUCC(ret); i++) {
        ObBucketWLockGuard guard(bucket_lock_, i);
        if (OB_FAIL(guard.get_ret())) {
          COMMON_LOG(WARN, "Fail to write lock bucket, ", K(ret), K(i));
        } else {
          Node *&bucket_ptr = get_bucket_node(i);
          iter = bucket_ptr;
          bucket_ptr = NULL;
          while (NULL != iter) {
            if (OB_FAIL(hazptr_holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
              COMMON_LOG(WARN, "protect failed", KP(iter->mb_handle_));
            } else if (protect_success) {
              (void) ATOMIC_SAF(&iter->mb_handle_->kv_cnt_, 1);
              (void) ATOMIC_SAF(&iter->mb_handle_->get_cnt_, iter->get_cnt_);
              hazptr_holder.release();
            }
            (void) ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
            erase_node = iter;
            iter = iter->next_;
            global_hazard_station_.delete_node(hazard_guard.get_slot_id(), erase_node);
          }
        }
      }
    } // hazard version guard

    int temp_ret = global_hazard_station_.retire();
    if (OB_SUCCESS != temp_ret) {
      COMMON_LOG(WARN, "Fail to retire global hazard version", K(temp_ret));
    }
  }
  return ret;
}

int ObKVCacheMap::erase_all(const int64_t cache_id)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(cache_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument ", K(cache_id), K(ret));
  } else {
    Node *iter = NULL;
    Node *prev = NULL;
    ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
    if (OB_FAIL(hazard_guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to acquire hazard version", K(ret));
    } else {
      HazptrHolder hazptr_holder;
      bool protect_success;
      for (int64_t i = 0; i < bucket_num_ && OB_SUCC(ret); i++) {
        ObBucketWLockGuard guard(bucket_lock_, i);
        if (OB_FAIL(guard.get_ret())) {
          COMMON_LOG(WARN, "Fail to write lock bucket", K(ret), K(i));
        } else {
          Node *&bucket_ptr = get_bucket_node(i);
          prev = NULL;
          iter = bucket_ptr;
          while (NULL != iter && OB_SUCC(ret)) {
            if (cache_id == iter->inst_->cache_id_) {

              if (OB_FAIL(hazptr_holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
                COMMON_LOG(WARN, "protect failed", KP(iter->mb_handle_));
              } else if (protect_success) {
                (void)ATOMIC_SAF(&iter->mb_handle_->kv_cnt_, 1);
                (void)ATOMIC_SAF(&iter->mb_handle_->get_cnt_, iter->get_cnt_);
                hazptr_holder.release();
              }
              (void) ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
              internal_map_erase(hazard_guard, prev, iter, bucket_ptr);
            } else {
              prev = iter;
              iter = iter->next_;
            }
          }
        }
      }
    } // hazard version guard
    int temp_ret = global_hazard_station_.retire();
    if (OB_SUCCESS != temp_ret) {
      COMMON_LOG(WARN, "Fail to retire global hazard version", K(temp_ret));
    }
  }

  return ret;
}

int ObKVCacheMap::clean_garbage_node(int64_t &start_pos, const int64_t clean_num)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(start_pos < 0)
      || OB_UNLIKELY(start_pos >= bucket_num_)
      || OB_UNLIKELY(clean_num <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(start_pos), K_(bucket_num), K(clean_num), K(ret));
  } else {
    int64_t clean_node_count = 0;
    ObTimeGuard tg("clean_garbage_node", 100000);
    // The variable 'clean_start_pos' do not need atomic operation because it is only used by wash thread
    int64_t clean_start_pos = start_pos % bucket_num_;
    int64_t clean_end_pos = MIN(clean_num + clean_start_pos, bucket_num_);
    Node *iter = NULL;
    Node *prev = NULL;
    ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
    if (OB_FAIL(hazard_guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to acquire hazard version", K(ret));
    } else {
      HazptrHolder hazptr_holder;
      bool protect_success;
      for (int64_t i = clean_start_pos; i < clean_end_pos && OB_SUCC(ret); i++) {
        // fast path: skip empty buckets without lock acquisition
        if (NULL == get_bucket_node(i)) {
          continue;
        }
        ObBucketWLockGuard guard(bucket_lock_, i);
        if (OB_FAIL(guard.get_ret())) {
          COMMON_LOG(WARN, "Fail to write lock bucket, ", K(ret), K(i));
        } else {
          Node *&bucket_ptr = get_bucket_node(i);
          // double-check: another thread could have emptied it between check and lock
          if (NULL == bucket_ptr) {
            continue;
          }
          prev = NULL;
          iter = bucket_ptr;
          while (NULL != iter) {
            if (OB_FAIL(hazptr_holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
              COMMON_LOG(WARN, "protect failed", KP(iter->mb_handle_));
            } else if (protect_success) {
              hazptr_holder.release();
              prev = iter;
              iter = iter->next_;
            } else {
              (void) ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
              internal_map_erase(hazard_guard, prev, iter, bucket_ptr);
              ++clean_node_count;
            }
          }
        }
      }
    }  // hazard version guard
    start_pos = clean_end_pos >= bucket_num_ ? 0 : clean_end_pos;
    int temp_ret = global_hazard_station_.retire();
    if (OB_SUCCESS != temp_ret) {
      COMMON_LOG(WARN, "Fail to retire global hazard version", K(temp_ret));
    }
    COMMON_LOG(INFO, "Cache wash clean map node details", K(ret), K(clean_node_count), "clean_time", tg.get_diff(),
        K(clean_start_pos), K(clean_num));
  }

  return ret;
}

int ObKVCacheMap::replace_fragment_node(int64_t &start_pos, int64_t &replace_node_count, const int64_t replace_num)
{
  int ret = OB_SUCCESS;
  replace_node_count = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(start_pos < 0)
      || OB_UNLIKELY(start_pos >= bucket_num_)
      || OB_UNLIKELY(replace_num <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(start_pos), K_(bucket_num), K(replace_num), K(ret));
  } else {
    ObTimeGuard tg("replace_fragement_node", 100000);
    // The variable 'replace_start_pos' do not need atomic operation because it is only used by replace thread
    int64_t replace_start_pos = start_pos % bucket_num_;
    int64_t replace_end_pos = MIN(replace_num + replace_start_pos, bucket_num_);
    Node *iter = NULL;
    Node *prev = NULL;
    ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
    if (OB_FAIL(hazard_guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to acquire hazard version", K(ret));
    } else {
      for (int64_t i = replace_start_pos; i < replace_end_pos && OB_SUCC(ret); i++) {
        // fast path: skip empty buckets without lock acquisition
        if (NULL == get_bucket_node(i)) {
          continue;
        }
        ObBucketWLockGuard guard(bucket_lock_, i);
        if (OB_FAIL(guard.get_ret())) {
          COMMON_LOG(WARN, "Fail to write lock bucket", K(ret), K(i));
        } else {
          const int64_t start = common::ObClockGenerator::getClock();
          Node *&bucket_ptr = get_bucket_node(i);
          // double-check: another thread could have emptied it between check and lock
          if (NULL == bucket_ptr) {
            continue;
          }
          prev = NULL;
          iter = bucket_ptr;
          int64_t node_count = 0;
          while (NULL != iter) {
            if (OB_NOT_NULL(iter->inst_->node_allocator_) && iter->inst_->node_allocator_->is_fragment(iter)) {
              internal_map_replace(hazard_guard, prev, iter, bucket_ptr);
              ++node_count;
            }
            prev = iter;
            iter = iter->next_;
            if (common::ObClockGenerator::getClock() - start >= 1 * 1000 * 1000) {
                  COMMON_LOG(INFO, "replace map node cost too much time", K(node_count), K(replace_node_count), K(replace_start_pos), K(i));
              break;
            }
          }
          replace_node_count += node_count;
        }
      }
    }  // hazard version guard
    start_pos = replace_end_pos >= bucket_num_ ? 0 : replace_end_pos;
    COMMON_LOG(INFO, "Cache replace map node details", K(ret), K(replace_node_count), "replace_time", tg.get_diff(),
        K(replace_start_pos), K(replace_num));
  }
  return ret;
}

void ObKVCacheMap::print_hazard_version_info()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap is not inited", K(ret));
  } else if (OB_FAIL(global_hazard_station_.print_current_status())) {
    COMMON_LOG(WARN, "Fail to print hazard version current status", K(ret));
  }
}

int ObKVCacheMap::multi_get(
  const int64_t cache_id,
  const int64_t pos,
  common::ObList<Node, common::ObArenaAllocator> &list)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(cache_id < 0)
      || OB_UNLIKELY(pos < 0)
      || OB_UNLIKELY(pos >= bucket_num_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(cache_id), K(pos), K_(bucket_num), K(ret));
  } else {
    ObKVCacheHazardGuard hazard_guard(global_hazard_station_);
    if (OB_FAIL(hazard_guard.get_ret())) {
      COMMON_LOG(WARN, "Fail to acquire hazard version", K(ret));
    } else {
      Node *iter = get_bucket_node(pos);
      HazptrHolder hazptr_holder;
      bool protect_success;
      while (NULL != iter && OB_SUCC(ret)) {
        if (cache_id == iter->inst_->cache_id_) {
          if (OB_FAIL(hazptr_holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
            COMMON_LOG(WARN, "protect failed", KP(iter->mb_handle_));
          } else if (protect_success) {
            list.push_back(*iter);
            hazptr_holder.release();
          }
        }
        iter = iter->next_;
      }
    }  // hazard version guard
  }

  return ret;
}

void ObKVCacheMap::internal_map_erase(const ObKVCacheHazardGuard &guard,
                                      Node *&prev,
                                      Node *&iter,
                                      Node *&bucket_ptr)
{
  // Remember to update kv_cnt of inst and mb_handle outside
  if (NULL != iter) {
    ATOMIC_SAF(&iter->inst_->status_.store_size_, iter->kvpair_size_);
    Node *erase_node = iter;
    if (NULL == prev) {
      bucket_ptr = iter->next_;
    } else {
      prev->next_ = iter->next_;
    }
    iter = iter->next_;
    global_hazard_station_.delete_node(guard.get_slot_id(), erase_node);
  }
}

void ObKVCacheMap::internal_map_replace(const ObKVCacheHazardGuard &guard,
                                        Node *&prev,
                                        Node *&iter,
                                        Node *&bucket_ptr)
{
  if (NULL != iter) {
    Node *new_node = NULL;
    void *buf = NULL;
    if (OB_NOT_NULL(iter->inst_->node_allocator_)
        && NULL != (buf = iter->inst_->node_allocator_->alloc(sizeof(Node)))) {
      new_node = new (buf) Node();
      *new_node = *iter;

      // ensure new_node has been inited
      WEAK_BARRIER();

      if (NULL == prev) {
        bucket_ptr = new_node;
      } else {
        prev->next_ = new_node;
      }
      Node *erase_node = iter;
      iter = new_node;
      global_hazard_station_.delete_node(guard.get_slot_id(), erase_node);
    }
  }
}

int ObKVCacheMap::internal_data_move(const ObKVCacheHazardGuard &guard,
                                     Node *&prev,
                                     Node *&old_iter,
                                     Node *&bucket_ptr)
{
  int ret = OB_SUCCESS;
  Node *new_node = NULL;
  void *buf = NULL;
  ObKVCachePair *new_kvpair = NULL;
  ObKVMemBlockHandle *new_mb_handle = NULL;
  HazptrHolder hazptr_holder;
  if (OB_ISNULL(old_iter->inst_->node_allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Unexpected null node allocator", K(ret), KPC(old_iter));
  } else if (NULL == (buf = old_iter->inst_->node_allocator_->alloc(sizeof(Node)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(WARN, "Fail to allocate memory for Node, ", K(ret), "size:", sizeof(Node));
  } else if (OB_FAIL(store_->store(*old_iter->key_, *old_iter->value_, new_kvpair, hazptr_holder, LFU))) {
    old_iter->inst_->node_allocator_->free(buf);
    COMMON_LOG(WARN, "Fail to move kvpair ", K(ret));
  } else {
    new_mb_handle = hazptr_holder.get_mb_handle();
    new_node = new(buf) Node();

    // set new node
    
    new_node->inst_ = old_iter->inst_;
    new_node->hash_code_ = old_iter->hash_code_;
    new_node->mb_handle_ = new_mb_handle;
    new_node->key_ = new_kvpair->key_;
    new_node->value_ = new_kvpair->value_;
    new_node->get_cnt_ = old_iter->get_cnt_;
    new_node->next_ = old_iter->next_;
    new_node->seq_num_ = new_node->mb_handle_->get_seq_num_for_node();
    new_node->kvpair_size_ = new_kvpair->size_;

    // update inst and mb_handle
    (void) ATOMIC_SAF(&old_iter->mb_handle_->kv_cnt_, 1);
    (void) ATOMIC_SAF(&old_iter->mb_handle_->get_cnt_, old_iter->get_cnt_);
    (void) ATOMIC_AAF(&new_mb_handle->kv_cnt_, 1);
    (void) ATOMIC_AAF(&new_mb_handle->get_cnt_, old_iter->get_cnt_);
    ++new_mb_handle->recent_get_cnt_;

    hazptr_holder.release();

    // ensure new node has been inited before put into bucket node list
    WEAK_BARRIER();

    // replace old node
    if (prev == NULL) {
      bucket_ptr = new_node;
    } else {
      prev->next_ = new_node;
    }
    global_hazard_station_.delete_node(guard.get_slot_id(), old_iter);
  }
  return ret;
}

void ObKVCacheMap::Node::retire()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(inst_) && OB_NOT_NULL(inst_->node_allocator_)) {
    inst_->node_allocator_->free(this);
  } else {
    _OB_LOG(ERROR, "Invalid node allocator when retire, inst=%p, node=%p", inst_, this);
  }
}

}//end namespace common
}//end namespace oceanbase
