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

#include "data_plane/access/ob_parallel_range_task_planner.h"

#include <algorithm>

#include "lib/ob_errno.h"

namespace oceanbase
{
namespace data_plane
{

int ObParallelRangeTaskPlanner::compute_total_task_count(
    const ObParallelRangeTaskParams &params,
    const int64_t total_size_kb,
    int64_t &total_task_count)
{
  int ret = common::OB_SUCCESS;
  int64_t result = -1;
  if (params.min_task_count_per_thread_ <= 0
      || params.max_task_count_per_thread_ <= 0
      || params.min_task_access_size_kb_ <= 0
      || params.parallelism_ <= 0
      || params.expected_task_load_kb_ <= 0) {
    ret = common::OB_ERR_UNEXPECTED;
  } else {
    const int64_t expected_task_load_kb =
        std::max(params.expected_task_load_kb_, params.min_task_access_size_kb_);
    const int64_t lower_bound_size_kb = params.parallelism_
        * expected_task_load_kb * params.min_task_count_per_thread_;
    const int64_t upper_bound_size_kb = params.parallelism_
        * expected_task_load_kb * params.max_task_count_per_thread_;

    if (total_size_kb < 0 || lower_bound_size_kb < 0 || upper_bound_size_kb < 0) {
      ret = common::OB_ERR_UNEXPECTED;
    } else if (total_size_kb < lower_bound_size_kb) {
      result = std::min(params.min_task_count_per_thread_ * params.parallelism_,
                        total_size_kb / params.min_task_access_size_kb_);
      result = std::max(result, total_size_kb / expected_task_load_kb);
    } else if (total_size_kb > upper_bound_size_kb) {
      result = params.max_task_count_per_thread_ * params.parallelism_;
    } else {
      result = total_size_kb / expected_task_load_kb;
    }
  }

  if (common::OB_SUCCESS == ret) {
    total_task_count = result;
  }
  return ret;
}

} // namespace data_plane
} // namespace oceanbase
