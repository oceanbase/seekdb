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

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_WORKAREA_MEMORY_LIMIT_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_WORKAREA_MEMORY_LIMIT_H_

#include <cstdint>
#include <limits>

namespace oceanbase
{
namespace sql
{

static constexpr int64_t UNLIMITED_WORKAREA_MEMORY =
    std::numeric_limits<int64_t>::max();

// A zero per-store limit inherits 80% of the process SQL work-area quota so
// that spill can make progress before the strict admission limit is reached.
// A positive value, including UNLIMITED_WORKAREA_MEMORY, is explicit.
int64_t calculate_workarea_memory_limit(int64_t configured_limit,
                                        int64_t manager_limit);
int64_t effective_workarea_memory_limit(int64_t configured_limit);

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_BASIC_OB_WORKAREA_MEMORY_LIMIT_H_
