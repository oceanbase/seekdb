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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_EXTERNAL_MERGE_SORTER_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_EXTERNAL_MERGE_SORTER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_heap.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace sql
{

static const int64_t MAX_MERGE_WAYS_STATIC = 256;

template <typename ChunkType, typename CompareType>
class ObExternalMergeSorter
{
public:
  typedef common::ObBinaryHeap<ChunkType *, CompareType, 16> MergeHeap;

  ObExternalMergeSorter() : is_inited_(false), chunk_count_(0)
  {
  }

  int init(CompareType &comp, int64_t merge_ways)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(chunks_.init(merge_ways))) {
    } else {
      comp_ = &comp;
      merge_ways_ = merge_ways;
      is_inited_ = true;
    }
    return ret;
  }

  int add_chunk(ChunkType *chunk)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(chunks_.push_back(chunk))) {
    } else {
      chunk_count_++;
    }
    return ret;
  }

  int sort()
  {
    int ret = OB_SUCCESS;
    while (OB_SUCC(ret) && chunk_count_ > static_cast<int64_t>(merge_ways_)) {
      if (OB_FAIL(do_merge_pass())) {
      }
    }
    return ret;
  }

private:
  int do_merge_pass()
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  bool is_inited_;
  int64_t chunk_count_;
  int64_t merge_ways_;
  CompareType *comp_;
  common::ObSEArray<ChunkType *, MAX_MERGE_WAYS_STATIC> chunks_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_EXTERNAL_MERGE_SORTER_H_ */
