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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_CHUNK_BUILDER_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_CHUNK_BUILDER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace sql
{

template <typename StoreRowType, bool HAS_ADDON>
class ObSortChunkBuilder
{
public:
  ObSortChunkBuilder() : level_(0)
  {
  }

  int init(ObIAllocator &allocator, int64_t level, int64_t file_buf_size)
  {
    level_ = level;
    return OB_SUCCESS;
  }

  int add_row(StoreRowType *row)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(rows_.push_back(row))) {
    }
    return ret;
  }

  int sort_and_flush()
  {
    return OB_SUCCESS;
  }

private:
  int64_t level_;
  common::ObSEArray<StoreRowType *, 64> rows_;
};

template <typename StoreRowType, bool HAS_ADDON>
class ObSortChunkMultiSlicer
{
public:
  ObSortChunkMultiSlicer() : slice_cnt_(0)
  {
  }

  int init(int64_t slice_cnt)
  {
    slice_cnt_ = slice_cnt;
    return OB_SUCCESS;
  }

  int assign_to_slice(StoreRowType *row, int64_t &slice_idx)
  {
    slice_idx = 0;
    return OB_SUCCESS;
  }

private:
  int64_t slice_cnt_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_CHUNK_BUILDER_H_ */
