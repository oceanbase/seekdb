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

#include "ob_cache_object_factory.h"
#include "share/rc/ob_module_provider.h"
#include "sql/ob_sql.h"

namespace oceanbase
{
using namespace common;
using namespace pl;
using namespace lib;
namespace sql
{

int ObCacheObjectFactory::alloc(ObCacheObjGuard& guard, ObLibCacheNameSpace ns)
{
  int ret = OB_SUCCESS;
  MOD_SCOPE {
    ObPlanCache *lib_cache = share::g_mp->plan_cache();
    if (OB_ISNULL(lib_cache)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null plan cache", K(ret));
    } else if (OB_FAIL(lib_cache->alloc_cache_obj(guard, ns))) {
      LOG_WARN("failed to alloc cache obj", K(ret), K(ns));
    }
  }
  return ret;
}

int ObCacheObjectFactory::alloc(ObPlanCache *plan_cache,
                                ObCacheObjGuard& guard,
                                ObLibCacheNameSpace ns)
{
  int ret = OB_SUCCESS;
  MOD_SCOPE {
    if (OB_ISNULL(plan_cache)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null plan cache", K(ret));
    } else if (OB_FAIL(plan_cache->alloc_cache_obj(guard, ns))) {
      LOG_WARN("failed to alloc cache obj", K(ret), K(ns));
    }
  }
  return ret;
}

void ObCacheObjectFactory::inner_free(ObILibCacheObject *&cache_obj,
                                      const CacheRefHandleID ref_handle)
{
  int ret = OB_SUCCESS;
  
  MOD_SCOPE {
    ObPlanCache *lib_cache = OB_NOT_NULL(cache_obj) ? cache_obj->get_lib_cache() : NULL;
    if (OB_ISNULL(lib_cache) && OB_NOT_NULL(share::g_mp)) {
      lib_cache = share::g_mp->plan_cache();
    }
    if (OB_ISNULL(lib_cache)) {
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "invalid null plan cache");
    } else {
      lib_cache->free_cache_obj(cache_obj, ref_handle);
    }
  }
}

void ObCacheObjectFactory::inner_free(ObPlanCache *pc,
                                      ObILibCacheObject *&cache_obj,
                                      const CacheRefHandleID ref_handle)
{
  ObPlanCache *owner_cache = OB_NOT_NULL(cache_obj) ? cache_obj->get_lib_cache() : NULL;
  if (OB_ISNULL(owner_cache)) {
    owner_cache = pc;
  }
  if (OB_ISNULL(owner_cache) && OB_NOT_NULL(share::g_mp)) {
    owner_cache = share::g_mp->plan_cache();
  }
  if (OB_ISNULL(owner_cache)) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid null plan cache");
  } else {
    if (OB_NOT_NULL(pc) && pc != owner_cache) {
      LOG_DEBUG("ignore non-owner plan cache when freeing cache object",
                KP(pc), KP(owner_cache), KPC(cache_obj));
    }
    owner_cache->free_cache_obj(cache_obj, ref_handle);
  }
}

int ObCacheObjectFactory::destroy_cache_obj(const bool is_leaked,
                                            const uint64_t obj_id,
                                            ObPlanCache *lib_cache)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(lib_cache)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null plan cache", K(ret));
  } else if (OB_FAIL(lib_cache->destroy_cache_obj(is_leaked, obj_id))) {
    LOG_WARN("failed to destory cache obj", K(ret), K(is_leaked), K(obj_id));
  }
  return ret;
}

int ObCacheObjGuard::force_early_release(ObPlanCache *plan_cache)
{
  int ret = OB_SUCCESS;
  ObPlanCache *owner_cache = owner_cache_;
  if (OB_ISNULL(owner_cache) && OB_NOT_NULL(cache_obj_)) {
    owner_cache = cache_obj_->get_lib_cache();
  }
  if (OB_ISNULL(owner_cache)) {
    owner_cache = plan_cache;
  }
  if (OB_ISNULL(cache_obj_)) {
    // do nothing
  } else if (OB_ISNULL(owner_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("is null", K(ret));
  } else {
    if (OB_NOT_NULL(plan_cache) && plan_cache != owner_cache) {
      LOG_DEBUG("ignore non-owner plan cache when releasing cache object",
                KP(plan_cache), KP(owner_cache), KPC(cache_obj_));
    }
    ObCacheObjectFactory::free(owner_cache, cache_obj_, ref_handle_);
    cache_obj_ = NULL;
    owner_cache_ = NULL;
  }
  return ret;
}

} //end namespace sql
} //namespace oceanbase
