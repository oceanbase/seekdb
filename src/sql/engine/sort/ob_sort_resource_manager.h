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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_RESOURCE_MANAGER_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_RESOURCE_MANAGER_H_

#include <algorithm>
#include <functional>
#include "sql/engine/ob_sql_mem_mgr_processor.h"
#include "sql/engine/basic/ob_temp_block_store.h"

namespace oceanbase
{
namespace sql
{

class ObSortResourceManager
{
public:
  static int64_t get_max_merge_ways() { return 256; }
  static int64_t get_min_merge_buffer_size() { return 16L * 1024L * 1024L; }
  static int64_t get_ddl_sort_min_cache_size() { return 256L * 1024L * 1024L; }

  static int64_t calc_initial_cache_size(const int64_t input_rows,
                                         const int64_t input_width,
                                         const bool is_ddl)
  {
    const int64_t row_cnt = OB_INVALID_ID == input_rows ? 0 : input_rows;
    const int64_t row_width = OB_INVALID_ID == input_width ? 0 : input_width;
    int64_t cache_size = row_cnt > 0 && row_width > 0 ? row_cnt * row_width : 0;
    if (is_ddl) {
      cache_size = std::max(cache_size, get_ddl_sort_min_cache_size());
    }
    return cache_size;
  }

  static int update_max_available_mem_size_periodically(ObSqlMemMgrProcessor &sql_mem_processor,
                                                        lib::MemoryContext mem_context,
                                                        const int64_t row_count,
                                                        bool &updated)
  {
    return sql_mem_processor.update_max_available_mem_size_periodically(
      &mem_context->get_malloc_allocator(),
      [&](int64_t cur_cnt) { return row_count > cur_cnt; },
      updated);
  }

  static int preprocess_dump(ObSqlMemMgrProcessor &sql_mem_processor,
                             ObSqlWorkAreaProfile &profile,
                             lib::MemoryContext mem_context,
                             const int64_t total_used_size,
                             const int64_t data_size,
                             const std::function<bool()> &need_dump_func,
                             bool &dumped)
  {
    int ret = OB_SUCCESS;
    dumped = false;
    if (OB_FAIL(sql_mem_processor.get_max_available_mem_size(&mem_context->get_malloc_allocator()))) {
      SQL_ENG_LOG(WARN, "failed to get max available memory size", K(ret));
    } else if (OB_FAIL(sql_mem_processor.update_used_mem_size(total_used_size))) {
      SQL_ENG_LOG(WARN, "failed to update used memory size", K(ret));
    } else {
      dumped = need_dump_func();
      if (dumped) {
        if (!sql_mem_processor.is_auto_mgr() || profile.get_cache_size() < profile.get_global_bound_size()) {
          if (OB_FAIL(sql_mem_processor.extend_max_memory_size(&mem_context->get_malloc_allocator(),
                  [&](int64_t max_memory_size) {
                    UNUSED(max_memory_size);
                    return need_dump_func();
                  },
                  dumped,
                  total_used_size))) {
            SQL_ENG_LOG(WARN, "failed to extend sort memory", K(ret));
          }
        } else if (profile.get_cache_size() <= data_size) {
          if (OB_FAIL(sql_mem_processor.update_cache_size(
                  &mem_context->get_malloc_allocator(), profile.get_cache_size() * 2))) {
            SQL_ENG_LOG(WARN, "failed to update sort cache size", K(ret), K(profile.get_cache_size()));
          } else {
            dumped = need_dump_func();
          }
        }
      }
    }
    return ret;
  }

  static int calc_merge_ways(ObSqlMemMgrProcessor &sql_mem_processor,
                             lib::MemoryContext mem_context,
                             const int64_t max_ways,
                             int64_t &merge_ways)
  {
    int ret = OB_SUCCESS;
    merge_ways = 0;
    if (OB_UNLIKELY(max_ways < 2)) {
      ret = OB_INVALID_ARGUMENT;
      SQL_ENG_LOG(WARN, "invalid max merge ways", K(ret), K(max_ways));
    } else if (OB_FAIL(sql_mem_processor.get_max_available_mem_size(
                 &mem_context->get_malloc_allocator()))) {
      SQL_ENG_LOG(WARN, "failed to get max available memory size", K(ret));
    } else {
      const int64_t min_ways = std::max(static_cast<int64_t>(2),
                                        get_min_merge_buffer_size() / ObTempBlockStore::BLOCK_SIZE);
      merge_ways = std::max(min_ways, sql_mem_processor.get_mem_bound() / ObTempBlockStore::BLOCK_SIZE);
      if (merge_ways < max_ways) {
        bool dumped = false;
        const int64_t need_size = max_ways * ObTempBlockStore::BLOCK_SIZE;
        if (OB_FAIL(sql_mem_processor.extend_max_memory_size(
              &mem_context->get_malloc_allocator(),
              [&](int64_t max_memory_size) { return max_memory_size < need_size; },
              dumped,
              mem_context->used()))) {
          SQL_ENG_LOG(WARN, "failed to extend merge memory", K(ret));
        } else {
          merge_ways = std::max(merge_ways,
                                sql_mem_processor.get_mem_bound() / ObTempBlockStore::BLOCK_SIZE);
        }
      }
      merge_ways = std::min(merge_ways, max_ways);
    }
    return ret;
  }
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_RESOURCE_MANAGER_H_ */
