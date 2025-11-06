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

#ifndef OCEANBASE_STORAGE_OB_TABLET_MEDIUM_INFO_READER
#define OCEANBASE_STORAGE_OB_TABLET_MEDIUM_INFO_READER

#include "storage/multi_data_source/mds_table_handle.h"
#include "storage/tablet/ob_tablet_dumped_medium_info.h"
#include "storage/tablet/ob_mds_cl_range_query_iterator.h"
#include "storage/multi_data_source/mds_table_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObTabletMediumInfoReader
{
public:
  ObTabletMediumInfoReader();
  ~ObTabletMediumInfoReader();
  static int get_medium_info_with_merge_version(
      const int64_t merge_version,
      const ObTablet &tablet,
      common::ObIAllocator &allocator,
      compaction::ObMediumCompactionInfo *&medium_info);
public:
  int init(
      const ObTablet &tablet,
      ObTableScanParam &scan_param);
  int get_next_medium_info(
      common::ObIAllocator &allocator,
      compaction::ObMediumCompactionInfoKey &key,
      compaction::ObMediumCompactionInfo &medium_info);
  int get_specified_medium_info(
      common::ObIAllocator &allocator,
      const compaction::ObMediumCompactionInfoKey &key,
      compaction::ObMediumCompactionInfo &medium_info);
  int get_min_medium_snapshot(
      const int64_t last_major_snapshot_version,
      int64_t &min_medium_snapshot);
  int get_next_mds_kv(
      common::ObIAllocator &allocator,
      mds::MdsDumpKV *&kv);
private:
  bool is_inited_;
  common::ObArenaAllocator allocator_;
  ObMdsRangeQueryIterator<compaction::ObMediumCompactionInfoKey, compaction::ObMediumCompactionInfo> iter_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_MEDIUM_INFO_READER
