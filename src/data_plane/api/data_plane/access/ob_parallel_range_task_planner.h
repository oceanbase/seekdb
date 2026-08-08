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

#ifndef OCEANBASE_DATA_PLANE_ACCESS_OB_PARALLEL_RANGE_TASK_PLANNER_H_
#define OCEANBASE_DATA_PLANE_ACCESS_OB_PARALLEL_RANGE_TASK_PLANNER_H_

#include <stdint.h>

namespace oceanbase
{
namespace data_plane
{

struct ObParallelRangeTaskParams
{
  static constexpr int64_t DEFAULT_EXPECTED_TASK_LOAD_KB = 102400;
  static constexpr int64_t DEFAULT_MIN_TASK_COUNT_PER_THREAD = 13;
  static constexpr int64_t DEFAULT_MAX_TASK_COUNT_PER_THREAD = 100;

  explicit ObParallelRangeTaskParams(const int64_t min_task_access_size_kb)
      : parallelism_(0),
        expected_task_load_kb_(DEFAULT_EXPECTED_TASK_LOAD_KB),
        min_task_count_per_thread_(DEFAULT_MIN_TASK_COUNT_PER_THREAD),
        max_task_count_per_thread_(DEFAULT_MAX_TASK_COUNT_PER_THREAD),
        min_task_access_size_kb_(min_task_access_size_kb)
  {}

  int64_t parallelism_;
  int64_t expected_task_load_kb_;
  int64_t min_task_count_per_thread_;
  int64_t max_task_count_per_thread_;
  int64_t min_task_access_size_kb_;
};

class ObParallelRangeTaskPlanner
{
public:
  static int compute_total_task_count(const ObParallelRangeTaskParams &params,
                                      const int64_t total_size_kb,
                                      int64_t &total_task_count);
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_ACCESS_OB_PARALLEL_RANGE_TASK_PLANNER_H_
