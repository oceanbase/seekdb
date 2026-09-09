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

#define USING_LOG_PREFIX SQL_PC
#include "ob_i_lib_cache_node.h"
#include "sql/plan_cache/ob_plan_cache.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{
static constexpr int64_t PLAN_CACHE_LOCK_TIMEOUT_US = 100L;


ObILibCacheNode::~ObILibCacheNode()
{
  IGNORE_RETURN remove_all_plan_stat();
  free_cache_obj_array();
  co_list_.reset();
}

int64_t ObILibCacheNode::detach_cache_obj_owners()
{
  int64_t detached_count = 0;
  for (CacheObjList::iterator iter = co_list_.begin(); iter != co_list_.end(); ++iter) {
    ObILibCacheObject *obj = *iter;
    if (OB_ISNULL(obj)) {
      LOG_ERROR_RET(OB_ERR_UNEXPECTED, "invalid null cache object");
    } else {
      ObByteLockGuard obj_guard(obj->cache_node_lock_);
      if (obj->cache_node_ == this) {
        obj->cache_node_ = nullptr;
        obj->set_added_lc(false);
        ++detached_count;
      } else if (OB_NOT_NULL(obj->cache_node_)) {
        LOG_ERROR_RET(OB_ERR_UNEXPECTED, "cache object belongs to another node",
                      KP(obj), KP(this), KP(obj->cache_node_));
      }
    }
  }
  return detached_count;
}

int ObILibCacheNode::init(ObILibCacheCtx &ctx, const ObILibCacheObject *cache_obj)
{
  UNUSED(ctx);
  UNUSED(cache_obj);
  int ret = OB_SUCCESS;
  return ret;
}

void ObILibCacheNode::free_cache_obj_array()
{
  if (OB_ISNULL(lib_cache_)) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "lib cache is invalid");
  } else {
    ObLCObjectManager &mgr = lib_cache_->get_cache_obj_mgr();
    ObILibCacheObject* obj = nullptr;
    while (!co_list_.empty()) {
      co_list_.pop_front(obj);
      if (OB_ISNULL(obj)) {
        //do nothing
      } else {
        {
          ObByteLockGuard obj_guard(obj->cache_node_lock_);
          if (obj->cache_node_ == this) {
            LOG_ERROR_RET(OB_ERR_UNEXPECTED,
                          "cache object owner was not detached before node destruction",
                          KP(obj), KP(this));
            obj->cache_node_ = nullptr;
            obj->set_added_lc(false);
          } else if (OB_NOT_NULL(obj->cache_node_)) {
            LOG_ERROR_RET(OB_ERR_UNEXPECTED,
                          "cache object belongs to another node during node destruction",
                          KP(obj), KP(this), KP(obj->cache_node_));
          } else {
            obj->set_added_lc(false);
          }
        }
        mgr.free(obj);
        obj = NULL;
      }
    }
  }
}

int ObILibCacheNode::remove_all_plan_stat()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(lib_cache_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lib cache is invalid");
  } else {
    ObILibCacheObject* obj = nullptr;
    CacheObjList::const_iterator iter = co_list_.begin();
    for (; iter != co_list_.end(); iter++) {
      if (OB_ISNULL(obj = *iter)) {
        // do nothing
      } else {
        const int tmp_ret = lib_cache_->remove_cache_obj_stat_entry(obj->get_object_id());
        if (OB_SUCCESS != tmp_ret) {
          if (OB_SUCC(ret)) {
            ret = tmp_ret;
          }
          LOG_WARN("failed to remove plan stat", K(obj->get_object_id()), K(tmp_ret));
        }
      }
    }
  }
  return ret;
}

int ObILibCacheNode::get_cache_obj(ObILibCacheCtx &ctx,
                                   ObILibCacheKey *key,
                                   ObILibCacheObject *&obj)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(key)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key));
  } else if (OB_FAIL(inner_get_cache_obj(ctx, key, obj))) {
  } else {
    obj->inc_ref_count();
  }
  return ret;
}

int ObILibCacheNode::add_cache_obj(ObILibCacheCtx &ctx,
                                   ObILibCacheKey *key,
                                   ObILibCacheObject *obj)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(lib_cache_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lib cache is invalid", K(ret));
  } else if (OB_ISNULL(key) || OB_ISNULL(obj)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key), K(obj));
  } else if (OB_FAIL(inner_add_cache_obj(ctx, key, obj))) {
  } else {
    if (OB_FAIL(co_list_.push_back(obj))) {
    }
    if (OB_SUCC(ret)) {
      obj->inc_ref_count();
      obj->set_added_lc(true);
    }
  }
  if (OB_FAIL(ret) && ret != OB_SQL_PC_PLAN_DUPLICATE) {
    is_invalid_ = true;
  }
  return ret;
}

int ObILibCacheNode::attach_cache_obj_owner(ObILibCacheObject *cache_obj)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cache_obj)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null cache object", K(ret));
  } else {
    ObByteLockGuard obj_guard(cache_obj->cache_node_lock_);
    if (OB_ISNULL(cache_obj->cache_node_)) {
      cache_obj->cache_node_ = this;
    } else if (cache_obj->cache_node_ != this) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("cache object already belongs to another node", K(ret),
                KP(cache_obj), KP(this), KP(cache_obj->cache_node_));
    }
  }
  return ret;
}

int ObILibCacheNode::unlink_cache_obj(ObILibCacheObject *cache_obj,
                                      bool &removed,
                                      bool &empty)
{
  int ret = OB_SUCCESS;
  removed = false;
  empty = false;
  if (OB_ISNULL(cache_obj)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null cache object", K(ret));
  } else {
    for (CacheObjList::iterator iter = co_list_.begin();
         !removed && iter != co_list_.end(); ++iter) {
      if (*iter == cache_obj) {
        co_list_.erase(iter);
        removed = true;
      }
    }
    empty = co_list_.empty();
  }
  return ret;
}

int ObILibCacheNode::lock(bool is_rdlock)
{
  int ret = OB_SUCCESS;
  // Keep plan-cache lock contention off the query's long-running path. A lock
  // conflict is handled as a cache miss by the caller.
  if (is_rdlock) {
    if (!rwlock_.try_rdlock()) {
      const int64_t lock_timeout_ts = ObTimeUtility::current_time() + PLAN_CACHE_LOCK_TIMEOUT_US;
      if (OB_FAIL(rwlock_.rdlock(lock_timeout_ts))) {
        ret = OB_PC_LOCK_CONFLICT;
      }
    }
  } else {
    const int64_t lock_timeout_ts = ObTimeUtility::current_time() + PLAN_CACHE_LOCK_TIMEOUT_US;
    if (OB_FAIL(rwlock_.wrlock(lock_timeout_ts))) {
      ret = OB_PC_LOCK_CONFLICT;
    }
  }
  return ret;
}

int ObILibCacheNode::update_node_stat(ObILibCacheCtx &ctx)
{
  int ret = OB_SUCCESS;
  ATOMIC_STORE(&(node_stat_.last_active_timestamp_), ObClockGenerator::getClock());
  ATOMIC_INC(&(node_stat_.execute_count_));
  return ret;
}

int64_t ObILibCacheNode::get_mem_size()
{
  int ret = OB_SUCCESS;
  int64_t total_mem_size = 0;
  TCRLockGuard lock_guard(rwlock_);
  CacheObjList::iterator iter = co_list_.begin();
  for (; OB_SUCC(ret) && iter != co_list_.end(); iter++) {
    ObILibCacheObject *obj = *iter;
    if (OB_ISNULL(obj)) {
      BACKTRACE(ERROR, true, "invalid cache obj");
    } else {
      total_mem_size += obj->get_mem_size();
    }
  }
  total_mem_size += allocator_.total();
  return total_mem_size;
}

int64_t ObILibCacheNode::inc_ref_count()
{
  return ATOMIC_AAF(&ref_count_, 1);
}

int64_t ObILibCacheNode::dec_ref_count()
{
  int ret = OB_SUCCESS;
  int64_t ref_count = ATOMIC_SAF(&ref_count_, 1);
  if (ref_count > 0) {
    // do nothing
  } else if (0 == ref_count) {
    if (OB_ISNULL(lib_cache_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid null lib cache");
    } else {
      ObLCNodeFactory &ln_factory = lib_cache_->get_cache_node_factory();
      ln_factory.destroy_cache_node(this);
    }
  } else {
    LOG_ERROR("invalid pcv_set ref count", K(ref_count));
  }
  return ref_count;
}

int ObILibCacheNode::before_cache_evicted()
{
  int ret = OB_SUCCESS;
  return ret;
}

} // namespace common
} // namespace oceanbase
