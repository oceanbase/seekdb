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

#ifndef OB_GRANULE_FTS_UTIL_H_
#define OB_GRANULE_FTS_UTIL_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace sql
{

class ObGranuleFtsUtil
{
public:
  static int get_fts_forward_range(int64_t tablet_id,
                                   int64_t &start_doc_id,
                                   int64_t &end_doc_id)
  {
    int ret = OB_SUCCESS;
    return ret;
  }

  static int calc_fts_slice_index(int64_t doc_id, int64_t slice_count, int64_t &slice_idx)
  {
    int ret = OB_SUCCESS;
    slice_idx = (slice_count > 0) ? (doc_id % slice_count) : 0;
    return ret;
  }

  static int assign_fts_task_to_slice(int64_t doc_id,
                                      int64_t slice_count,
                                      int64_t &slice_idx)
  {
    int ret = OB_SUCCESS;
    slice_idx = doc_id % slice_count;
    return ret;
  }
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OB_GRANULE_FTS_UTIL_H_ */
