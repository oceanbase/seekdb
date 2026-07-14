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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SQL_SORT_RESOURCE_MANAGER_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SQL_SORT_RESOURCE_MANAGER_H_

#include "sql/engine/sort/ob_sort_resource_manager.h"

namespace oceanbase
{
namespace sql
{

class ObSQLSortResourceManager : public ObSortResourceManager
{
public:
  static int64_t calc_sql_initial_cache_size(const int64_t input_rows,
                                             const int64_t input_width,
                                             const bool is_ddl,
                                             const bool is_topn,
                                             const int64_t topn_cnt)
  {
    int64_t cache_size = calc_initial_cache_size(input_rows, input_width, is_ddl);
    if (is_topn && topn_cnt > 0 && topn_cnt != INT64_MAX && input_width > 0) {
      cache_size = std::min(cache_size, topn_cnt * input_width * 2);
      if (is_ddl) {
        cache_size = std::max(cache_size, get_ddl_sort_min_cache_size());
      }
    }
    return cache_size;
  }
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SQL_SORT_RESOURCE_MANAGER_H_ */
