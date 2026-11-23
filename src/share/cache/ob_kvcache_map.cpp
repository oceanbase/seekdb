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
#include "ob_kvcache_inst_map.h"
#include "lib/lock/ob_rwlock.h"
#include "lib/time/ob_clock_generator.h"
#include "lib/utility/utility.h"
#include "share/cache/ob_kvcache_hazard_domain.h"

namespace oceanbase
{
using namespace lib;

namespace common
{

struct ObKVCacheMap::Shard
{
  obsys::ObRWLock lock_;
  Node **buckets_;
  int64_t bucket_num_;
  int64_t size_;
  // Keep neighboring shard locks off the same cache line.
  char cache_line_pad_[64];
  Shard()
    : lock_(obsys::WRITE_PRIORITY),
      buckets_(NULL),
      bucket_num_(0),
      size_(0)
  {}
};

ObKVCacheMap::ObKVCacheMap()
    : is_inited_(false),
      node_allocator_(),
      bucket_num_(0),
      shard_num_(0),
      resize_count_(0),
      resize_us_(0),
      shards_(NULL),
      store_(NULL),
      inst_map_(NULL)
{
}

ObKVCacheMap::~ObKVCacheMap()
{
}

int64_t ObKVCacheMap::choose_shard_num()
{
  int64_t cpu_num = get_cpu_num();
  if (cpu_num <= 0) {
    cpu_num = 1;
  }
  if (cpu_num > SHARD_NUM_MAX) {
    cpu_num = SHARD_NUM_MAX;
  }
  int64_t shard_num = cpu_num * 4;
  if (shard_num < SHARD_NUM_MIN) {
    shard_num = SHARD_NUM_MIN;
  }
  if (shard_num > SHARD_NUM_MAX) {
    shard_num = SHARD_NUM_MAX;
  }
  int64_t pow2 = 1;
  while (pow2 < shard_num && pow2 <= (SHARD_NUM_MAX >> 1)) {
    pow2 <<= 1;
  }
  return pow2;
}

int64_t ObKVCacheMap::shard_index_of(const uint64_t raw_hash) const
{
  return static_cast<int64_t>((raw_hash >> 8) & static_cast<uint64_t>(shard_num_ - 1));
}

int64_t ObKVCacheMap::bucket_pos(const uint64_t raw_hash, const int64_t bucket_num)
{
  // Bucket counts stay powers of two, so this matches raw_hash % bucket_num.
  return static_cast<int64_t>(raw_hash & static_cast<uint64_t>(bucket_num - 1));
}

uint64_t ObKVCacheMap::raw_hash_of(const Node &node)
{
  return node.hash_code_ - static_cast<uint64_t>(node.inst_->cache_id_);
}

int ObKVCacheMap::init(const int64_t bucket_num, ObKVCacheStore *store, ObKVCacheInstMap *inst_map)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The ObKVCacheMap has been inited, ", K(ret));
  } else if (0 >= bucket_num || NULL == store || NULL == inst_map) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid arguments, ", K(bucket_num), K(store), KP(inst_map), K(ret));
  } else if (OB_FAIL(node_allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                          ObMemAttr("CACHE_MAP_NODE"),
                                          1))) {
  } else {
    shard_num_ = choose_shard_num();
    void *mem = ob_malloc(sizeof(Shard) * shard_num_, ObMemAttr("CACHE_MAP_SHARD", ObCtxIds::DEFAULT_CTX_ID));
    if (OB_ISNULL(mem)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(WARN, "failed to allocate kvcache shards", K(ret), K_(shard_num));
    } else {
      shards_ = static_cast<Shard *>(mem);
      for (int64_t i = 0; i < shard_num_; ++i) {
        new (&shards_[i]) Shard();
      }
      bucket_num_ = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < shard_num_; ++i) {
        void *buckets = ob_malloc(sizeof(Node *) * INIT_BUCKETS_PER_SHARD,
                                  ObMemAttr("CACHE_MAP_BKT", ObCtxIds::DEFAULT_CTX_ID));
        if (OB_ISNULL(buckets)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN, "failed to allocate shard buckets", K(ret), K(i));
        } else {
          memset(buckets, 0, sizeof(Node *) * INIT_BUCKETS_PER_SHARD);
          shards_[i].buckets_ = static_cast<Node **>(buckets);
          shards_[i].bucket_num_ = INIT_BUCKETS_PER_SHARD;
          bucket_num_ += INIT_BUCKETS_PER_SHARD;
        }
      }
    }
    if (OB_SUCC(ret)) {
      store_ = store;
      inst_map_ = inst_map;
      is_inited_ = true;
      COMMON_LOG(INFO, "kvcache map uses sharded growable buckets", K_(shard_num),
          K_(bucket_num), "requested_bucket_num", bucket_num,
          "init_buckets_per_shard", INIT_BUCKETS_PER_SHARD);
    }
  }
  if (!is_inited_) {
    destroy();
  }
  return ret;
}

void ObKVCacheMap::destroy()
{
  if (NULL != shards_) {
    for (int64_t i = 0; i < shard_num_; ++i) {
      Shard &shard = shards_[i];
      if (NULL != shard.buckets_) {
        for (int64_t b = 0; b < shard.bucket_num_; ++b) {
          Node *iter = shard.buckets_[b];
          while (NULL != iter) {
            Node *next = iter->next_;
            iter->retire();
            iter = next;
          }
          shard.buckets_[b] = NULL;
        }
        ob_free(shard.buckets_);
        shard.buckets_ = NULL;
      }
      shard.~Shard();
    }
    ob_free(shards_);
    shards_ = NULL;
  }
  node_allocator_.destroy();
  bucket_num_ = 0;
  shard_num_ = 0;
  resize_count_ = 0;
  resize_us_ = 0;
  store_ = NULL;
  inst_map_ = NULL;
  is_inited_ = false;
}

int64_t ObKVCacheMap::get_managed_used() const
{
  int64_t used = node_allocator_.allocated();
  used += shard_num_ * static_cast<int64_t>(sizeof(Shard));
  if (NULL != shards_) {
    for (int64_t i = 0; i < shard_num_; ++i) {
      used += ATOMIC_LOAD(&shards_[i].bucket_num_) * static_cast<int64_t>(sizeof(Node *));
    }
  }
  return used;
}

void ObKVCacheMap::free_nodes(Node *head)
{
  while (NULL != head) {
    Node *next = head->next_;
    head->retire();
    head = next;
  }
}

void ObKVCacheMap::unlink_node(Node *&prev,
                               Node *&iter,
                               Node *&bucket_ptr,
                               int64_t &shard_size,
                               Node *&retired)
{
  if (NULL != iter) {
    (void)ATOMIC_SAF(&iter->inst_->status_.store_size_, iter->kvpair_size_);
    Node *erased = iter;
    Node *next = iter->next_;
    if (NULL == prev) {
      bucket_ptr = next;
    } else {
      prev->next_ = next;
    }
    iter = next;
    erased->next_ = retired;
    retired = erased;
    if (shard_size > 0) {
      --shard_size;
    } else {
      int ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(ERROR, "kvcache shard size underflow", K(ret));
    }
  }
}

int ObKVCacheMap::expand_shard(Shard &shard)
{
  int ret = OB_SUCCESS;
  const int64_t old_num = shard.bucket_num_;
  int64_t new_num = INIT_BUCKETS_PER_SHARD;
  if (old_num > 0 && old_num > (INT64_MAX / 2)) {
    ret = OB_SIZE_OVERFLOW;
    COMMON_LOG(WARN, "kvcache shard bucket count overflow", K(ret), K(old_num));
  } else if (old_num > 0) {
    new_num = old_num * 2;
  }
  if (OB_FAIL(ret)) {
  } else if (new_num <= 0 || 0 != (new_num & (new_num - 1))
      || new_num > INT64_MAX / static_cast<int64_t>(sizeof(Node *))) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "invalid kvcache shard bucket count", K(ret), K(old_num), K(new_num));
  } else {
    void *mem = ob_malloc(sizeof(Node *) * new_num, ObMemAttr("CACHE_MAP_BKT", ObCtxIds::DEFAULT_CTX_ID));
    if (OB_ISNULL(mem)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      COMMON_LOG(WARN, "failed to expand kvcache shard", K(ret), K(old_num), K(new_num));
    } else {
      Node **new_buckets = static_cast<Node **>(mem);
      memset(new_buckets, 0, sizeof(Node *) * new_num);
      const int64_t start_us = ObClockGenerator::getClock();
      for (int64_t i = 0; NULL != shard.buckets_ && i < old_num; ++i) {
        Node *iter = shard.buckets_[i];
        while (NULL != iter) {
          Node *next = iter->next_;
          const int64_t pos = bucket_pos(raw_hash_of(*iter), new_num);
          iter->next_ = new_buckets[pos];
          new_buckets[pos] = iter;
          iter = next;
        }
      }
      if (NULL != shard.buckets_) {
        ob_free(shard.buckets_);
      }
      shard.buckets_ = new_buckets;
      // Publish the new count after the pointer is stored. Lock-free readers of
      // the count never dereference the array; chain walks take the shard lock.
      ATOMIC_STORE(&shard.bucket_num_, new_num);
      (void)ATOMIC_AAF(&bucket_num_, new_num - old_num);
      const int64_t cost_us = ObClockGenerator::getClock() - start_us;
      const int64_t total = ATOMIC_AAF(&resize_count_, 1);
      (void)ATOMIC_AAF(&resize_us_, cost_us);
      const int64_t shard_id = &shard - shards_;
      COMMON_LOG(INFO, "kvcache map shard resized", K(shard_id), K(old_num), K(new_num), K(cost_us),
          "resize_count", total, "resize_us", ATOMIC_LOAD(&resize_us_), "size", shard.size_);
    }
  }
  return ret;
}

int ObKVCacheMap::locate_bucket(const int64_t flat_pos, int64_t &shard_idx, int64_t &local_pos) const
{
  int ret = OB_SUCCESS;
  int64_t cursor = 0;
  shard_idx = -1;
  local_pos = -1;
  if (OB_UNLIKELY(flat_pos < 0 || NULL == shards_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; i < shard_num_; ++i) {
      const int64_t bucket_num = ATOMIC_LOAD(&shards_[i].bucket_num_);
      if (flat_pos < cursor + bucket_num) {
        shard_idx = i;
        local_pos = flat_pos - cursor;
        break;
      }
      cursor += bucket_num;
    }
    if (shard_idx < 0) {
      // A concurrent expand can publish the global count before this snapshot
      // of per-shard counts includes the new buckets. The caller skips the slot.
      ret = OB_ENTRY_NOT_EXIST;
    }
  }
  return ret;
}

int ObKVCacheMap::put(
  ObKVCacheInst &inst,
  const ObIKVCacheKey &key,
  const ObKVCachePair *kvpair,
  HazptrHolder &hazptr_holder,
  bool overwrite)
{
  int ret = OB_SUCCESS;
  uint64_t raw_hash = 0;
  Node *retired = NULL;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(NULL == kvpair) || OB_UNLIKELY(!hazptr_holder.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", KP(kvpair), K(hazptr_holder), K(ret));
  } else if (OB_FAIL(key.hash(raw_hash))) {
  } else {
    Shard &shard = shards_[shard_index_of(raw_hash)];
    {
      obsys::ObWLockGuard guard(shard.lock_);
      HazptrHolder holder;
      if (!guard.acquired()) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "failed to lock kvcache shard", K(ret));
      } else if (shard.size_ >= shard.bucket_num_ && OB_FAIL(expand_shard(shard))) {
      } else if (OB_UNLIKELY(shard.bucket_num_ <= 0) || OB_ISNULL(shard.buckets_)) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "kvcache shard has no buckets", K(ret), K(shard.bucket_num_));
      } else {
        const uint64_t hash_code = raw_hash + static_cast<uint64_t>(inst.cache_id_);
        const int64_t pos = bucket_pos(raw_hash, shard.bucket_num_);
        Node *&bucket_ptr = shard.buckets_[pos];
        Node *iter = bucket_ptr;
        Node *prev = NULL;
        bool protect_success = false;
        while (NULL != iter && OB_SUCC(ret)) {
          bool is_equal = false;
          if (OB_FAIL(holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
          } else if (!protect_success) {
            (void)ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
            unlink_node(prev, iter, bucket_ptr, shard.size_, retired);
          } else {
            if (OB_NOT_NULL(iter->inst_->node_allocator_) && iter->inst_->node_allocator_->is_fragment(iter)) {
              internal_map_replace(prev, iter, bucket_ptr, retired);
            }
            if (hash_code == iter->hash_code_ && OB_FAIL(key.equal(*iter->key_, is_equal))) {
              holder.release();
            } else if (is_equal) {
              if (!overwrite) {
                ret = OB_ENTRY_EXIST;
                holder.release();
              }
              // Overwrite keeps the protection until the old memblock count is dropped.
              break;
            } else {
              holder.release();
              prev = iter;
              iter = iter->next_;
            }
          }
        }
        if (OB_SUCC(ret)) {
          void *buf = NULL;
          if (OB_ISNULL(inst.node_allocator_)) {
            ret = OB_ERR_UNEXPECTED;
            COMMON_LOG(ERROR, "Unexpected null node allocator", K(ret), K(inst.cache_id_));
          } else if (NULL == (buf = inst.node_allocator_->alloc(sizeof(Node)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            COMMON_LOG(ERROR, "Fail to allocate memory, ", K(ret), "size", sizeof(Node));
          } else {
            Node *new_node = new (buf) Node();
            new_node->inst_ = &inst;
            new_node->hash_code_ = hash_code;
            new_node->mb_handle_ = hazptr_holder.get_mb_handle();
            new_node->key_ = kvpair->key_;
            new_node->value_ = kvpair->value_;
            new_node->get_cnt_ = 1;
            new_node->seq_num_ = new_node->mb_handle_->get_seq_num_for_node();
            new_node->kvpair_size_ = kvpair->size_;
            ++shard.size_;
            if (NULL == iter) {
              (void)ATOMIC_AAF(&inst.status_.kv_cnt_, 1);
            }
            (void)ATOMIC_AAF(&new_node->mb_handle_->kv_cnt_, 1);
            (void)ATOMIC_AAF(&new_node->mb_handle_->get_cnt_, 1);
            ++new_node->mb_handle_->recent_get_cnt_;
            inst.status_.total_put_cnt_.inc();
            (void)ATOMIC_AAF(&inst.status_.store_size_, kvpair->size_);
            new_node->next_ = bucket_ptr;
            bucket_ptr = new_node;
            if (NULL != iter) {
              // The old node is still protected by holder. Drop its memblock count
              // before wash can reuse the handle, then unlink it.
              (void)ATOMIC_SAF(&iter->mb_handle_->kv_cnt_, 1);
              (void)ATOMIC_SAF(&iter->mb_handle_->get_cnt_, iter->get_cnt_);
              holder.release();
              unlink_node(prev, iter, new_node->next_, shard.size_, retired);
            }
          }
        }
      }
    }
    free_nodes(retired);
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
  uint64_t raw_hash = 0;
  pvalue = NULL;
  bool need_move = false;
  int64_t iter_get_cnt = 0;
  int64_t mb_get_cnt = 0;
  int64_t mb_handle_kv_cnt = 0;
  ObKVCachePolicy mb_policy = LFU;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_FAIL(key.hash(raw_hash))) {
  } else {
    Shard &shard = shards_[shard_index_of(raw_hash)];
    const uint64_t hash_code = raw_hash + static_cast<uint64_t>(cache_id);
    {
      obsys::ObRLockGuard guard(shard.lock_);
      if (!guard.acquired()) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "failed to lock kvcache shard", K(ret));
      } else if (shard.bucket_num_ <= 0 || OB_ISNULL(shard.buckets_)) {
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        const int64_t pos = bucket_pos(raw_hash, shard.bucket_num_);
        Node *iter = shard.buckets_[pos];
        bool is_equal = false;
        while (NULL != iter && OB_SUCC(ret)) {
          __builtin_prefetch(iter, 0);
          if (hash_code == iter->hash_code_) {
            bool protect_success = false;
            if (OB_FAIL(hazptr_holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
            } else if (protect_success) {
              if (OB_FAIL(key.equal(*iter->key_, is_equal))) {
                hazptr_holder.release();
              } else if (is_equal) {
                pvalue = iter->value_;
                mb_get_cnt = ATOMIC_AAF(&iter->mb_handle_->get_cnt_, 1);
                mb_handle_kv_cnt = ATOMIC_LOAD(&iter->mb_handle_->kv_cnt_);
                (void)ATOMIC_AAF(&iter->mb_handle_->recent_get_cnt_, 1);
                iter_get_cnt = ATOMIC_AAF(&iter->get_cnt_, 1);
                iter->inst_->status_.total_hit_cnt_.inc();
                mb_policy = iter->mb_handle_->policy_;
                need_move = (LRU == mb_policy && mb_handle_kv_cnt >= 0
                    && need_modify_cache(iter_get_cnt, mb_get_cnt, mb_handle_kv_cnt));
                break;
              } else {
                hazptr_holder.release();
              }
            }
          }
          iter = iter->next_;
        }
        if (OB_SUCC(ret) && NULL == pvalue) {
          ret = OB_ENTRY_NOT_EXIST;
        }
      }
    }
    if (OB_ENTRY_NOT_EXIST == ret && OB_NOT_NULL(inst_map_)) {
      ObKVCacheInstKey inst_key(cache_id);
      ObKVCacheInstHandle inst_handle;
      if (OB_SUCCESS == inst_map_->get_cache_inst(inst_key, inst_handle)
          && OB_NOT_NULL(inst_handle.get_inst())) {
        inst_handle.get_inst()->status_.total_miss_cnt_.inc();
      }
    }
    if (OB_SUCC(ret) && OB_UNLIKELY(mb_handle_kv_cnt < 0)) {
      COMMON_LOG(ERROR, "unexpected kv cnt", K(mb_handle_kv_cnt));
    }
    // Re-find by key under the write lock. The node seen above must not be
    // used after the read lock is released: an erase can free it immediately.
    if (OB_SUCC(ret) && need_move) {
      try_promote(shard, raw_hash, hash_code, key);
    }
  }
  return ret;
}

void ObKVCacheMap::try_promote(Shard &shard,
                               const uint64_t raw_hash,
                               const uint64_t hash_code,
                               const ObIKVCacheKey &key)
{
  Node *retired = NULL;
  {
    obsys::ObWLockGuard guard(shard.lock_);
    if (!guard.acquired()) {
      int ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "failed to lock kvcache shard for promotion", K(ret));
    } else if (shard.bucket_num_ > 0 && OB_NOT_NULL(shard.buckets_)) {
      const int64_t pos = bucket_pos(raw_hash, shard.bucket_num_);
      Node *&bucket_ptr = shard.buckets_[pos];
      Node *prev = NULL;
      Node *iter = bucket_ptr;
      HazptrHolder holder;
      bool found = false;
      int ret = OB_SUCCESS;
      while (NULL != iter && OB_SUCC(ret) && !found) {
        if (hash_code != iter->hash_code_) {
          prev = iter;
          iter = iter->next_;
        } else {
          bool protect_success = false;
          bool is_equal = false;
          if (OB_FAIL(holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
          } else if (!protect_success) {
            (void)ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
            unlink_node(prev, iter, bucket_ptr, shard.size_, retired);
          } else if (OB_FAIL(key.equal(*iter->key_, is_equal))) {
            holder.release();
          } else if (!is_equal) {
            holder.release();
            prev = iter;
            iter = iter->next_;
          } else {
            // Keep the memblock protected while internal_data_move copies the key.
            found = true;
          }
        }
      }
      if (found && NULL != iter && LRU == iter->mb_handle_->policy_
          && need_modify_cache(iter->get_cnt_, ATOMIC_LOAD(&iter->mb_handle_->get_cnt_),
              ATOMIC_LOAD(&iter->mb_handle_->kv_cnt_))) {
        int tmp_ret = internal_data_move(prev, iter, bucket_ptr, retired);
        if (OB_SUCCESS != tmp_ret) {
          COMMON_LOG(WARN, "failed to move hot kvcache pair", K(tmp_ret));
        }
      }
      holder.release();
    }
  }
  free_nodes(retired);
}

int ObKVCacheMap::erase(const int64_t cache_id, const ObIKVCacheKey &key)
{
  int ret = OB_SUCCESS;
  uint64_t raw_hash = 0;
  Node *retired = NULL;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_FAIL(key.hash(raw_hash))) {
  } else {
    Shard &shard = shards_[shard_index_of(raw_hash)];
    {
      obsys::ObWLockGuard guard(shard.lock_);
      if (!guard.acquired()) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "failed to lock kvcache shard", K(ret));
      } else if (shard.bucket_num_ <= 0 || OB_ISNULL(shard.buckets_)) {
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        const uint64_t hash_code = raw_hash + static_cast<uint64_t>(cache_id);
        const int64_t pos = bucket_pos(raw_hash, shard.bucket_num_);
        Node *&bucket_ptr = shard.buckets_[pos];
        Node *iter = bucket_ptr;
        Node *prev = NULL;
        bool found = false;
        HazptrHolder holder;
        bool protect_success = false;
        while (NULL != iter && OB_SUCC(ret) && !found) {
          if (OB_FAIL(holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
          } else if (!protect_success) {
            (void)ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
            unlink_node(prev, iter, bucket_ptr, shard.size_, retired);
          } else {
            bool is_equal = false;
            if (OB_NOT_NULL(iter->inst_->node_allocator_) && iter->inst_->node_allocator_->is_fragment(iter)) {
              internal_map_replace(prev, iter, bucket_ptr, retired);
            }
            if (hash_code != iter->hash_code_) {
              holder.release();
              prev = iter;
              iter = iter->next_;
            } else if (OB_FAIL(key.equal(*iter->key_, is_equal))) {
              holder.release();
            } else if (!is_equal) {
              holder.release();
              prev = iter;
              iter = iter->next_;
            } else {
              (void)ATOMIC_SAF(&iter->mb_handle_->kv_cnt_, 1);
              (void)ATOMIC_SAF(&iter->mb_handle_->get_cnt_, iter->get_cnt_);
              (void)ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
              holder.release();
              unlink_node(prev, iter, bucket_ptr, shard.size_, retired);
              found = true;
            }
          }
        }
        if (OB_SUCC(ret) && !found) {
          ret = OB_ENTRY_NOT_EXIST;
        }
      }
    }
    free_nodes(retired);
  }
  return ret;
}

int ObKVCacheMap::erase_all()
{
  return erase_nodes(-1, true);
}

int ObKVCacheMap::erase_all(const int64_t cache_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(cache_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(cache_id), K(ret));
  } else {
    ret = erase_nodes(cache_id, false);
  }
  return ret;
}

int ObKVCacheMap::erase_nodes(const int64_t cache_id, const bool all_caches)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else {
    for (int64_t i = 0; i < shard_num_ && OB_SUCC(ret); ++i) {
      Shard &shard = shards_[i];
      Node *retired = NULL;
      {
        obsys::ObWLockGuard guard(shard.lock_);
        if (!guard.acquired()) {
          ret = OB_ERR_UNEXPECTED;
          COMMON_LOG(WARN, "failed to lock kvcache shard", K(ret), K(i));
        } else if (NULL != shard.buckets_) {
          HazptrHolder holder;
          for (int64_t b = 0; b < shard.bucket_num_; ++b) {
            Node *prev = NULL;
            Node *iter = shard.buckets_[b];
            while (NULL != iter && OB_SUCC(ret)) {
              if (!all_caches && iter->inst_->cache_id_ != cache_id) {
                prev = iter;
                iter = iter->next_;
                continue;
              }
              bool protect_success = false;
              if (OB_FAIL(holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
              } else if (protect_success) {
                (void)ATOMIC_SAF(&iter->mb_handle_->kv_cnt_, 1);
                (void)ATOMIC_SAF(&iter->mb_handle_->get_cnt_, iter->get_cnt_);
                holder.release();
              }
              if (OB_SUCC(ret)) {
                (void)ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
                unlink_node(prev, iter, shard.buckets_[b], shard.size_, retired);
              }
            }
          }
        }
      }
      free_nodes(retired);
    }
  }
  return ret;
}

void ObKVCacheMap::unpack_scan_pos(const int64_t start_pos, int64_t &shard_idx, int64_t &local_pos) const
{
  const int64_t mask = (1LL << SCAN_SHARD_SHIFT) - 1;
  shard_idx = start_pos >> SCAN_SHARD_SHIFT;
  local_pos = start_pos & mask;
  if (shard_idx < 0 || shard_idx >= shard_num_ || local_pos < 0) {
    shard_idx = 0;
    local_pos = 0;
  }
}

int64_t ObKVCacheMap::pack_scan_pos(const int64_t shard_idx, const int64_t local_pos)
{
  return (shard_idx << SCAN_SHARD_SHIFT) | local_pos;
}

void ObKVCacheMap::clean_one_bucket(Shard &shard,
                                    const int64_t local_pos,
                                    Node *&retired,
                                    int64_t &clean_node_count)
{
  Node *prev = NULL;
  Node *iter = shard.buckets_[local_pos];
  HazptrHolder holder;
  const int64_t start_us = ObClockGenerator::getClock();
  while (NULL != iter) {
    bool protect_success = false;
    int ret = OB_SUCCESS;
    if (OB_FAIL(holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
      break;
    } else if (protect_success) {
      holder.release();
      prev = iter;
      iter = iter->next_;
    } else {
      (void)ATOMIC_SAF(&iter->inst_->status_.kv_cnt_, 1);
      unlink_node(prev, iter, shard.buckets_[local_pos], shard.size_, retired);
      ++clean_node_count;
    }
    if (ObClockGenerator::getClock() - start_us >= SCAN_CHAIN_LIMIT_US) {
      break;
    }
  }
}

void ObKVCacheMap::replace_one_bucket(Shard &shard,
                                      const int64_t local_pos,
                                      Node *&retired,
                                      int64_t &replace_node_count)
{
  Node *prev = NULL;
  Node *iter = shard.buckets_[local_pos];
  const int64_t start_us = ObClockGenerator::getClock();
  int64_t node_count = 0;
  while (NULL != iter) {
    if (OB_NOT_NULL(iter->inst_->node_allocator_) && iter->inst_->node_allocator_->is_fragment(iter)) {
      internal_map_replace(prev, iter, shard.buckets_[local_pos], retired);
      ++node_count;
    }
    prev = iter;
    iter = iter->next_;
    if (ObClockGenerator::getClock() - start_us >= SCAN_CHAIN_LIMIT_US) {
      COMMON_LOG(INFO, "replace map node cost too much time", K(node_count), K(local_pos));
      break;
    }
  }
  replace_node_count += node_count;
}

int ObKVCacheMap::scan_buckets(int64_t &start_pos,
                               const int64_t limit,
                               int64_t &touched,
                               void (ObKVCacheMap::*fn)(Shard &, const int64_t, Node *&, int64_t &))
{
  int ret = OB_SUCCESS;
  touched = 0;
  int64_t shard_idx = 0;
  int64_t local_pos = 0;
  int64_t scanned = 0;
  bool wrapped = false;
  unpack_scan_pos(start_pos, shard_idx, local_pos);
  while (scanned < limit && !wrapped && OB_SUCC(ret)) {
    if (shard_idx < 0 || shard_idx >= shard_num_) {
      wrapped = true;
      break;
    }
    Shard &shard = shards_[shard_idx];
    Node *retired = NULL;
    bool shard_done = false;
    {
      obsys::ObWLockGuard guard(shard.lock_);
      if (!guard.acquired()) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "failed to lock kvcache shard", K(ret), K(shard_idx));
      } else if (NULL == shard.buckets_ || local_pos >= shard.bucket_num_) {
        shard_done = true;
      } else {
        const int64_t lock_start_us = ObClockGenerator::getClock();
        while (local_pos < shard.bucket_num_ && scanned < limit) {
          if (NULL != shard.buckets_[local_pos]) {
            (this->*fn)(shard, local_pos, retired, touched);
          }
          ++local_pos;
          ++scanned;
          if (ObClockGenerator::getClock() - lock_start_us >= SCAN_LOCK_HOLD_US) {
            break;
          }
        }
        shard_done = local_pos >= shard.bucket_num_;
      }
    }
    free_nodes(retired);
    if (shard_done) {
      local_pos = 0;
      ++shard_idx;
      if (shard_idx >= shard_num_) {
        shard_idx = 0;
        wrapped = true;
      }
    }
  }
  start_pos = wrapped ? 0 : pack_scan_pos(shard_idx, local_pos);
  return ret;
}

int ObKVCacheMap::clean_garbage_node(int64_t &start_pos, const int64_t clean_num)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(start_pos < 0) || OB_UNLIKELY(clean_num <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(start_pos), K(clean_num), K(ret));
  } else {
    ObTimeGuard tg("clean_garbage_node", 100000);
    int64_t clean_node_count = 0;
    if (OB_FAIL(scan_buckets(start_pos, clean_num, clean_node_count, &ObKVCacheMap::clean_one_bucket))) {
    }
    if (clean_node_count > 0) {
      COMMON_LOG(INFO, "Cache wash clean map node details", K(ret), K(clean_node_count),
          "clean_time", tg.get_diff(), K(clean_num), K(start_pos));
    }
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
  } else if (OB_UNLIKELY(start_pos < 0) || OB_UNLIKELY(replace_num <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(start_pos), K(replace_num), K(ret));
  } else {
    ObTimeGuard tg("replace_fragement_node", 100000);
    if (OB_FAIL(scan_buckets(start_pos, replace_num, replace_node_count, &ObKVCacheMap::replace_one_bucket))) {
    }
    COMMON_LOG(INFO, "Cache replace map node details", K(ret), K(replace_node_count),
        "replace_time", tg.get_diff(), K(replace_num), K(start_pos));
  }
  return ret;
}

void ObKVCacheMap::print_hazard_version_info()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap is not inited", K(ret));
  } else {
    COMMON_LOG(INFO, "kvcache map info", K_(shard_num), "bucket_num", get_bucket_num(),
        "resize_count", ATOMIC_LOAD(&resize_count_), "resize_us", ATOMIC_LOAD(&resize_us_),
        "map_mem", get_managed_used());
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
  } else if (OB_UNLIKELY(cache_id < 0) || OB_UNLIKELY(pos < 0) || OB_UNLIKELY(pos >= get_bucket_num())) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(cache_id), K(pos), "bucket_num", get_bucket_num(), K(ret));
  } else {
    int64_t shard_idx = 0;
    int64_t local_pos = 0;
    int locate_ret = locate_bucket(pos, shard_idx, local_pos);
    if (OB_SUCCESS != locate_ret) {
      // Expand moved the flat index. Skip this slot instead of failing iteration.
    } else {
      Shard &shard = shards_[shard_idx];
      obsys::ObRLockGuard guard(shard.lock_);
      if (!guard.acquired()) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "failed to lock kvcache shard", K(ret), K(shard_idx));
      } else if (local_pos >= 0 && local_pos < shard.bucket_num_ && OB_NOT_NULL(shard.buckets_)) {
        Node *iter = shard.buckets_[local_pos];
        HazptrHolder holder;
        bool protect_success = false;
        while (NULL != iter && OB_SUCC(ret)) {
          if (cache_id == iter->inst_->cache_id_) {
            if (OB_FAIL(holder.protect(protect_success, iter->mb_handle_, iter->seq_num_))) {
            } else if (protect_success) {
              if (OB_FAIL(list.push_back(*iter))) {
                holder.release();
              } else {
                holder.release();
              }
            }
          }
          iter = iter->next_;
        }
      }
    }
  }
  return ret;
}

void ObKVCacheMap::internal_map_replace(Node *&prev,
                                        Node *&iter,
                                        Node *&bucket_ptr,
                                        Node *&retired)
{
  if (NULL != iter) {
    void *buf = NULL;
    if (OB_NOT_NULL(iter->inst_->node_allocator_)
        && NULL != (buf = iter->inst_->node_allocator_->alloc(sizeof(Node)))) {
      Node *new_node = new (buf) Node();
      *new_node = *iter;
      new_node->set_next(NULL);
      WEAK_BARRIER();
      if (NULL == prev) {
        bucket_ptr = new_node;
      } else {
        prev->next_ = new_node;
      }
      Node *erased = iter;
      iter = new_node;
      erased->next_ = retired;
      retired = erased;
    }
  }
}

int ObKVCacheMap::internal_data_move(Node *&prev,
                                     Node *&old_iter,
                                     Node *&bucket_ptr,
                                     Node *&retired)
{
  int ret = OB_SUCCESS;
  void *buf = NULL;
  ObKVCachePair *new_kvpair = NULL;
  HazptrHolder hazptr_holder;
  // Caller holds a hazard protection on old_iter->mb_handle_ for the duration of
  // this call. store() copies the key and value before returning.
  if (OB_ISNULL(old_iter) || OB_ISNULL(old_iter->inst_) || OB_ISNULL(old_iter->inst_->node_allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Unexpected null node allocator", K(ret), KPC(old_iter));
  } else if (NULL == (buf = old_iter->inst_->node_allocator_->alloc(sizeof(Node)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(WARN, "Fail to allocate memory for Node, ", K(ret), "size:", sizeof(Node));
  } else if (OB_FAIL(store_->store(*old_iter->key_, *old_iter->value_, new_kvpair, hazptr_holder, LFU))) {
    old_iter->inst_->node_allocator_->free(buf);
    COMMON_LOG(WARN, "Fail to move kvpair ", K(ret));
  } else {
    ObKVMemBlockHandle *new_mb_handle = hazptr_holder.get_mb_handle();
    Node *new_node = new (buf) Node();
    new_node->inst_ = old_iter->inst_;
    new_node->hash_code_ = old_iter->hash_code_;
    new_node->mb_handle_ = new_mb_handle;
    new_node->key_ = new_kvpair->key_;
    new_node->value_ = new_kvpair->value_;
    new_node->get_cnt_ = old_iter->get_cnt_;
    new_node->next_ = old_iter->next_;
    new_node->seq_num_ = new_node->mb_handle_->get_seq_num_for_node();
    new_node->kvpair_size_ = new_kvpair->size_;
    (void)ATOMIC_SAF(&old_iter->mb_handle_->kv_cnt_, 1);
    (void)ATOMIC_SAF(&old_iter->mb_handle_->get_cnt_, old_iter->get_cnt_);
    (void)ATOMIC_AAF(&new_mb_handle->kv_cnt_, 1);
    (void)ATOMIC_AAF(&new_mb_handle->get_cnt_, old_iter->get_cnt_);
    ++new_mb_handle->recent_get_cnt_;
    hazptr_holder.release();
    WEAK_BARRIER();
    if (NULL == prev) {
      bucket_ptr = new_node;
    } else {
      prev->next_ = new_node;
    }
    old_iter->next_ = retired;
    retired = old_iter;
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
