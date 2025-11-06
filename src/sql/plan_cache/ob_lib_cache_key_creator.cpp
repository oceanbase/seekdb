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
#include "sql/plan_cache/ob_lib_cache_key_creator.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace sql
{

int OBLCKeyCreator::create_cache_key(ObLibCacheNameSpace ns,
                                     common::ObIAllocator &allocator,
                                     ObILibCacheKey*& key)
{
  int ret = OB_SUCCESS;
  if (ns <= NS_INVALID || ns >= NS_MAX || OB_ISNULL(LC_CK_ALLOC[ns])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("out of the max type", K(ret), K(ns));
  } else if (OB_FAIL(LC_CK_ALLOC[ns](allocator, key))) {
    LOG_WARN("failed to create lib cache node", K(ret), K(ns));
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
