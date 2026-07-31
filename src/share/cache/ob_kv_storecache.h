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

#ifndef  OCEANBASE_COMMON_KV_STORE_CACHE_H_
#define  OCEANBASE_COMMON_KV_STORE_CACHE_H_

#include "lib/lock/ob_mutex.h"
#include "lib/task/ob_timer.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/list/ob_list.h"
#include "share/cache/ob_kvcache_struct.h"
#include "share/cache/ob_kvcache_inst_map.h"
#include "share/cache/ob_kvcache_map.h"
#include "share/cache/ob_kvcache_hazard_domain.h"


namespace oceanbase
{
namespace blocksstable
{
class ObMicroBlockBufferHandle;
}
namespace common
{
class ObKVCacheHandle;
class ObKVCacheIterator;

template <class Key, class Value>
class ObIKVCache
{
public:
  virtual int put(const Key &key, const Value &value, bool overwrite = true) = 0;
  virtual int put_and_fetch(const Key &key, const Value &value, const Value *&pvalue,
      ObKVCacheHandle &handle, bool overwrite = true) = 0;
  virtual int get(const Key &key, const Value *&pvalue, ObKVCacheHandle &handle) = 0;
  virtual int erase(const Key &key) = 0;
  virtual int alloc(const int64_t key_size, const int64_t value_size,
      ObKVCachePair *&kvpair, ObKVCacheHandle &handle, ObKVCacheInstHandle &inst_handle) = 0;
  virtual int put_kvpair(ObKVCacheInstHandle &inst_handle, ObKVCachePair *kvpair, ObKVCacheHandle &handle, bool overwrite = true);
};

template <class Key, class Value>
class ObKVCache : public ObIKVCache<Key, Value>
{
public:
  ObKVCache();
  virtual ~ObKVCache();
  int init(const char *cache_name, const int64_t mem_limit_pct = 100);
  void destroy();
  int set_mem_limit_pct(const int64_t mem_limit_pct);
  virtual int put(const Key &key, const Value &value, bool overwrite = true);
  virtual int put_and_fetch(
    const Key &key,
    const Value &value,
    const Value *&pvalue,
    ObKVCacheHandle &handle,
    bool overwrite = true);
  virtual int get(const Key &key, const Value *&pvalue, ObKVCacheHandle &handle);
  int get_iterator(ObKVCacheIterator &iter);
  virtual int erase(const Key &key);
  virtual int alloc(
      const int64_t key_size,
      const int64_t value_size,
      ObKVCachePair *&kvpair,
      ObKVCacheHandle &handle,
      ObKVCacheInstHandle &inst_handle) override;
  int64_t size() const;
  int64_t count() const;
  int64_t get_hit_cnt() const;
  int64_t get_miss_cnt() const;
  double get_hit_rate() const;
  int64_t store_size() const;
  int64_t get_cache_id() const { return cache_id_; }
private:
  bool inited_;
  int64_t cache_id_;
};

class ObKVCacheHandle;
struct ObKVCacheRuntimeOptions
{
  static const int64_t DEFAULT_WASH_INTERVAL_US = 200 * 1000;

  explicit ObKVCacheRuntimeOptions(
      const int64_t wash_interval_us = DEFAULT_WASH_INTERVAL_US)
      : wash_interval_us_(wash_interval_us)
  {}

  bool is_valid() const { return wash_interval_us_ > 0; }

  int64_t wash_interval_us_;
};

class ObKVGlobalCache : public lib::ObICacheWasher
{
public:
  static const int64_t DEFAULT_ONCE_BATCH_GET_BUCKET_NUM = 10000;
  static ObKVGlobalCache &get_instance();
  static int64_t default_max_cache_size() { return DEFAULT_MAX_CACHE_SIZE; }
  int init(const int64_t bucket_num = DEFAULT_BUCKET_NUM,
           const int64_t max_cache_size = DEFAULT_MAX_CACHE_SIZE,
           const int64_t block_size = lib::ACHUNK_SIZE,
           const int64_t cache_wash_interval = 0,
           const ObKVCacheRuntimeOptions &runtime_options = ObKVCacheRuntimeOptions());
  void stop();
  void wait();
  void destroy();
  int reload_config(const ObKVCacheRuntimeOptions &runtime_options);
  int get_suitable_bucket_num(
      int64_t &bucket_num,
      const int64_t memory_limit,
      const int64_t reserved_memory);
  int get_cache_inst_info(ObIArray<ObKVCacheInstHandle> &inst_handles);
  int get_memblock_info(ObIArray<ObKVCacheStoreMemblockInfo> &memblock_infos);
  void print_all_cache_info();
  virtual int erase_cache() override;
  int erase_cache(const char *cache_name);

  int get_washable_size(int64_t &washable_size);

  // wash memblock from cache synchronously
  virtual int sync_wash_mbs(const int64_t wash_size,
                            lib::ObICacheWasher::ObCacheMemBlock *&wash_blocks);
  int get_cache_name(const int64_t cache_id, char *cache_name);
  OB_INLINE int64_t get_bucket_num() const { return map_.get_bucket_num(); }
  int64_t get_managed_used() const
  {
    return store_.get_store_size() + map_.get_managed_used();
  }
  HazardDomain& get_hazard_domain() { return hazard_domain_; }
private:
  template<class Key, class Value> friend class ObIKVCache;
  template<class Key, class Value> friend class ObKVCache;
  friend class ObKVCacheHandle;
  friend class HazptrHolder;
  ObKVGlobalCache();
  virtual ~ObKVGlobalCache();
  int register_cache(const char *cache_name, const int64_t mem_limit_pct, int64_t &cache_id);
  void deregister_cache(const int64_t cache_id);
  int set_mem_limit_pct(const int64_t cache_id, const int64_t mem_limit_pct);
  int put(
    const int64_t cache_id,
    const ObIKVCacheKey &key,
    const ObIKVCacheValue &value,
    const ObIKVCacheValue *&pvalue,
    HazptrHolder &hazptr_holder,
    bool overwrite = true);
  int put(
    ObIKVCacheStore &store,
    const int64_t cache_id,
    const ObIKVCacheKey &key,
    const ObIKVCacheValue &value,
    const ObIKVCacheValue *&pvalue,
    HazptrHolder &hazptr_holder,
    bool overwrite = true);
  int alloc(
      const int64_t cache_id,
      const int64_t key_size,
      const int64_t value_size,
      ObKVCachePair *&kvpair,
      HazptrHolder &hazptr_holder,
      ObKVCacheInstHandle &inst_handle);

  int alloc(
      ObIKVCacheStore &store,
      const int64_t cache_id,
      const int64_t key_size,
      const int64_t value_size,
      ObKVCachePair *&kvpair,
      HazptrHolder &hazptr_holder,
      ObKVCacheInstHandle &inst_handle);
  int get(
    const int64_t cache_id,
    const ObIKVCacheKey &key,
    const ObIKVCacheValue *&pvalue,
    HazptrHolder &hazptr_holder);
  int erase(const int64_t cache_id, const ObIKVCacheKey &key);
  void revert(HazptrHolder& mb_handle);
  void wash();
  void replace_map();
  int get_cache_id(const char *cache_name, int64_t &cache_id);
private:
  static const int64_t DEFAULT_BUCKET_NUM = 10000000L;
  static const int64_t DEFAULT_MAX_CACHE_SIZE = 1024LL * 1024LL * 1024LL * 1024LL;  //1T
  static const int64_t MAP_ONCE_CLEAN_RATIO = 50;  // 50 * 0.2 = 10s
  static const int64_t MAP_ONCE_REPLACE_RATIO = 100;  // 100 * 0.2 = 20s
  static const int64_t MAX_MAP_ONCE_CLEAN_NUM = 200000;  // 200K
  static const int64_t EXPAND_MAP_ONCE_CLEAN_RATIO = 10;
  static const int64_t MAX_MAP_ONCE_REPLACE_NUM = 100000;  // 100K
  static const int64_t TIMER_SCHEDULE_INTERVAL_US = 800 * 1000;
  static const int64_t WORKING_SET_LIMIT_PERCENTAGE = 5;
  static const int64_t BASE_SERVER_MEMORY_FACTOR = 1LL << 30; // 1G is the start level
  static constexpr double MAX_RESERVED_MEMORY_RATIO = 0.3;
  static const int64_t MAX_BUCKET_NUM_LEVEL = 10;
  static const int64_t bucket_num_array_[MAX_BUCKET_NUM_LEVEL];
  static const int64_t PRINT_INTERVAL = 30 * 1000L * 1000L;
  static const int64_t MAP_WASH_CLEAN_INTERNAL = 10;
  static const int64_t MAP_REPLACE_ONCE_SKIP_COUNT = 10;
private:
  class KVStoreWashTask: public ObTimerTask
  {
  public:
    KVStoreWashTask()
    {
    }
    virtual ~KVStoreWashTask()
    {
    }
    void runTimerTask()
    {
      ObKVGlobalCache::get_instance().wash();
      HazardDomain::get_instance().wash();
      if (REACH_TIME_INTERVAL(PRINT_INTERVAL)) {
        ObKVGlobalCache::get_instance().print_all_cache_info();
      }
    }
  };
  class KVMapReplaceTask : public ObTimerTask
  {
  public:
    KVMapReplaceTask()
    {
    }
    virtual ~KVMapReplaceTask()
    {
    }
    void runTimerTask()
    {
      ObKVGlobalCache::get_instance().replace_map();
    }
  };
private:
  bool inited_;
  // map
  ObKVCacheMap map_;
  // store
  ObKVCacheStore store_;
  // cache instances
  ObKVCacheInstMap insts_;
  // cache configs
  ObKVCacheConfig configs_[MAX_CACHE_NUM];
  HazardDomain hazard_domain_;
  int64_t cache_num_;
  lib::ObMutex mutex_;
  // timer and task
  int64_t map_clean_pos_;
  int64_t map_once_clean_num_;
  KVStoreWashTask wash_task_;
  int64_t map_replace_pos_;
  int64_t map_once_replace_num_;
  int64_t map_replace_skip_count_;
  KVMapReplaceTask replace_task_;
  ObTimer wash_timer_;
  ObTimer replace_timer_;
  bool stopped_;
  int64_t cache_wash_interval_;
};


class ObKVCacheHandle
{
public:
  ObKVCacheHandle();
  ~ObKVCacheHandle(); // release hazard pointer
  void reset(); // only release protection, hazard pointer is not released
  inline bool is_valid() const { return hazptr_holder_.is_valid(); }
  // simulate move obj, use must pay attention
  void move_from(ObKVCacheHandle &other);
  int assign(const ObKVCacheHandle& other);
  inline ObKVMemBlockHandle* get_mb_handle() const { return hazptr_holder_.get_mb_handle(); }
  inline void set_hazptr_holder(HazptrHolder& hazptr_holder) { this->hazptr_holder_.move_from(hazptr_holder); }
  TO_STRING_KV(K_(hazptr_holder));

private:
  template<class Key, class Value> friend class ObIKVCache;
  template<class Key, class Value> friend class ObKVCache;
  friend class ObKVCacheIterator;
  friend class ObPointerSwizzleNode;

  HazptrHolder hazptr_holder_;
};

class ObKVCacheIterator
{
public:
  ObKVCacheIterator();
  virtual ~ObKVCacheIterator();
  int init(const int64_t cache_id, ObKVCacheMap *map);
  /**
   * get a kvpair from the kvcache, if return OB_SUCCESS, remember to call revert(handle)
   * to revert the handle.
   * @param key: out
   * @param value: out
   * @param handle: out
   * @return OB_SUCCESS or OB_ITER_END or other error code
   */
  template <class Key, class Value>
  int get_next_kvpair(const Key *&key, const Value *&value, ObKVCacheHandle &handle);
private:
  int64_t cache_id_;
  ObKVCacheMap *map_;
  int64_t pos_;
  common::ObArenaAllocator allocator_;
  common::ObList<ObKVCacheMap::Node, common::ObArenaAllocator> handle_list_;
  bool is_inited_;
};

//-------------------------------------------------------Template Methods----------------------------------------------------------

template <class Key, class Value>
int ObIKVCache<Key, Value>::put_kvpair(ObKVCacheInstHandle &inst_handle, ObKVCachePair *kvpair, ObKVCacheHandle &handle, bool overwrite)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == kvpair)
      || OB_UNLIKELY(NULL == kvpair->key_)
      || OB_UNLIKELY(NULL == kvpair->value_)
      || OB_UNLIKELY(!handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", KP(kvpair), K(handle), K(ret));
  } else {
    if (OB_ISNULL(inst_handle.get_inst())) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "The inst is NULL, ", K(ret));
    } else if (OB_FAIL(ObKVGlobalCache::get_instance().map_.put(*inst_handle.get_inst(),
        *kvpair->key_, kvpair, handle.hazptr_holder_, overwrite))) {
      if (OB_ENTRY_EXIST != ret) {
        COMMON_LOG(WARN, "Fail to put kvpair to map, ", K(ret));
      }
    } else {
    }
  }
  return ret;
}


/*
 * ------------------------------------------------------------ObKVCache-----------------------------------------------------------------
 */
template <class Key, class Value>
ObKVCache<Key, Value>::ObKVCache()
    : inited_(false), cache_id_(-1)
{
}

template <class Key, class Value>
ObKVCache<Key, Value>::~ObKVCache()
{
  destroy();
}

template <class Key, class Value>
int ObKVCache<Key, Value>::init(const char *cache_name, const int64_t mem_limit_pct)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The ObKVCache has been inited, ", K(ret));
  } else if (OB_UNLIKELY(NULL == cache_name)
      || OB_UNLIKELY(mem_limit_pct <= 0 || mem_limit_pct > 100)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", KP(cache_name), K(ret));
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().register_cache(cache_name, mem_limit_pct, cache_id_))) {
    COMMON_LOG(WARN, "Fail to register cache, ", K(ret));
  } else {
    COMMON_LOG(INFO, "Succ to register cache", K(cache_name), K_(cache_id));
    inited_ = true;
  }
  return ret;
}

template <class Key, class Value>
void ObKVCache<Key, Value>::destroy()
{
  if (OB_LIKELY(inited_)) {
    ObKVGlobalCache::get_instance().deregister_cache(cache_id_);
    inited_ = false;
  }
}

template <class Key, class Value>
int64_t ObKVCache<Key, Value>::size() const
{
  int64_t size = 0;
  if (OB_LIKELY(inited_)) {
    int ret = OB_SUCCESS;
    ObKVCacheInstKey inst_key(cache_id_);
    ObKVCacheInstHandle inst_handle;
    if (OB_SUCC(ObKVGlobalCache::get_instance().insts_.get_cache_inst(inst_key, inst_handle))) {
      if (NULL != inst_handle.get_inst()) {
        size += inst_handle.get_inst()->status_.store_size_;
      }
    }
  }
  return size;
}

template <class Key, class Value>
int64_t ObKVCache<Key, Value>::count() const
{
  int64_t count = 0;
  if (OB_LIKELY(inited_)) {
    int ret = OB_SUCCESS;
    ObKVCacheInstKey inst_key(cache_id_);
    ObKVCacheInstHandle inst_handle;
    if (OB_SUCC(ObKVGlobalCache::get_instance().insts_.get_cache_inst(inst_key, inst_handle))) {
      if (NULL != inst_handle.get_inst()) {
        count = inst_handle.get_inst()->status_.kv_cnt_;
      }
    }
  }
  return count;
}

template <class Key, class Value>
int64_t ObKVCache<Key, Value>::get_hit_cnt() const
{
  int64_t hit_cnt = 0;
  if (OB_LIKELY(inited_)) {
    int ret = OB_SUCCESS;
    ObKVCacheInstKey inst_key(cache_id_);
    ObKVCacheInstHandle inst_handle;
    if (OB_SUCC(ObKVGlobalCache::get_instance().insts_.get_cache_inst(inst_key, inst_handle))) {
      if (NULL != inst_handle.get_inst()) {
        hit_cnt = inst_handle.get_inst()->status_.total_hit_cnt_.value();
      }
    }
  }
  return hit_cnt;
}

template <class Key, class Value>
int64_t ObKVCache<Key, Value>::get_miss_cnt() const
{
  int64_t miss_cnt = 0;
  if (OB_LIKELY(inited_)) {
    int ret = OB_SUCCESS;
    ObKVCacheInstKey inst_key(cache_id_);
    ObKVCacheInstHandle inst_handle;
    if (OB_SUCC(ObKVGlobalCache::get_instance().insts_.get_cache_inst(inst_key, inst_handle))) {
      if (NULL != inst_handle.get_inst()) {
        miss_cnt = inst_handle.get_inst()->status_.total_miss_cnt_;
      }
    }
  }
  return miss_cnt;
}

template <class Key, class Value>
double ObKVCache<Key, Value>::get_hit_rate() const
{
  return 0.8;
}

template <class Key, class Value>
int ObKVCache<Key, Value>::get_iterator(ObKVCacheIterator &iter)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCache has not been inited, ", K(ret));
  } else if (OB_FAIL(iter.init(cache_id_, &ObKVGlobalCache::get_instance().map_))) {
    COMMON_LOG(WARN, "Fail to init ObKVCacheIterator, ", K(ret));
  }
  return ret;
}

template <class Key, class Value>
int ObKVCache<Key, Value>::put(const Key &key, const Value &value, bool overwrite)
{
  int ret = OB_SUCCESS;
  ObKVCacheHandle handle;
  const ObIKVCacheValue *pvalue = NULL;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCache has not been inited, ", K(ret));
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().put(cache_id_, key, value, pvalue,
      handle.hazptr_holder_, overwrite))) {
    if (OB_ENTRY_EXIST != ret) {
      COMMON_LOG(WARN, "Fail to put kv to ObKVGlobalCache, ", K_(cache_id), K(ret));
    }
  }
  return ret;
}


template <class Key, class Value>
int ObKVCache<Key, Value>::put_and_fetch(
    const Key &key,
    const Value &value,
    const Value *&pvalue,
    ObKVCacheHandle &handle,
    bool overwrite)
{
  int ret = OB_SUCCESS;
  handle.reset();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCache has not been inited, ", K(ret));
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().put(cache_id_, key, value,
      reinterpret_cast<const ObIKVCacheValue *&>(pvalue), handle.hazptr_holder_, overwrite))) {
    if (OB_ENTRY_EXIST != ret) {
      COMMON_LOG(WARN, "Fail to put kv to ObKVGlobalCache, ", K_(cache_id), K(ret));
    }
  } else {
  }
  return ret;
}

template <class Key, class Value>
int ObKVCache<Key, Value>::get(const Key &key, const Value *&pvalue, ObKVCacheHandle &handle)
{
  int ret = OB_SUCCESS;
  const ObIKVCacheValue *value = NULL;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCache has not been inited, ", K(ret));
  } else {
    handle.reset();
    if (OB_FAIL(ObKVGlobalCache::get_instance().get(cache_id_, key, value, handle.hazptr_holder_))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        COMMON_LOG(WARN, "Fail to get value from ObKVGlobalCache, ", K(ret));
      }
    } else {
      pvalue = reinterpret_cast<const Value*> (value);
    }
  }
  return ret;
}

template <class Key, class Value>
int ObKVCache<Key, Value>::erase(const Key &key)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCache has not been inited, ", K(ret));
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().erase(cache_id_, key))) {
    COMMON_LOG(WARN, "Fail to erase key from ObKVGlobalCache, ", K_(cache_id), K(ret));
  }
  return ret;
}

template <class Key, class Value>
int ObKVCache<Key, Value>::alloc(const int64_t key_size, const int64_t value_size,
    ObKVCachePair *&kvpair, ObKVCacheHandle &handle, ObKVCacheInstHandle &inst_handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCache has not been inited, ", K(ret));
  } else if (OB_FAIL(ObKVGlobalCache::get_instance().alloc(
          cache_id_,
          key_size,
          value_size,
          kvpair,
          handle.hazptr_holder_,
          inst_handle))) {
    COMMON_LOG(WARN, "failed to alloc", K(ret));
  } else {
  }

  return ret;
}


template <class Key, class Value>
int64_t ObKVCache<Key, Value>::store_size() const
{
  int64_t store_size = 0;
  if (OB_LIKELY(inited_)) {
    int ret = OB_SUCCESS;
    ObKVCacheInstKey inst_key(cache_id_);
    ObKVCacheInstHandle inst_handle;
    if (OB_SUCC(ObKVGlobalCache::get_instance().insts_.get_cache_inst(inst_key, inst_handle))) {
      if (NULL != inst_handle.get_inst()) {
        store_size += inst_handle.get_inst()->status_.store_size_;
      }
    }
  }
  return store_size;

}

/*
 * ----------------------------------------------------ObKVCacheIterator---------------------------------------------
 */
template <class Key, class Value>
int ObKVCacheIterator::get_next_kvpair(
    const Key *&key,
    const Value *&value,
    ObKVCacheHandle &handle)
{
  int ret = OB_SUCCESS;
  ObKVCacheMap::Node node;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheIterator has not been inited, ", K(ret));
  } else {
    handle.reset();
    while (OB_SUCC(ret)) {
      if (pos_ >= map_->bucket_num_ && handle_list_.empty()) {
        ret = OB_ITER_END;
      } else if (OB_SUCC(handle_list_.pop_front(node))) {
        bool protect_success;
        if (OB_FAIL(handle.hazptr_holder_.protect(protect_success, node.mb_handle_, node.seq_num_))) {
          COMMON_LOG(WARN, "protect failed", KP(node.mb_handle_));
        } else if (protect_success) {
          break;
        }
      } else {
        if (common::OB_ENTRY_NOT_EXIST == ret) {
          if (pos_ >= map_->bucket_num_) {
            ret = OB_ITER_END;
          } else if (OB_FAIL(map_->multi_get(cache_id_, pos_++, handle_list_))) {
            COMMON_LOG(WARN, "Fail to multi get from map, ", K(ret));
          }
        } else {
          COMMON_LOG(WARN, "Unexpected error, ", K(ret));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    key = reinterpret_cast<const Key*>(node.key_);
    value = reinterpret_cast<const Value*>(node.value_);
  }
  return ret;
}

} // common
} // oceanbase

#endif //OCEANBASE_COMMON_KV_STORE_CACHE_H_
