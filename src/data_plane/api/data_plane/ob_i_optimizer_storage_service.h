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

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_OPTIMIZER_STORAGE_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_OPTIMIZER_STORAGE_SERVICE_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObTabletID;
}
namespace data_plane
{

// Query-facing storage operations used by optimizer statistics. Storage keeps
// ownership of tablet statistics, macro blocks, and IO handles; Query sees
// only the row-count delta and benchmark result it actually consumes.
class ObIOptimizerStorageService
{
public:
  virtual ~ObIOptimizerStorageService() {}

  virtual int get_latest_tablet_row_count_delta(
      const common::ObTabletID &tablet_id,
      int64_t &row_count_delta) const = 0;

  virtual int run_io_benchmark(
      common::ObIAllocator &allocator,
      int64_t &disk_random_read_speed,
      int64_t &disk_sequential_read_speed) const = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_OPTIMIZER_STORAGE_SERVICE_H_
