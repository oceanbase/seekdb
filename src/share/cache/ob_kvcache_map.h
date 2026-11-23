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

#ifndef OCEANBASE_CACHE_OB_KVCACHE_MAP_H_
#define OCEANBASE_CACHE_OB_KVCACHE_MAP_H_

#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/ob_lf_fifo_allocator.h"
#include "share/cache/ob_kvcache_struct.h"
#include "share/cache/ob_kvcache_store.h"
#include "share/cache/ob_kvcache_hazard_version.h"

namespace oceanbase
{
namespace common
{
class ObKVCacheIterator;
class ObKVCacheInstMap;
class HazptrHolder;
class ObKVCacheMap
{
  static const int64_t DEFAULT_LFU_THRESHOLD_BASE = 2;
  // Shard count follows other concurrent maps: next power of two of cpu_count * 4.
  // Each shard is internally exclusive under its rwlock, so its bucket array can
  // double when the chain load reaches one. High hash bits select the shard and
  // low bits select the bucket, so the two levels do not share the same bits.
  // Resize follows Redis dict and Linux rhashtable: the new array is allocated
  // and touched outside the write lock, then each write moves a bounded number
  // of nodes. Lookup checks the new table and any old bucket not yet migrated.
  static constexpr int64_t SHARD_NUM_MIN = 16;
  static constexpr int64_t SHARD_NUM_MAX = 256;
  static constexpr int64_t INIT_BUCKETS_PER_SHARD = 8;
  static constexpr int64_t REHASH_BUDGET_US = 100;
  static constexpr int64_t REHASH_WASH_BUDGET_US = 500;
  static constexpr int64_t REHASH_MAX_NODES = 2048;
  // Wash/replace cursors are not flat bucket indexes. A shard expand changes
  // every shard's bucket count, so a flat position would skip or revisit slots.
  static constexpr int64_t SCAN_SHARD_SHIFT = 32;
  static constexpr int64_t SCAN_LOCK_HOLD_US = 1000;
  static constexpr int64_t SCAN_CHAIN_LIMIT_US = 1000 * 1000;
public:
  ObKVCacheMap();
  virtual ~ObKVCacheMap();
  int init(const int64_t bucket_num, ObKVCacheStore *store, ObKVCacheInstMap *inst_map);
  void destroy();
  int erase_all();
  int erase_all(const int64_t cache_id);
  int clean_garbage_node(int64_t &start_pos, const int64_t clean_num);
  int replace_fragment_node(int64_t &start_pos, int64_t &replace_node_count, const int64_t replace_num);
  int put(
    ObKVCacheInst &inst,
    const ObIKVCacheKey &key,
    const ObKVCachePair *kvpair,
    HazptrHolder &hazptr_holder,
    bool overwrite = true);
  int get(
    const int64_t cache_id,
    const ObIKVCacheKey &key,
    const ObIKVCacheValue *&pvalue,
    HazptrHolder &hazptr_holder);
  int erase(const int64_t cache_id, const ObIKVCacheKey &key);
  // Current bucket count. It grows as shards expand and is not the size requested at init.
  OB_INLINE int64_t get_bucket_num() const { return ATOMIC_LOAD(&bucket_num_); }
  OB_INLINE int64_t get_shard_num() const { return shard_num_; }
  OB_INLINE ObLfFIFOAllocator *get_node_allocator() { return &node_allocator_; }
  int64_t get_managed_used() const;
  void print_hazard_version_info();
private:
  friend class ObKVCacheIterator;
  struct Node : public ObKVCacheHazardNode
  {
    ObKVCacheInst *inst_;
    uint64_t hash_code_;
    int32_t seq_num_;
    int32_t kvpair_size_;
    ObKVMemBlockHandle *mb_handle_;
    const ObIKVCacheKey *key_;
    const ObIKVCacheValue *value_;
    Node *next_;
    int64_t get_cnt_;
    Node() :
        inst_(NULL),
        hash_code_(0),
        seq_num_(0),
        kvpair_size_(0),
        mb_handle_(NULL),
        key_(NULL),
        value_(NULL),
        next_(NULL),
        get_cnt_(0)
    {}
    virtual ~Node() {};
    virtual void retire() override;  // only free memory of itself
    INHERIT_TO_STRING_KV("Node", ObKVCacheHazardNode, KPC_(inst), K_(hash_code), K_(seq_num), K_(kvpair_size), KP_(mb_handle), KP_(key),
                         KP_(value), KP_(next), K_(get_cnt));
  };
  // Defined in the cpp. Holds the rwlock and the growable bucket array.
  // Every chain walk holds that lock, so nodes are freed by the unlinking thread
  // instead of the node hazard station. Memblock bytes stay hazard-protected
  // because wash does not take the shard lock.
  struct Shard;
private:
  int multi_get(const int64_t cache_id, const int64_t pos, common::ObList<Node, common::ObArenaAllocator> &list);
  void unlink_node(Node *&prev, Node *&iter, Node *&bucket_ptr, int64_t &shard_size, Node *&retired);
  void free_nodes(Node *head);
  void internal_map_replace(Node *&prev, Node *&iter, Node *&bucket_ptr, Node *&retired);
  int internal_data_move(Node *&prev, Node *&iter, Node *&bucket_ptr, Node *&retired);
  int64_t rehash_alloc_count(const Shard &shard) const;
  void install_rehash(Shard &shard, Node **buckets, int64_t bucket_num);
  // Returns the retired bucket array when this call finishes the rehash.
  // budget_us < 0 migrates everything still pending.
  Node **migrate_rehash(Shard &shard, int64_t budget_us);
  void chain_slots(Shard &shard, uint64_t raw_hash, Node **&primary, Node **&secondary) const;
  int locate_bucket(const int64_t flat_pos, int64_t &shard_idx, int64_t &local_pos) const;
  int64_t shard_index_of(const uint64_t raw_hash) const;
  static int64_t bucket_pos(const uint64_t raw_hash, const int64_t bucket_num);
  static uint64_t raw_hash_of(const Node &node);
  static int64_t choose_shard_num();
  void unpack_scan_pos(const int64_t start_pos, int64_t &shard_idx, int64_t &local_pos) const;
  static int64_t pack_scan_pos(const int64_t shard_idx, const int64_t local_pos);
  void clean_one_bucket(Shard &shard, const int64_t local_pos, Node *&retired, int64_t &clean_node_count);
  void replace_one_bucket(Shard &shard, const int64_t local_pos, Node *&retired, int64_t &replace_node_count);
  int scan_buckets(int64_t &start_pos,
                   const int64_t limit,
                   int64_t &touched,
                   void (ObKVCacheMap::*fn)(Shard &, const int64_t, Node *&, int64_t &));
  void try_promote(Shard &shard, const uint64_t raw_hash, const uint64_t hash_code, const ObIKVCacheKey &key);
  int erase_nodes(const int64_t cache_id, const bool all_caches);
  OB_INLINE bool need_modify_cache(const int64_t iter_get_cnt, const int64_t total_get_cnt, const int64_t kv_cnt) const
  {
    bool ret = false;
    int64_t threshold = total_get_cnt / (kv_cnt + 1) + DEFAULT_LFU_THRESHOLD_BASE;
    ret = iter_get_cnt > threshold;
    return ret;
  }
private:
  bool is_inited_;
  ObLfFIFOAllocator node_allocator_;
  int64_t bucket_num_;
  int64_t shard_num_;
  int64_t resize_count_;
  int64_t resize_us_;
  Shard *shards_;
  ObKVCacheStore *store_;
  ObKVCacheInstMap *inst_map_;
};

}//end namespace common
}//end namespace oceanbase

#endif //OCEANBASE_CACHE_OB_KVCACHE_MAP_H_
