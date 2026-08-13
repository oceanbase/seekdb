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

#ifdef LIB_CACHE_OBJ_DEF
#include "share/ob_lib_cache_namespace.def"
#endif /*LIB_CACHE_OBJ_DEF*/

#ifndef OCEANBASE_QUERY_PLAN_CACHE_OB_LIB_CACHE_REGISTER_
#define OCEANBASE_QUERY_PLAN_CACHE_OB_LIB_CACHE_REGISTER_

#include "lib/utility/ob_mod_define.h"
#include "lib/alloc/alloc_struct.h"
#include "share/ob_lib_cache_namespace.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace lib
{
class MemoryContext;
}

namespace sql
{
class ObILibCacheObject;
struct ObILibCacheKey;
class ObILibCacheNode;
class ObPlanCache;


typedef int (*CNAllocFunc) (lib::MemoryContext &mem_ctx,
                            ObILibCacheNode  *&cache_node,
                            ObPlanCache *lib_cahe);
typedef int (*COAllocFunc) (lib::MemoryContext &mem_ctx,
                            ObILibCacheObject *&cache_obj);
typedef int (*CKAllocFunc) (ObIAllocator &allocator,
                            ObILibCacheKey *&cache_key);

class ObLibCacheRegister
{
public:
  static void register_cache_objs();
  static void register_lc_key();
  static void register_lc_obj();
  static void register_lc_node();
  static ObLibCacheNameSpace get_ns_type_by_name(const ObString &name);

public:
  static CKAllocFunc CK_ALLOC[NS_MAX];
  static COAllocFunc CO_ALLOC[NS_MAX];
  static CNAllocFunc CN_ALLOC[NS_MAX];
  static const char *NAME_TYPES[NS_MAX];
  static lib::ObLabel NS_TYPE_LABELS[NS_MAX];
};

#define LC_CO_ALLOC (ObLibCacheRegister::CO_ALLOC)
#define LC_CN_ALLOC (ObLibCacheRegister::CN_ALLOC)
#define LC_CK_ALLOC (ObLibCacheRegister::CK_ALLOC)
#define LC_NS_TYPE_LABELS (ObLibCacheRegister::NS_TYPE_LABELS)

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_QUERY_PLAN_CACHE_OB_LIB_CACHE_REGISTER_
