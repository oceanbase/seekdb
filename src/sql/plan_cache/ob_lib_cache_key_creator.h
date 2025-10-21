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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_LIB_CACHE_KEY_CREATOR_
#define OCEANBASE_SQL_PLAN_CACHE_OB_LIB_CACHE_KEY_CREATOR_

#include "sql/plan_cache/ob_lib_cache_register.h"

namespace oceanbase
{
namespace sql
{
class ObILibCacheKey;

class OBLCKeyCreator
{
public:
  static int create_cache_key(ObLibCacheNameSpace ns,
                              common::ObIAllocator &allocator,
                              ObILibCacheKey*& key);
  template<typename ClassT>
  static int create(common::ObIAllocator &allocator, ObILibCacheKey*& key)
  {
    int ret = OB_SUCCESS;
    char *ptr = NULL;
    if (NULL == (ptr = (char *)allocator.alloc(sizeof(ClassT)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OB_LOG(WARN, "failed to allocate memory for lib cache key", K(ret));
    } else {
      key = new(ptr)ClassT();
    }
    return ret;
  }
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SQL_PLAN_CACHE_OB_LIB_CACHE_KEY_CREATOR_
