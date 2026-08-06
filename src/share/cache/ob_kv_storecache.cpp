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

#include "lib/utility/ob_macro_utils.h"
#define USING_LOG_PREFIX COMMON

#include "share/cache/ob_kv_storecache.h"
#include "share/ob_task_define.h"
#include "share/ob_debug_sync.h"             // DEBUG_SYNC
#include "share/config/ob_server_config.h"

namespace oceanbase
{
using namespace lib;
namespace common
{

ObKVCacheHandle::ObKVCacheHandle()
  : hazptr_holder_()
{
}

ObKVCacheHandle::~ObKVCacheHandle()
{
  reset();
  hazptr_holder_.reset();
}

void ObKVCacheHandle::move_from(ObKVCacheHandle &other)
{
  if (&other != this) {
    reset();
    hazptr_holder_.move_from(other.hazptr_holder_);
  }
}

int ObKVCacheHandle::assign(const ObKVCacheHandle& other)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_FAIL(this->hazptr_holder_.assign(other.hazptr_holder_))) {
    COMMON_LOG(WARN, "Fail to assign hazptr_holder, ", K(ret));
  }
  return ret;
}

void ObKVCacheHandle::reset()
{
  if (hazptr_holder_.is_valid()) {
    hazptr_holder_.release();
  }
}

/*
 * ----------------------------------------ObKVCacheMapIterator---------------------------------------------------------
 */
ObKVCacheIterator::ObKVCacheIterator()
    : cache_id_(-1), map_(NULL), pos_(0), allocator_(ObModIds::OB_KVSTORE_CACHE_ITERATOR, OB_MALLOC_NORMAL_BLOCK_SIZE),
      handle_list_(allocator_), is_inited_(false)
{
}

ObKVCacheIterator::~ObKVCacheIterator()
{
  handle_list_.reset();
}

int ObKVCacheIterator::init(const int64_t cache_id, ObKVCacheMap * const map)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The ObKVCacheIterator has been inited, ", K(ret));
  } else if (OB_UNLIKELY(cache_id < 0) || OB_UNLIKELY(NULL == map)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(cache_id), KP(map), K(ret));
  } else {
    map_ = map;
    cache_id_ = cache_id;
    is_inited_ = true;
  }
  return ret;
}



/*
 * -------------------------------------------------------ObKVGlobalCache---------------------------------------------------------------
 */
//TODO bucket num level map should be system parameter
const int64_t ObKVGlobalCache::bucket_num_array_[MAX_BUCKET_NUM_LEVEL] =
    {
      196613l,      // more than 2G, 1.5M kvcache meta
      393241l,      // more than 4G, 3M kvcache meta
      786433l,      // more than 8G, 6M kvcache meta
      1572869l,     // more than 16G, 12M kvcache meta
      3145739l,     // more than 32G, 25M kvcache meta
      6291469l,     // more than 64G, 50M kvcache meta
      12582917l,    // more than 128G, 100M kvcache meta
      25165843l,    // more than 256G, 200M kvcache meta
      50331653l,    // more than 512G, 500M kvcache meta
      100663319l,   // more than 1024G, 1G kvcache meta
    };

ObKVGlobalCache::ObKVGlobalCache()
    : inited_(false),
      cache_num_(0),
      mutex_(common::ObLatchIds::GLOBAL_KV_CACHE_CONFIG_LOCK),
      map_clean_pos_(0),
      map_once_clean_num_(0),
      map_replace_pos_(0),
      map_once_replace_num_(0),
      map_replace_skip_count_(0),
      wash_timer_(),
      replace_timer_(),
      stopped_(true),
      cache_wash_interval_(0)
{
}

ObKVGlobalCache::~ObKVGlobalCache()
{
  destroy();
}

ObKVGlobalCache &ObKVGlobalCache::get_instance()
{
  static ObKVGlobalCache instance_;
  return instance_;
}

int ObKVGlobalCache::calculate_suitable_bucket_num(
    const int64_t cache_memory_limit,
    int64_t &bucket_num)
{
  INIT_SUCC(ret);
  bucket_num = -1;
  if (cache_memory_limit <= 0) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const int64_t bounded_limit = MIN(cache_memory_limit, MAX_CACHE_SIZE);
    const int64_t required_bucket_num =
        upper_align(bounded_limit, KVCACHE_BYTES_PER_BUCKET)
        / KVCACHE_BYTES_PER_BUCKET;
    int64_t bucket_level = 0;
    while (bucket_level < MAX_BUCKET_NUM_LEVEL - 1
           && bucket_num_array_[bucket_level] < required_bucket_num) {
      ++bucket_level;
    }
    bucket_num = bucket_num_array_[bucket_level];
  }

  return ret;
}

int ObKVGlobalCache::get_suitable_bucket_num(int64_t& bucket_num)
{
  const int64_t cache_memory_limit = GMEMCONF.get_kvcache_memory_limit();
  int ret = calculate_suitable_bucket_num(cache_memory_limit, bucket_num);
  if (OB_FAIL(ret)) {
    COMMON_LOG(ERROR, "failed to calculate suitable bucket num", K(ret),
               K(cache_memory_limit));
  } else {
    share::ObTaskController::get().allow_next_syslog();
    COMMON_LOG(INFO, "The ObKVGlobalCache set suitable kvcache buckets", K(bucket_num),
               K(cache_memory_limit));
  }

  return ret;
}

int ObKVGlobalCache::init(
    const int64_t bucket_num,
    const int64_t max_cache_size,
    const int64_t block_size,
    const int64_t cache_wash_interval)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The ObKVGlobalCache has been inited, ", K(ret));
  } else if (bucket_num <= 0 ||
             max_cache_size <= 0 ||
             max_cache_size > MAX_CACHE_SIZE ||
             block_size <= 0 ||
             cache_wash_interval < 0) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(ret), K(bucket_num),
               K(max_cache_size), K(block_size), K(cache_wash_interval));
  } else if (OB_FAIL(hazard_domain_.init(ObKVCacheStore::compute_mb_handle_num(max_cache_size, block_size)))) {
    COMMON_LOG(WARN, "Fail to init hazard domain, ", K(ret));
  } else if (OB_FAIL(store_.init(max_cache_size, block_size))) {
    COMMON_LOG(WARN, "Fail to init store, ", K(ret));
  } else if (OB_FAIL(map_.init(hash::cal_next_prime(bucket_num), &store_))) {
    COMMON_LOG(WARN, "Fail to init map, ", K(ret), K(bucket_num));
  } else if (OB_FAIL(insts_.init(MAX_CACHE_NUM, configs_, map_.get_node_allocator()))) {
    COMMON_LOG(WARN, "Fail to init insts, ", K(ret));
  } else if (OB_FAIL(wash_timer_.init("KVCacheWash", ObMemAttr("KVCacheWash")))) {
    COMMON_LOG(WARN, "Fail to init wash timer, ", K(ret));
  } else if (OB_FAIL(replace_timer_.init("KVCacheRep", ObMemAttr("KVCacheRep")))) {
    COMMON_LOG(WARN, "Fail to init replace timer", K(ret));
  } else if (FALSE_IT(cache_wash_interval_ = cache_wash_interval)) {
  } else if (OB_FAIL(reload_wash_interval())) {
    COMMON_LOG(WARN, "failed to reload wash interval", K(ret));
  } else {
    cache_num_ = 0;
    stopped_ = false;
    map_once_clean_num_ = bucket_num / MAP_ONCE_CLEAN_RATIO;
    if (map_once_clean_num_ > MAX_MAP_ONCE_CLEAN_NUM) {
      map_once_clean_num_ = MAX(MAX_MAP_ONCE_CLEAN_NUM, map_once_clean_num_/EXPAND_MAP_ONCE_CLEAN_RATIO);
    }
    map_once_replace_num_ = min(MAX_MAP_ONCE_REPLACE_NUM, bucket_num / MAP_ONCE_REPLACE_RATIO);
    inited_ = true;
  }

  if (OB_UNLIKELY(!inited_)) {
    destroy();
    COMMON_LOG(ERROR, "Fail to create ObKVGlobalCache, ", K(ret));
  } else {
    COMMON_LOG(INFO, "ObKVGlobalCache has been inited!", K(bucket_num), K(max_cache_size), K(block_size));
  }

  return ret;
}

void ObKVGlobalCache::stop()
{
  if (inited_) {
    stopped_ = true;
    wash_timer_.stop();
    replace_timer_.stop();
  }
}

void ObKVGlobalCache::wait()
{
  if (inited_) {
    wash_timer_.wait();
    replace_timer_.wait();
  }
}

void ObKVGlobalCache::destroy()
{
  if (inited_) {
    COMMON_LOG(INFO, "Begin destroy the ObKVGlobalCache!");
    // should destroy store_ before timer threads exit, before some mb_handles may
    // cache in wash thread.
    stop();
    wait();
    wash_timer_.destroy();
    replace_timer_.destroy();
    map_.destroy();
    store_.destroy();
    hazard_domain_.reset_retire_list();
    insts_.destroy();
    for (int64_t i = 0; i < MAX_CACHE_NUM; ++i) {
      configs_[i].reset();
    }
    cache_num_ = 0;
    inited_ = false;
    COMMON_LOG(INFO, "The ObKVGlobalCache has been destroyed!");
  }
}

int ObKVGlobalCache::put(
  const int64_t cache_id,
  const ObIKVCacheKey &key,
  const ObIKVCacheValue &value,
  const ObIKVCacheValue *&pvalue,
  HazptrHolder &hazptr_holder,
  bool overwrite)
{
  return put(store_, cache_id, key, value, pvalue, hazptr_holder, overwrite);
}

int ObKVGlobalCache::put(
    ObIKVCacheStore &store,
    const int64_t cache_id,
    const ObIKVCacheKey &key,
    const ObIKVCacheValue &value,
    const ObIKVCacheValue *&pvalue,
    HazptrHolder &hazptr_holder,
    bool overwrite)
{
  int ret = OB_SUCCESS;
  ObKVCacheInstKey inst_key(cache_id);
  ObKVCacheInstHandle inst_handle;
  ObKVCachePair *kvpair = NULL;
  pvalue = NULL;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(!inst_key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid inst_key", K(inst_key), K(ret));
  } else if (OB_FAIL(insts_.get_cache_inst(inst_key, inst_handle))) {
    COMMON_LOG(WARN, "Fail to get cache inst, ", K(ret));
  } else if (NULL == inst_handle.get_inst()) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "The inst is NULL, ", K(ret));
  } else if (!overwrite) {
    if (OB_FAIL(map_.get(cache_id, key, pvalue, hazptr_holder))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        COMMON_LOG(WARN, "KVCacheMap::get failed", K(ret));
      } else {
        ret = OB_SUCCESS;
      }
    } else {
      ret = OB_ENTRY_EXIST;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(store.store(key, value, kvpair, hazptr_holder))) {
    COMMON_LOG(WARN, "Fail to store kvpair to store, ", K(ret));
  } else {
    pvalue = kvpair->value_;
    if (OB_FAIL(map_.put(*inst_handle.get_inst(), key, kvpair, hazptr_holder, overwrite))) {
      if (OB_ENTRY_EXIST != ret) {
        COMMON_LOG(WARN, "Fail to put kvpair to map, ", K(ret));
      }
    }
  }

  if (OB_FAIL(ret)) {
    if (OB_ENTRY_EXIST != ret) {
      revert(hazptr_holder);
      pvalue = NULL;
    }
  }
  return ret;

}

int ObKVGlobalCache::alloc(
    const int64_t cache_id,
    const int64_t key_size,
    const int64_t value_size,
    ObKVCachePair *&kvpair,
    HazptrHolder &hazptr_holder,
    ObKVCacheInstHandle &inst_handle)
{
  return alloc(store_, cache_id, key_size, value_size, kvpair, hazptr_holder, inst_handle);
}

int ObKVGlobalCache::alloc(
    ObIKVCacheStore &store,
    const int64_t cache_id,
    const int64_t key_size,
    const int64_t value_size,
    ObKVCachePair *&kvpair,
    HazptrHolder &hazptr_holder,
    ObKVCacheInstHandle &inst_handle)
{
  int ret = OB_SUCCESS;
  ObKVCacheInstKey inst_key(cache_id);
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCache has not been inited, ", K(ret));
  } else if (hazptr_holder.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Cannot overwrite valid hazptr_holder", K(ret), K(hazptr_holder));
  } else if (OB_FAIL(insts_.get_cache_inst(inst_key, inst_handle))) {
    COMMON_LOG(WARN, "Fail to get cache inst, ", K(ret));
  } else if (OB_ISNULL(inst_handle.get_inst())) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "The inst is NULL, ", K(ret));
  } else if (OB_FAIL(store.alloc_kvpair(
          key_size, value_size, kvpair, hazptr_holder))) {
    COMMON_LOG(WARN, "Fail to store kvpair, ", K(ret));
  }
  return ret;
}

int ObKVGlobalCache::get(
  const int64_t cache_id,
  const ObIKVCacheKey &key,
  const ObIKVCacheValue *&pvalue,
  HazptrHolder &hazptr_holder)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache has not been inited, ", K(ret));
  } else if (FALSE_IT(revert(hazptr_holder))) {
  } else if (OB_FAIL(map_.get(cache_id, key, pvalue, hazptr_holder))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      COMMON_LOG(WARN, "fail to get value from map, ", K(ret));
    }
  }
  return ret;
}

int ObKVGlobalCache::erase(const int64_t cache_id, const ObIKVCacheKey &key)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache has not been inited, ", K(ret));
  } else if (OB_FAIL(map_.erase(cache_id, key))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      COMMON_LOG(WARN, "Fail to erase key from cache, ", K(cache_id), K(ret));
    } else {
      // has been erased via wash()
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObKVGlobalCache::erase_cache()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else {
    store_.flush_washable_mbs(false);
    if (OB_FAIL(map_.erase_all())) {
      COMMON_LOG(WARN, "fail to erase cache, ", K(ret));
    }
  }
  return ret;
}

int ObKVGlobalCache::erase_cache(const char *cache_name)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheMap has not been inited, ", K(ret));
  } else {
    int64_t cache_id = -1;
    for (int16_t i = 0; i < MAX_CACHE_NUM; ++i) {
      if (configs_[i].is_valid_) {
        if (0 == STRNCMP(configs_[i].cache_name_, cache_name, MAX_CACHE_NAME_LENGTH)) {
          cache_id = i;
          break;
        }
      }
    }
    if (-1 != cache_id) {
      store_.flush_washable_mbs(false);
      if (OB_FAIL(map_.erase_all(cache_id))) {
        COMMON_LOG(WARN, "fail to erase cache, ", K(ret), K(cache_id));
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "Invalid argument, ", K(ret));
    }
  }
  return ret;
}

int ObKVGlobalCache::register_cache(
  const char *cache_name,
  const int64_t mem_limit_pct,
  int64_t &cache_id)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache has not been inited, ", K(ret));
  } else if (NULL == cache_name) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", KP(cache_name), K(ret));
  } else {
    int64_t i = 0;
    lib::ObMutexGuard guard(mutex_);
    for (i = 0; OB_SUCC(ret) && i < cache_num_; ++i) {
      if (configs_[i].is_valid_) {
        if (0 == STRNCMP(cache_name, configs_[i].cache_name_, MAX_CACHE_NAME_LENGTH)) {
          ret = OB_INVALID_ARGUMENT;
          COMMON_LOG(WARN, "The cache name has been registered, ", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (cache_num_ >= MAX_CACHE_NUM) {
        ret = OB_SIZE_OVERFLOW;
        COMMON_LOG(WARN, "Can not register more cache, ", K(ret));
      } else {
        cache_id = cache_num_++;
        STRNCPY(configs_[cache_id].cache_name_, cache_name, MAX_CACHE_NAME_LENGTH - 1);
        configs_[cache_id].cache_name_[MAX_CACHE_NAME_LENGTH - 1] = '\0';
        configs_[cache_id].mem_limit_pct_ = mem_limit_pct;
        configs_[cache_id].is_valid_ = true;
      }
    }
  }

  return ret;
}

void ObKVGlobalCache::deregister_cache(const int64_t cache_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache has not been inited, ", K(ret));
  } else if (OB_UNLIKELY(cache_id < 0) || OB_UNLIKELY(cache_id >= MAX_CACHE_NUM)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(cache_id), K(ret));
  } else {
    lib::ObMutexGuard guard(mutex_);
    configs_[cache_id].is_valid_ = false;
  }

  if (OB_SUCC(ret)) {
    COMMON_LOG(INFO, "Success to deregister cache, ", K(cache_id));
  }
}

ERRSIM_POINT_DEF(ERRSIM_FLUSH_KVCACHE, "flush kvcache every ERROR_CODE s");

void ObKVGlobalCache::wash()
{
  if (OB_LIKELY(inited_ && !stopped_)) {
    DEBUG_SYNC(BEFORE_BACKGROUND_WASH);
    if (store_.wash()) {
      map_.clean_garbage_node(map_clean_pos_, map_once_clean_num_);
    }
    int sec = -ERRSIM_FLUSH_KVCACHE;
    if (sec != 0 && REACH_TIME_INTERVAL(sec * 1000000)) {
      store_.flush_washable_mbs(false);
    }
  }
}

void ObKVGlobalCache::replace_map()
{
  if (inited_ && !stopped_) {
    int ret = OB_SUCCESS;
    int64_t replace_node_count = 0;
    if (map_replace_skip_count_ <= 0) {
      if (OB_FAIL(map_.replace_fragment_node(map_replace_pos_, replace_node_count, map_once_replace_num_))) {
      } else if (0 == replace_node_count) {
        map_replace_skip_count_ = MAP_REPLACE_ONCE_SKIP_COUNT;
      }
    } else {
      --map_replace_skip_count_;
    }
    COMMON_LOG(INFO, "replace map num details", K(ret), K(replace_node_count), K(map_once_replace_num_),
                                                K(map_replace_skip_count_));
  }
}

void ObKVGlobalCache::revert(HazptrHolder& hazptr_holder)
{
  ObKVMemBlockHandle* mb_handle = hazptr_holder.get_mb_handle();
  if (inited_ && NULL != mb_handle) {
    hazptr_holder.release();
  }
}

int ObKVGlobalCache::reload_wash_interval()
{
  int ret = OB_SUCCESS;
  if (0 == cache_wash_interval_) {
    const int64_t wash_interval = GCONF._cache_wash_interval;
    bool is_exist = wash_timer_.task_exist(wash_task_);
    if (is_exist && OB_FAIL(wash_timer_.cancel_task(wash_task_))) {
      COMMON_LOG(WARN, "failed to cancel wash task", K(ret));
    } else if (OB_FAIL(wash_timer_.schedule(wash_task_, wash_interval, true))) {
      COMMON_LOG(WARN, "failed to schedule wash task", K(ret));
    }

    is_exist = false;
    if (OB_FAIL(ret)) {
    } else if (FALSE_IT(is_exist = replace_timer_.task_exist(replace_task_))) {
    } else if (is_exist && OB_FAIL(replace_timer_.cancel_task(replace_task_))) {
      COMMON_LOG(WARN, "failed to cancel replace task", K(ret));
    } else if (OB_FAIL(replace_timer_.schedule(replace_task_, wash_interval, true))) {
      COMMON_LOG(WARN, "failed to schedule replace task", K(ret));
    }
    if (OB_SUCC(ret)) {
      COMMON_LOG(INFO, "success to reload_wash_interval", K(wash_interval));
    }
  } else if (!inited_) {
    if (OB_FAIL(wash_timer_.schedule(wash_task_, cache_wash_interval_, true))) {
      COMMON_LOG(WARN, "failed to schedule wash task", K(ret));
    } else if (OB_FAIL(replace_timer_.schedule(replace_task_,
                                               cache_wash_interval_, true))) {
      COMMON_LOG(WARN, "failed to schedule replace task", K(ret));
    }
  }
  return ret;
}

int ObKVGlobalCache::get_washable_size(int64_t &washable_size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "not init", K(ret));
  } else if (OB_FAIL(store_.get_washable_size(washable_size))) {
    COMMON_LOG(WARN, "get washable size failed", K(ret), K(washable_size));
  }
  return ret;
}

int ObKVGlobalCache::sync_wash_mbs(const int64_t wash_size,
                                   ObICacheWasher::ObCacheMemBlock *&wash_blocks)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "not init", K(ret));
  } else if (wash_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid arguments", K(ret), K(wash_size));
  } else if (OB_FAIL(store_.sync_wash_mbs(wash_size, wash_blocks))) {
    if (ret != OB_CACHE_FREE_BLOCK_NOT_ENOUGH) {
      COMMON_LOG(WARN, "sync_wash_mbs failed", K(ret), K(wash_size));
    }
  }
  return ret;
}

void ObKVGlobalCache::print_all_cache_info()
{
  if (OB_UNLIKELY(!inited_)) {
    COMMON_LOG_RET(WARN, common::OB_NOT_INIT, "The ObKVGlobalCache has not been inited, ");
  } else {
    insts_.print_all_cache_info();
    map_.print_hazard_version_info();
    HazardDomain::get_instance().print_info();
  }
}

int ObKVGlobalCache::get_cache_inst_info(ObIArray<ObKVCacheInstHandle> &inst_handles)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache has not been inited", K(ret));
  } else if (OB_FAIL(insts_.get_cache_info(inst_handles))) {
    COMMON_LOG(WARN, "Fail to get all cache info", K(ret));
  }

  return ret;
}

int ObKVGlobalCache::get_memblock_info(ObIArray<ObKVCacheStoreMemblockInfo> &memblock_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache has not been inited", K(ret));
  } else if (OB_FAIL(store_.get_memblock_info(memblock_infos))) {
    COMMON_LOG(WARN, "Fail to get all memblock info", K(ret));
  }
  return ret;
}


int ObKVGlobalCache::get_cache_id(const char *cache_name, int64_t &cache_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache is not inited", K(ret));
  } else if (NULL == cache_name) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "invalid cache_name", K(ret), KP(cache_name));
  } else {
    bool find = false;
    lib::ObMutexGuard guard(mutex_);
    for (int64_t i = 0; !find && OB_SUCC(ret) && i < cache_num_; ++i) {
      if (configs_[i].is_valid_) {
        if (0 == STRNCMP(cache_name, configs_[i].cache_name_, MAX_CACHE_NAME_LENGTH)) {
          cache_id = i;
          find = true;
        }
      }
    }
    if (!find) {
      ret = OB_ENTRY_NOT_EXIST;
      COMMON_LOG(WARN, "cache not exist", K(ret), K(cache_name));
    }
  }
  return ret;
}

int ObKVGlobalCache::get_cache_name(const int64_t cache_id, char *cache_name)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVGlobalCache is not inited", K(ret));
  } else if (cache_id < 0 || cache_id > cache_num_ || nullptr == cache_name) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument", K(ret), K(cache_id), KP(cache_name));
  } else {
    MEMCPY(cache_name, configs_[cache_id].cache_name_, MAX_CACHE_NAME_LENGTH);
  }
  return ret;
}


} // namespace common
} // namespace oceanbase
