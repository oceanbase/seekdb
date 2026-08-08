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
#include "sql/plan_cache/ob_plan_cache.h"

namespace oceanbase
{
using namespace common;
using namespace pl;
using namespace lib;
namespace sql
{

int ObCacheObjectFactory::alloc(ObPlanCache &plan_cache,
                                ObCacheObjGuard& guard,
                                ObLibCacheNameSpace ns)
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    if (OB_FAIL(plan_cache.alloc_cache_obj(guard, ns))) {
    }
  }
  return ret;
}

void ObCacheObjectFactory::inner_free(ObILibCacheObject *&cache_obj)
{
  int ret = OB_SUCCESS;

  SERVER_MODULE_SCOPE {
    ObPlanCache *lib_cache = OB_ISNULL(cache_obj) ? nullptr : cache_obj->get_plan_cache();
    if (OB_ISNULL(lib_cache)) {
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "invalid null plan cache");
    } else {
      lib_cache->free_cache_obj(cache_obj);
    }
  }
}

void ObCacheObjectFactory::inner_free(ObPlanCache *pc,
                                      ObILibCacheObject *&cache_obj)
{
  if (OB_ISNULL(pc)) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid null plan cache");
  } else {
    pc->free_cache_obj(cache_obj);
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
  }
  return ret;
}

int ObCacheObjGuard::force_early_release(ObPlanCache *plan_cache)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cache_obj_)) {
    // do nothing
  } else if (OB_ISNULL(plan_cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("is null", K(ret));
  } else {
    ObCacheObjectFactory::free(plan_cache, cache_obj_);
    cache_obj_ = NULL;
  }
  return ret;
}

} //end namespace sql
} //namespace oceanbase
