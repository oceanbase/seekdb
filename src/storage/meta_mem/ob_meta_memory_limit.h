/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_STORAGE_META_MEMORY_LIMIT_H_
#define OCEANBASE_STORAGE_META_MEMORY_LIMIT_H_

#include "lib/alloc/alloc_func.h"
#include "lib/utility/ob_mod_define.h"

namespace oceanbase
{
namespace storage
{

inline int set_meta_obj_memory_limit(const int64_t percentage)
{
  const int64_t memory_budget = lib::get_memory_budget();
  const int64_t memory_limit = 0 == percentage
      ? memory_budget
      : memory_budget / 100 * percentage;
  return lib::set_ctx_limit(common::ObCtxIds::META_OBJ_CTX_ID, memory_limit);
}

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_META_MEMORY_LIMIT_H_
