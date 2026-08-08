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

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_RANGE_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_RANGE_SERVICE_H_

#include "common/ob_store_range.h"
#include "common/ob_tablet_id.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_array_array.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace data_plane
{

// Neutral query/data-plane boundary for range sizing and splitting.  Calls are
// coarse grained; implementations retain ownership of tablets and storage
// internals, while callers only exchange common value types.
class ObIRangeService
{
public:
  virtual ~ObIRangeService() {}

  virtual int get_multi_ranges_cost(
      const common::ObTabletID &tablet_id,
      const int64_t timeout_us,
      const common::ObIArray<common::ObStoreRange> &ranges,
      int64_t &total_size) = 0;

  virtual int split_multi_ranges(
      const common::ObTabletID &tablet_id,
      const int64_t timeout_us,
      const common::ObIArray<common::ObStoreRange> &ranges,
      const int64_t expected_task_count,
      common::ObIAllocator &allocator,
      common::ObArrayArray<common::ObStoreRange> &multi_range_split_array) = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_RANGE_SERVICE_H_
