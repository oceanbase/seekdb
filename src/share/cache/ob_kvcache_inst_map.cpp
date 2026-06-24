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

#include "share/cache/ob_kvcache_inst_map.h"
#include "share/cache/ob_kvcache_hazard_domain.h"


namespace oceanbase
{
using namespace lib;
namespace common
{
int ObTenantMBList::init()
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "init twice", K(ret));
  } else if (OB_FAIL(ObResourceMgr::get_instance().get_tenant_resource_mgr(
      resource_mgr_))) {
    COMMON_LOG(WARN, "get_tenant_resource_mgr failed", K(ret));
  } else {
    head_.reset();
    head_.prev_ = &head_;
    head_.next_ = &head_;
    ref_cnt_ = 0;
    inited_ = true;
  }
  return ret;
}

/**
 * ---------------------------------------------------------ObKVCacheInst-----------------------------------------------------
 */
bool ObKVCacheInst::can_destroy() const
{
  return is_delete_
      && 0 == ATOMIC_LOAD(&ref_cnt_)
      && 0 == status_.kv_cnt_
      && 0 == status_.store_size_
      && 0 == status_.lru_mb_cnt_
      && 0 == status_.lfu_mb_cnt_;
}

void ObKVCacheInst::try_mark_delete()
{
  if (!is_delete_) {
    is_delete_ = true;
    ATOMIC_DEC(&ref_cnt_);
  }
}

/**
 * ---------------------------------------------------------ObKVCacheInstHandle-----------------------------------------------------
 */
ObKVCacheInstHandle::ObKVCacheInstHandle()
  : map_(NULL), inst_(NULL)
{
}

ObKVCacheInstHandle::~ObKVCacheInstHandle()
{
  reset();
}

void ObKVCacheInstHandle::reset()
{
  if (NULL != map_ && NULL != inst_) {
    map_->de_inst_ref(inst_);
  }
  map_ = NULL;
  inst_ = NULL;
}

bool ObKVCacheInstHandle::is_valid() const
{
  return (nullptr != map_) && (nullptr != inst_);
}

ObKVCacheInstHandle::ObKVCacheInstHandle(const ObKVCacheInstHandle &other)
{
  map_ = other.map_;
  inst_ = other.inst_;
  if (NULL != map_ && NULL != inst_) {
    map_->add_inst_ref(inst_);
  }
}

ObKVCacheInstHandle& ObKVCacheInstHandle::operator = (const ObKVCacheInstHandle& other)
{
  if (map_ == other.map_ && inst_ == other.inst_) { // do nothing
  } else {
    reset();
    map_ = other.map_;
    inst_ = other.inst_;
    if (NULL != map_ && NULL != inst_) {
      map_->add_inst_ref(inst_);
    }
  }
  return *this;
}

ObKVCacheInstMap::ObKVCacheInstMap()
  : lock_(common::ObLatchIds::KV_CACHE_INST_LOCK),
    inst_map_(),
    configs_(NULL),
    mem_limit_getter_(NULL),
    node_allocator_(NULL),
    is_inited_(false)
{
}

ObKVCacheInstMap::~ObKVCacheInstMap()
{
  destroy();
}

int ObKVCacheInstMap::init(const int64_t max_entry_cnt, const ObKVCacheConfig *configs,
                           const ObITenantMemLimitGetter &mem_limit_getter,
                           ObLfFIFOAllocator *node_allocator)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The ObKVCacheInstMap has been inited, ", K(ret));
  } else if (max_entry_cnt <= 0 || NULL == configs || NULL == node_allocator) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Invalid argument, ", K(max_entry_cnt), KP(configs), KP(node_allocator), K(ret));
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(inst_map_.create(max_entry_cnt, "CACHE_INST_MAP", "CACHE_INST_MAP"))) {
      COMMON_LOG(WARN, "Fail to create inst map, ", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    configs_ = configs;
    mem_limit_getter_ = &mem_limit_getter;
    node_allocator_ = node_allocator;
    is_inited_ = true;
  }

  if (!is_inited_) {
    destroy();
  }
  return ret;
}

void ObKVCacheInstMap::destroy()
{
  inst_map_.destroy();
  configs_ = NULL;
  node_allocator_ = NULL;
  is_inited_ = false;
}

int ObKVCacheInstMap::get_cache_inst(
    const ObKVCacheInstKey &inst_key,
    ObKVCacheInstHandle &inst_handle)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheInstMap has not been inited, ", K(ret));
  } else if (!inst_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "The inst_key is not valid, ", K(ret));
  } else {
    inst_handle.reset();
    ObKVCacheInst *inst = NULL;
    //try get store tenant handle
    {
      DRWLock::RDLockGuard rd_guard(lock_);
      if (OB_SUCC(inst_map_.get_refactored(inst_key, inst))) {
        //success to get st_handle, add ref to return outside
        add_inst_ref(inst);
      }
    }

    if (OB_HASH_NOT_EXIST == ret) {
      DRWLock::WRLockGuard wr_guard(lock_);
      if (OB_SUCC(inst_map_.get_refactored(inst_key, inst))) {
        //double check, success to get inst, add ref to return outside
        add_inst_ref(inst);
      } else if (OB_HASH_NOT_EXIST == ret) {
        inst = OB_NEW(ObKVCacheInst, ObMemAttr("CACHE_INST"));
        if (OB_ISNULL(inst)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN, "Fail to alloc cache inst, ", K(ret));
        } else if (OB_FAIL(inst_map_.set_refactored(inst_key, inst))) {
          COMMON_LOG(WARN, "Fail to set inst to inst map, ", K(ret));
        } else {
          inst->cache_id_ = inst_key.cache_id_;
          inst->node_allocator_ = node_allocator_;
          inst->status_.config_ = &configs_[inst_key.cache_id_];
          if (0 == STRNCMP(inst->status_.config_->cache_name_, "index_block_cache", MAX_CACHE_NAME_LENGTH)
            || 0 == STRNCMP(inst->status_.config_->cache_name_, "user_block_cache", MAX_CACHE_NAME_LENGTH)) {
            inst->is_block_cache_ = true;
          }

          //the first ref is kept by inst_map_
          add_inst_ref(inst);
          //the second ref is return outside
          add_inst_ref(inst);
        }

        if (OB_FAIL(ret) && NULL != inst) {
          inst->reset();
          int tmp_ret = OB_SUCCESS;
          ob_delete(inst);
          if (OB_SUCCESS != (tmp_ret = inst_map_.erase_refactored(inst_key))) {
            if (OB_HASH_NOT_EXIST != tmp_ret) {
              COMMON_LOG(ERROR, "Fail to erase inst key, ", K(ret));
            }
          }
          inst = NULL;
        }
      }
    }

    if (OB_SUCC(ret) && NULL != inst) {
      inst_handle.map_ = this;
      inst_handle.inst_ = inst;
    }
  }
  return ret;
}

int ObKVCacheInstMap::mark_tenant_delete()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheInstMap has not been inited", K(ret));
  } else {
    ObKVCacheInst *inst = nullptr;
    DRWLock::WRLockGuard wr_guard(lock_);
    for (KVCacheInstMap::iterator iter = inst_map_.begin() ; OB_SUCC(ret) && iter != inst_map_.end() ; ++iter) {
      if (OB_ISNULL(iter->second)) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "Unexpected null cache inst", K(ret));
      } else {
        iter->second->try_mark_delete();
      }
    }
    COMMON_LOG(INFO, "mark delete details", K(ret));
  }

  return ret;
}

int ObKVCacheInstMap::erase_tenant()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheInstMap has not been inited", K(ret));
  } else {
    ObSEArray<ObKVCacheInstKey, MAX_CACHE_NUM> erase_key_list;
    ObSEArray<ObKVCacheInst *, MAX_CACHE_NUM> erase_inst_list;
    DRWLock::WRLockGuard wr_guard(lock_);
    ObKVCacheInst *inst = nullptr;
    for (KVCacheInstMap::iterator iter = inst_map_.begin() ; OB_SUCC(ret) && iter != inst_map_.end() ; ++iter) {
      inst = iter->second;
      if (OB_ISNULL(inst)) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "Unexpected null cache inst", K(ret));
      } else if (!inst->can_destroy()) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "Still can not destroy cache inst", K(ret), KPC(inst), K(inst->status_.store_size_),
                   K(inst->status_.kv_cnt_), K(inst->status_.lfu_mb_cnt_), K(inst->status_.lru_mb_cnt_));
      } else if (OB_FAIL(erase_key_list.push_back(iter->first))) {
        COMMON_LOG(WARN, "Fail to push back erase inst key", K(ret));
      } else if (OB_FAIL(erase_inst_list.push_back(inst))) {
        COMMON_LOG(WARN, "Fail to push back erase inst key", K(ret));
      }
    }
    for (int i = 0 ; OB_SUCC(ret) && i < erase_key_list.count() ; ++i) {
      ObKVCacheInstKey tmp_key = erase_key_list.at(i);
      inst = erase_inst_list.at(i);
      if (OB_FAIL(inst_map_.erase_refactored(tmp_key))) {
        COMMON_LOG(WARN, "Fail to erase cache inst from inst map", K(ret));
      } else if (FALSE_IT(inst->reset())) {
      } else {
        ob_delete(inst);
      }
    }
  }
  COMMON_LOG(INFO, "erase tenant cache inst details", K(ret));

  return ret;
}

int ObKVCacheInstMap::refresh_score()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheInstMap has not been inited, ", K(ret));
  } else {
    int64_t mb_cnt = 0;
    double avg_hit = 0;
    ObKVCacheInst *inst = NULL;
    int64_t total_hit_cnt = 0;
    DRWLock::RDLockGuard rd_guard(lock_);
    for (KVCacheInstMap::iterator iter = inst_map_.begin(); OB_SUCC(ret) && iter != inst_map_.end(); ++iter) {
      inst = iter->second;
      mb_cnt = ATOMIC_LOAD(&inst->status_.lru_mb_cnt_) + ATOMIC_LOAD(&inst->status_.lfu_mb_cnt_);
      avg_hit = 0;
      total_hit_cnt = inst->status_.total_hit_cnt_.value();
      if (mb_cnt > 0) {
        avg_hit = double (total_hit_cnt - inst->status_.last_hit_cnt_) / (double) mb_cnt;
      }
      inst->status_.last_hit_cnt_ = total_hit_cnt;
      inst->status_.base_mb_score_ = inst->status_.base_mb_score_ * CACHE_SCORE_DECAY_FACTOR
          + avg_hit;
    }
  }
  return ret;
}

int ObKVCacheInstMap::get_cache_info(ObIArray<ObKVCacheInstHandle> &inst_handles)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The ObKVCacheInstMap has not been inited, ", K(ret));
  } else {
    DRWLock::RDLockGuard rd_guard(lock_);
    for (KVCacheInstMap::iterator iter = inst_map_.begin(); OB_SUCC(ret) && iter != inst_map_.end(); ++iter) {
      if (OB_ISNULL(iter->second)) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN, "Unexpected null cache inst", K(ret));
      } else if (iter->second->is_mark_delete()) {
      } else if (OB_FAIL(inner_push_inst_handle(iter, inst_handles))) {
        COMMON_LOG(WARN, "Fail to inner push cache inst", K(ret));
      }
    }
  }
  return ret;
}

void ObKVCacheInstMap::print_all_cache_info()
{
  int ret = OB_SUCCESS;

  if (OB_LIKELY(is_inited_)) {
    ContextParam param;
    param.set_mem_attr(ObModIds::OB_TEMP_VARIABLES);
    CREATE_WITH_TEMP_CONTEXT(param) {
      static const int64_t BUFLEN = 1 << 17;
      char *buf = (char *)ctxalp(BUFLEN);
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        COMMON_LOG(ERROR, "no memory", K(ret));
      } else {
        int64_t total_map_size = 0;
        int64_t total_kv_cnt = 0;
        int64_t ctx_pos = 0;
        {
          DRWLock::RDLockGuard rd_guard(lock_);
          if (nullptr != node_allocator_) {
            total_map_size = node_allocator_->allocated();
          }
          for (KVCacheInstMap::iterator iter = inst_map_.begin(); iter != inst_map_.end(); ++iter) {
            if (OB_NOT_NULL(iter->second)) {
              total_kv_cnt += MAX(ATOMIC_LOAD(&iter->second->status_.kv_cnt_), 0);
            }
          }
          for (KVCacheInstMap::iterator iter = inst_map_.begin(); iter != inst_map_.end(); ++iter) {
            const int64_t inst_kv_cnt = MAX(ATOMIC_LOAD(&iter->second->status_.kv_cnt_), 0);
            const int64_t cache_map_size = total_kv_cnt > 0
                ? (total_map_size * inst_kv_cnt) / total_kv_cnt
                : 0;
            ret = databuff_printf(buf, BUFLEN, ctx_pos,
                "[CACHE] cache_name=%30s | cache_size=%12ld | cache_store_size=%12ld | cache_retired_size=%12ld | cache_map_size=%12ld | kv_cnt=%8ld\n",
                iter->second->status_.config_->cache_name_,
                iter->second->status_.store_size_ + cache_map_size,
                iter->second->status_.store_size_,
                iter->second->status_.retired_size_,
                cache_map_size,
                iter->second->status_.kv_cnt_);
          }
          ret = databuff_printf(buf, BUFLEN, ctx_pos,
              "[CACHE] shared_cache_map_size=%12ld | total_kv_cnt=%8ld\n",
              total_map_size, total_kv_cnt);
        }
        _OB_LOG(INFO, "[CACHE] cache memory info: \n%s", buf);
      }
    }
  }
}

void ObKVCacheInstMap::add_inst_ref(ObKVCacheInst *inst)
{
  if (OB_UNLIKELY(NULL != inst)) {
    (void) ATOMIC_AAF(&inst->ref_cnt_, 1);
  }
}

void ObKVCacheInstMap::de_inst_ref(ObKVCacheInst *inst)
{
  if (OB_UNLIKELY(NULL != inst)) {
    (void) ATOMIC_SAF(&inst->ref_cnt_, 1);
  }
}

int ObKVCacheInstMap::inner_push_inst_handle(const KVCacheInstMap::iterator &iter, ObIArray<ObKVCacheInstHandle> &inst_handles)
{
  INIT_SUCC(ret);

  ObKVCacheInstHandle handle;
  handle.inst_ = iter->second;
  handle.map_ = this;
  add_inst_ref(handle.inst_);
  if (OB_FAIL(inst_handles.push_back(handle))) {
    COMMON_LOG(WARN, "Fail to push back inst handle to array", K(ret));
  }

  return ret;
}


}//end namespace common
}//end namespace oceanbase
