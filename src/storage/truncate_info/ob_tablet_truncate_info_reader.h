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

#ifndef OCEANBASE_STORAGE_OB_TABLET_TRUNCATE_INFO_READER
#define OCEANBASE_STORAGE_OB_TABLET_TRUNCATE_INFO_READER

#include "storage/multi_data_source/mds_table_handle.h"
#include "storage/truncate_info/ob_truncate_info.h"
#include "storage/tablet/ob_mds_cl_range_query_iterator.h"
#include "storage/multi_data_source/mds_table_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObTabletTruncateInfoReader
{
public:
  ObTabletTruncateInfoReader();
  ~ObTabletTruncateInfoReader();
public:
  int init(
      const ObTablet &tablet,
      ObTableScanParam &scan_param);
  int get_next_truncate_info(
      common::ObIAllocator &allocator,
      ObTruncateInfoKey &key,
      ObTruncateInfo &truncate_info);
  int get_next_mds_kv(
      common::ObIAllocator &allocator,
      mds::MdsDumpKV *&kv);
private:
  bool is_inited_;
  common::ObArenaAllocator allocator_;
  ObMdsRangeQueryIterator<ObTruncateInfoKey, ObTruncateInfo> iter_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_TRUNCATE_INFO_READER
