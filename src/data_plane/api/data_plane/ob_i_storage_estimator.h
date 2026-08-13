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

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_STORAGE_ESTIMATOR_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_STORAGE_ESTIMATOR_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
struct ObSimpleBatch;
class ObTabletID;
struct ObEstRowCountRecord;
template <typename T> class ObIArray;
}
namespace storage
{
class ObTableScanParam;
}
namespace data_plane
{

// Query-facing estimation seam.  It keeps execution and tablet ownership in
// the data plane while exposing only the two coarse-grained estimates used by
// the optimizer.
class ObIStorageEstimator
{
public:
  virtual ~ObIStorageEstimator() {}

  virtual int estimate_row_count_for_batch(
      storage::ObTableScanParam &param,
      const common::ObSimpleBatch &batch,
      common::ObIAllocator &allocator,
      const int64_t timeout_us,
      common::ObIArray<common::ObEstRowCountRecord> &est_records,
      int64_t &logical_row_count,
      int64_t &physical_row_count) const = 0;

  virtual int estimate_block_count_and_row_count(
      const common::ObTabletID &tablet_id,
      const int64_t timeout_us,
      int64_t &macro_block_count,
      int64_t &micro_block_count,
      int64_t &sstable_row_count,
      int64_t &memtable_row_count) const = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_STORAGE_ESTIMATOR_H_
