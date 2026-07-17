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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_MEMORY_POLICY_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_MEMORY_POLICY_H_

#include "sql/engine/ob_sql_mem_mgr_processor.h"

namespace oceanbase
{
namespace sql
{

class ObSortMemoryPolicy
{
public:
  static int calc_adaptive_merge_ways(ObSqlMemMgrProcessor &sql_mem_processor,
                                      common::ObIAllocator &allocator,
                                      const int64_t mem_used,
                                      const int64_t block_size,
                                      const int64_t max_ways,
                                      int64_t &merge_ways)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(block_size <= 0 || max_ways <= 0)) {
      ret = OB_INVALID_ARGUMENT;
      SQL_ENG_LOG(WARN, "invalid sort memory policy argument", K(ret), K(block_size), K(max_ways));
    } else {
      merge_ways = sql_mem_processor.get_mem_bound() / block_size;
      merge_ways = std::max(static_cast<int64_t>(2), merge_ways);
      if (merge_ways < max_ways) {
        bool dumped = false;
        const int64_t need_size = max_ways * block_size;
        if (OB_FAIL(sql_mem_processor.extend_max_memory_size(
              &allocator,
              [&](int64_t max_memory_size) { return max_memory_size < need_size; },
              dumped,
              mem_used))) {
          SQL_ENG_LOG(WARN, "failed to extend memory size", K(ret));
        }
        merge_ways = std::max(merge_ways, sql_mem_processor.get_mem_bound() / block_size);
      }
      merge_ways = std::min(merge_ways, max_ways);
    }
    return ret;
  }

private:
  ObSortMemoryPolicy() = delete;
  ~ObSortMemoryPolicy() = delete;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_SORT_OB_SORT_MEMORY_POLICY_H_
