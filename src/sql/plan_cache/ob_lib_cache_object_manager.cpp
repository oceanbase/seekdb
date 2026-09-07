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
#include "ob_lib_cache_object_manager.h"
#include "sql/plan_cache/ob_plan_cache.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace sql
{

struct ObDestroyCacheObjPred
{
  explicit ObDestroyCacheObjPred(const bool is_leaked)
    : is_leaked_(is_leaked)
  {}

  bool operator()(
      common::hash::HashMapPair<ObCacheObjID, ObILibCacheObject *> &entry) const
  {
    return OB_NOT_NULL(entry.second)
        && (is_leaked_ ? !entry.second->is_sql_crsr()
                       : 0 == entry.second->get_ref_count());
  }

  bool is_leaked_;
};

int ObLCObjectManager::init(int64_t hash_bucket, ObPlanCache *lib_cache)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(lib_cache)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid plan cache", K(ret));
  } else if (FALSE_IT(lib_cache_ = lib_cache)) {
  } else if (OB_FAIL(cache_obj_map_.create(hash::cal_next_prime(hash_bucket),
                                    ObModIds::OB_HASH_BUCKET_LC_STAT,
                                    ObModIds::OB_HASH_NODE_LC_STAT))) {
  } else if (OB_FAIL(alloc_cache_obj_map_.create(hash::cal_next_prime(hash_bucket),
                                                 ObModIds::OB_HASH_BUCKET_LC_STAT,
                                                 ObModIds::OB_HASH_NODE_LC_STAT))) {
  }
  return ret;
}

int ObLCObjectManager::alloc(ObCacheObjGuard& guard,
                             ObLibCacheNameSpace ns,
                             MemoryContext &parent_context)
{
  int ret = OB_SUCCESS;
  lib::MemoryContext entity = NULL;
  ObMemAttr mem_attr;
  ObILibCacheObject *cache_obj = NULL;
  
  mem_attr.ctx_id_ = ObCtxIds::PLAN_CACHE_CTX_ID;
  if (ns <= NS_INVALID || ns >= NS_MAX || OB_ISNULL(LC_CO_ALLOC[ns])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("out of the max type", K(ret), K(ns));
  } else if (FALSE_IT(mem_attr.label_ = LC_NS_TYPE_LABELS[ns])) {
  } else if (OB_FAIL(parent_context->CREATE_CONTEXT(entity,
                     lib::ContextParam().set_mem_attr(mem_attr)))) {
  } else if (OB_ISNULL(entity)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL memory entity", K(ret));
  } else {
    WITH_CONTEXT(entity) {
      if (OB_FAIL(LC_CO_ALLOC[ns](entity, cache_obj))) {
      } else {
        uint64_t obj_id = allocate_object_id();
        cache_obj->object_id_ = obj_id;
        if (OB_FAIL(alloc_cache_obj_map_.set_refactored(obj_id, cache_obj))) {
          LOG_WARN("failed to add element to hashmap", K(ret));
          inner_free(cache_obj);
          entity = NULL;
          cache_obj = NULL;
        }
      }
    }
  }
  if (OB_FAIL(ret) && NULL != entity) {
    DESTROY_CONTEXT(entity);
    entity = NULL;
  }
  guard.cache_obj_ = cache_obj;
  return ret;
}

int ObLCObjectManager::add_cache_obj(ObILibCacheObject *cache_obj)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cache_obj)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(cache_obj), K(ret));
  } else if (OB_FAIL(cache_obj_map_.set_refactored(cache_obj->get_object_id(), cache_obj))) {
  }
  return ret;
}

int ObLCObjectManager::erase_cache_obj(ObCacheObjID id)
{
  int ret = OB_SUCCESS;
  ObILibCacheObject *cache_obj = NULL;
  if (OB_FAIL(cache_obj_map_.erase_refactored(id, &cache_obj))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    }
  } else {
    if (NULL != cache_obj) {
      // set logical deleted time
      cache_obj->set_logical_del_time(common::ObTimeUtility::current_monotonic_time());
      LOG_DEBUG("set logical del time", K(cache_obj->get_logical_del_time()),
                                        K(cache_obj->get_object_id()),
                                        K(cache_obj->added_lc()),
                                        K(cache_obj));
      // The SQL-plan object-id map is a weak diagnostic index.  Other
      // library-cache namespaces retain their original owning index ref.
      if (!cache_obj->is_sql_crsr()) {
        common_free(cache_obj);
      }
      cache_obj = NULL;
    }
  }
  return ret;
}

void ObLCObjectManager::common_free(ObILibCacheObject *cache_obj)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cache_obj)) {
    // do nothing
  } else if (!cache_obj->is_sql_crsr()) {
    // Keep the legacy PL/package lifecycle unchanged. In particular, refresh
    // the safe-timestamp anchor on every release after logical eviction, and
    // do not destroy the object when its eviction callback fails.
    if (!cache_obj->added_lc()) {
      cache_obj->set_logical_del_time(ObTimeUtility::current_monotonic_time());
      LOG_WARN("set logical del time", K(cache_obj->get_logical_del_time()),
                                       K(cache_obj->added_lc()),
                                       K(cache_obj->get_object_id()),
                                       K(lbt()));
    }
    const int64_t ref_count = cache_obj->dec_ref_count();
    if (ref_count > 0) {
      // do nothing
    } else if (0 == ref_count) {
      if (OB_FAIL(cache_obj->before_cache_evicted())) {
      } else if (OB_FAIL(destroy_cache_obj(false, cache_obj->get_object_id()))) {
      }
    } else {
      LOG_ERROR("invalid cache obj ref count", K(ref_count), KP(cache_obj));
    }
  } else {
    ObILibCacheNode *cache_node = nullptr;
    int64_t ref_count = 0;
    if (!cache_obj->added_lc()
        && INT64_MAX == cache_obj->get_logical_del_time()) {
      cache_obj->set_logical_del_time(ObTimeUtility::current_monotonic_time());
      LOG_TRACE("set logical del time for uncached SQL plan",
                K(cache_obj->get_logical_del_time()),
                K(cache_obj->get_object_id()));
    }

    // Ref counts above two cannot be the transition to the sole node
    // membership reference, so release them without serializing every hot
    // plan-guard release on cache_node_lock_.
    ref_count = ATOMIC_LOAD(&cache_obj->ref_count_);
    while (ref_count > 2
           && !ATOMIC_BCAS(&cache_obj->ref_count_, ref_count, ref_count - 1)) {
      ref_count = ATOMIC_LOAD(&cache_obj->ref_count_);
    }
    if (ref_count > 2) {
      --ref_count;
    } else {
      // A SQL cache node keeps exactly one membership reference. Serialize
      // the transition to that last reference with owner detachment, and
      // pin the node before dropping the object lock.
      ObByteLockGuard lock_guard(cache_obj->cache_node_lock_);
      ref_count = cache_obj->dec_ref_count();
      if (1 == ref_count && OB_NOT_NULL(cache_obj->cache_node_)) {
        cache_node = cache_obj->cache_node_;
        cache_node->inc_ref_count();
      }
    }

    if (OB_NOT_NULL(cache_node)) {
      if (OB_ISNULL(lib_cache_)) {
        LOG_ERROR_RET(OB_ERR_UNEXPECTED, "invalid null lib cache");
      } else if (OB_FAIL(lib_cache_->try_remove_unused_sql_plan(cache_node, cache_obj))) {
        LOG_ERROR("failed to retire unused SQL plan", K(ret), KP(cache_node), KP(cache_obj));
      }
      cache_node->dec_ref_count();
    } else if (ref_count > 0) {
      // do nothing
    } else if (ref_count == 0) {
      // cache_obj_map_ is a weak diagnostic index.  Node retirement normally
      // erased it before dropping the membership reference; erase again here
      // so no error path can leave a dangling object-id entry behind.
      const int erase_ret = erase_cache_obj(cache_obj->get_object_id());
      if (OB_SUCCESS != erase_ret) {
        LOG_ERROR("failed to erase cache object id before destruction",
                  K(erase_ret), KP(cache_obj));
        // Keep the zero-ref object in alloc_cache_obj_map_: leaking is safer
        // than destroying it while a weak object-id entry may still point at
        // the allocation.
      } else {
        if (OB_FAIL(cache_obj->before_cache_evicted())) {
          LOG_WARN("failed to process before cache object eviction", K(ret), KP(cache_obj));
        }
        // Reaching zero is terminal.  A callback failure must not leave an
        // object at zero in alloc_cache_obj_map_ forever.
        if (OB_FAIL(destroy_cache_obj(false, cache_obj->get_object_id()))) {
        }
      }
    } else {
      LOG_ERROR("invalid cache obj ref count", K(ref_count), KP(cache_obj));
    }
  }
}

int ObLCObjectManager::destroy_cache_obj(const bool is_leaked,
                                         const uint64_t object_id)
{
  int ret = OB_SUCCESS;
  ObILibCacheObject *to_del_obj = nullptr;
  bool is_erased = false;
  ObDestroyCacheObjPred destroy_pred(is_leaked);
  ret = alloc_cache_obj_map_.erase_if(
      object_id, destroy_pred, is_erased, &to_del_obj);
  if (OB_HASH_NOT_EXIST == ret) {
    ret = OB_SUCCESS;
  } else if (OB_SUCCESS != ret) {
    LOG_WARN("failed to erase element from alloc obj list", K(object_id), K(ret));
  } else if (!is_erased) {
    if (is_leaked) {
      // SQL cursors are governed solely by their reference count.  The
      // safe-timestamp leak heuristic must not invalidate a live plan guard.
      LOG_DEBUG("skip forced destruction of referenced SQL plan", K(object_id));
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("cache object destruction requires zero references",
                K(ret), K(object_id));
    }
  } else if (OB_ISNULL(to_del_obj)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid cache obj", K(to_del_obj));
  } else {
    // Preserve the legacy forced-cleanup behavior for PL and other non-SQL
    // namespaces.  Only SQL cursors opt out of is_leaked destruction.
    to_del_obj->dump_deleted_log_info(!is_leaked);
    inner_free(to_del_obj);
  }
  return ret;
}

void ObLCObjectManager::inner_free(ObILibCacheObject *cache_obj)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(lib_cache_)) {
    lib_cache_->release_cache_object(*cache_obj);
  }
  lib::MemoryContext entity = cache_obj->get_mem_context();
  WITH_CONTEXT(entity) { cache_obj->~ObILibCacheObject(); }
  cache_obj = NULL;
  DESTROY_CONTEXT(entity);
}

} // namespace common
} // namespace oceanbase
