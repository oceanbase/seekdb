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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_I_LIB_CACHE_CONTEXT_
#define OCEANBASE_SQL_PLAN_CACHE_OB_I_LIB_CACHE_CONTEXT_

#include "sql/plan_cache/ob_lib_cache_register.h"

namespace oceanbase
{
namespace sql
{
class ObILibCacheKey;

// Each object in the ObLibCacheNameSpace enumeration structure needs to inherit ObILibCacheCtx
// to expand its own context
struct ObILibCacheCtx
{
public:
  ObILibCacheCtx()
    : key_(NULL),
    need_destroy_node_(false)
  {
  }
  virtual ~ObILibCacheCtx() {}
  VIRTUAL_TO_STRING_KV(KP_(key));

  ObILibCacheKey *key_;
  bool need_destroy_node_; // Indicate whether the cache node corresponding to key_ in lib cache is invalid
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SQL_PLAN_CACHE_OB_I_LIB_CACHE_CONTEXT_
