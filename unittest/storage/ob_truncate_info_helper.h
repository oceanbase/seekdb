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

#ifndef OCEANBASE_UNITTEST_TRUNCATE_INFO_HELPER
#define OCEANBASE_UNITTEST_TRUNCATE_INFO_HELPER

#include <stdint.h>
#define protected public
#define private public

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObArenaAllocator;
class ObRowkey;
class ObTabletID;
struct ObVersionRange;
}
namespace share
{
class ObLSID;
}
namespace storage
{
struct ObTruncateInfo;
struct ObTruncatePartition;
struct ObTruncateInfoArray;
class ObTablet;
class ObTabletHandle;
class TruncateInfoHelper
{
public:
  enum MockTruncateInfoCol : uint8_t
  {
    TRANS_ID = 0,
    COMMIT_VERSION,
    SCHEMA_VERSION,
    LOWER_BOUND,
    UPPER_BOUND,
    COL_MAX
  };
  static void mock_truncate_info(
    common::ObIAllocator &allocator,
    const int64_t trans_id,
    const int64_t schema_version,
    ObTruncateInfo &info);
  static void mock_truncate_info(
    common::ObIAllocator &allocator,
    const int64_t trans_id,
    const int64_t schema_version,
    const int64_t commit_version,
    ObTruncateInfo &info);
  static int mock_truncate_partition(
    common::ObIAllocator &allocator,
    const int64_t low_bound_val,
    const int64_t high_bound_val,
    ObTruncatePartition &part);
  static int mock_truncate_partition(
    common::ObIAllocator &allocator,
    const common::ObRowkey &low_bound_val,
    const common::ObRowkey &high_bound_val,
    ObTruncatePartition &part);
  static int mock_truncate_partition(
    common::ObIAllocator &allocator,
    const int64_t *list_val,
    const int64_t list_val_cnt,
    ObTruncatePartition &part);
  static int get_tablet(
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    ObTabletHandle &tablet_handle);
  static int mock_part_key_idxs(
    common::ObIAllocator &allocator,
    const int64_t cnt,
    const int64_t *col_idxs,
    ObTruncatePartition &part);
  static int mock_part_key_idxs(
    common::ObIAllocator &allocator,
    const int64_t col_idx,
    ObTruncatePartition &part)
    {
      return mock_part_key_idxs(allocator, 1, &col_idx, part);
    }
  static int read_distinct_truncate_info_array(
    common::ObArenaAllocator &allocator,
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    const common::ObVersionRange &read_version_range,
    storage::ObTruncateInfoArray &truncate_info_array);
  static int batch_mock_truncate_info(
    common::ObArenaAllocator &allocator,
    const char *key_data,
    storage::ObTruncateInfoArray &truncate_info_array);
};

} // storage
} // oceanbase

#endif // OCEANBASE_UNITTEST_TRUNCATE_INFO_HELPER
