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

#ifndef OB_DDL_MERGE_SORT_TASK_H_
#define OB_DDL_MERGE_SORT_TASK_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"
#include "share/task/ob_dag.h"

namespace oceanbase
{
namespace storage
{

static const int64_t MAX_FTS_MERGE_WAYS = 64;
static const int64_t FINAL_MERGE_WAYS = 4;

template <typename ChunkType>
class ObDDLMergeSortTask : public share::ObITask
{
public:
  ObDDLMergeSortTask()
    : is_inited_(false), slice_idx_(0), final_merge_ways_(FINAL_MERGE_WAYS)
  {
  }

  int init(int64_t slice_idx, int64_t final_merge_ways = FINAL_MERGE_WAYS)
  {
    int ret = OB_SUCCESS;
    slice_idx_ = slice_idx;
    final_merge_ways_ = final_merge_ways;
    is_inited_ = true;
    return ret;
  }

  int process() override
  {
    int ret = OB_SUCCESS;
    int64_t chunk_count = chunks_.count();
    if (chunk_count <= final_merge_ways_) {
    } else {
      int64_t pop_count = MIN(chunk_count / 2, MAX_FTS_MERGE_WAYS);
      if (pop_count < 2) {
        pop_count = 2;
      }
      if (OB_FAIL(do_merge_pass(pop_count))) {
      } else {
        ret = OB_DAG_TASK_IS_SUSPENDED;
      }
    }
    return ret;
  }

private:
  int do_merge_pass(int64_t pop_count)
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  bool is_inited_;
  int64_t slice_idx_;
  int64_t final_merge_ways_;
  common::ObSEArray<ChunkType, 16> chunks_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_DDL_MERGE_SORT_TASK_H_ */
