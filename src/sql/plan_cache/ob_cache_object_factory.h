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

#ifndef DEV_SRC_SQL_PLAN_CACHE_OB_CACHE_OBJECT_FACTORY_H_
#define DEV_SRC_SQL_PLAN_CACHE_OB_CACHE_OBJECT_FACTORY_H_
#include "sql/plan_cache/ob_cache_object.h"
#include "lib/allocator/page_arena.h"
#include "lib/objectpool/ob_global_factory.h"
#include "lib/objectpool/ob_tc_factory.h"

namespace oceanbase
{
namespace pl

{
class ObPLFunction;
class ObPLPackage;
}  // namespace pl
namespace sql
{
class ObPhysicalPlan;
class ObPlanCache;
class ObCacheObjGuard;
class ObSql;
class ObCacheObjectFactory
{
friend class ObPlanCacheObject;
friend class ObPlanCache;
public:
  static int alloc(ObCacheObjGuard& guard,
                   ObLibCacheNameSpace ns);
  static int alloc(ObPlanCache *plan_cache,
                   ObCacheObjGuard& guard,
                   ObLibCacheNameSpace ns);
  static void inner_free(ObILibCacheObject *&cache_obj,
                         const CacheRefHandleID ref_handle);
  static void inner_free(ObPlanCache *pc,
                         ObILibCacheObject *&cache_obj,
                         const CacheRefHandleID ref_handle);
  template<typename ClassT>
  static void free(ClassT *&cache_obj, const CacheRefHandleID ref_handle)
  {
    ObILibCacheObject *tmp_obj = (ObILibCacheObject *)cache_obj;
    inner_free(tmp_obj, ref_handle);
    cache_obj = NULL;
  }
  template<typename ClassT>
  static void free(ObPlanCache *pc, ClassT *&cache_obj, const CacheRefHandleID ref_handle)
  {
    ObILibCacheObject *tmp_obj = (ObILibCacheObject *)cache_obj;
    inner_free(pc, tmp_obj, ref_handle);
    cache_obj = NULL;
  }

private:
  static int destroy_cache_obj(const bool is_leaked,
                               const uint64_t obj_id,
                               ObPlanCache *lib_cache);
};


class ObCacheObjGuard {
friend class ObPlanCache;
friend class ObLCObjectManager;
private:
  // access only
  ObILibCacheObject* cache_obj_;
  // readable and writable
  CacheRefHandleID ref_handle_;
  // The cache which owns cache_obj_. It must be used for dereference because
  // SQL plans can be owned by a session cache instead of the tenant cache.
  ObPlanCache *owner_cache_;

public:
  ObCacheObjGuard()
    : cache_obj_(NULL),
    ref_handle_(MAX_HANDLE),
    owner_cache_(NULL)
  {
  }
  ObCacheObjGuard(CacheRefHandleID ref_handle)
    : cache_obj_(NULL),
    ref_handle_(ref_handle),
    owner_cache_(NULL)
  {
  }

  ~ObCacheObjGuard()
  {
    if (OB_ISNULL(cache_obj_)) {
      // do nothing
    } else {
      ObPlanCache *owner_cache = OB_NOT_NULL(owner_cache_)
          ? owner_cache_ : cache_obj_->get_lib_cache();
      ObCacheObjectFactory::free(owner_cache, cache_obj_, ref_handle_);
      cache_obj_ = NULL;
      owner_cache_ = NULL;
    }
  }

  void init(CacheRefHandleID ref_handle)
  {
    ref_handle_ = ref_handle;
  }

  ObILibCacheObject* get_cache_obj() const
  {
    return cache_obj_;
  }

  CacheRefHandleID get_ref_handle() const
  {
    return ref_handle_;
  }

  ObPlanCache *get_owner_cache() const
  {
    return owner_cache_;
  }

  int force_early_release(ObPlanCache *pc);

  // this function may be somewhat dangerous and may cause some memory leak.
  // Before use this function, PLEASE CONCAT @Shengle or @Juehui
  //
  // Why we provide swap, rather than assign?
  // We assume 'other' may be another stack variable, and it may be used by others
  // and therefore we cannot directly deconstruct it in this function. However, swap
  // need not destroy this variable in this function.
  //
  // Which scenario can use this function?
  // Change life cycle of current guard.
  void swap(ObCacheObjGuard& other)
  {
    ObCacheObjGuard tmp(MAX_HANDLE);

    tmp.cache_obj_ = this->cache_obj_;
    tmp.ref_handle_ = this->ref_handle_;
    tmp.owner_cache_ = this->owner_cache_;

    this->cache_obj_ = other.cache_obj_;
    this->ref_handle_ = other.ref_handle_;
    this->owner_cache_ = other.owner_cache_;

    other.cache_obj_ = tmp.cache_obj_;
    other.ref_handle_ = tmp.ref_handle_;
    other.owner_cache_ = tmp.owner_cache_;

    // If not reset tmp in this line, the reference count of current cache_obj_
    //  will be mistakenly decrease.
    tmp.reset();
  }
  TO_STRING_KV(K_(cache_obj));
private:
  void reset(){
    cache_obj_ = NULL;
    ref_handle_ = MAX_HANDLE;
    owner_cache_ = NULL;
  }
};

  }  // namespace sql
} //namespace oceanbase
#endif /* DEV_SRC_SQL_PLAN_CACHE_OB_CACHE_OBJECT_FACTORY_H_ */
