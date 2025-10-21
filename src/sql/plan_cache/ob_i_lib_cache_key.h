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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_I_LIB_CACHE_KEY_
#define OCEANBASE_SQL_PLAN_CACHE_OB_I_LIB_CACHE_KEY_

#include "sql/plan_cache/ob_lib_cache_register.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace sql
{
// The abstract interface class of library cache key, each object in the ObLibCacheNameSpace
// enum structure needs to inherit from this interface and implement its own implementation class
struct ObILibCacheKey
{
public:
  ObILibCacheKey() : namespace_(NS_INVALID)
  {
  }
  ObILibCacheKey(ObLibCacheNameSpace ns)
    : namespace_(ns)
  {
  }
  virtual ~ObILibCacheKey() {}
  virtual int deep_copy(common::ObIAllocator &allocator, const ObILibCacheKey &other) = 0;
  virtual uint64_t hash() const = 0;
  virtual bool is_equal(const ObILibCacheKey &other) const = 0;
  virtual bool operator==(const ObILibCacheKey &other) const
  {
    bool bool_ret = false;
    if (namespace_ != other.namespace_) {
      bool_ret = false;
    } else {
      bool_ret = is_equal(other);
    }
    return bool_ret;
  }
  virtual int hash(uint64_t &hash_val) const
  {
    hash_val = hash();
    return OB_SUCCESS;
  }
  VIRTUAL_TO_STRING_KV(K_(namespace));

  ObLibCacheNameSpace namespace_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SQL_PLAN_CACHE_OB_I_LIB_CACHE_KEY_
