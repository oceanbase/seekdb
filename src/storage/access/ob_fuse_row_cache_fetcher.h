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

#ifndef OB_FUSE_ROW_CACHE_FETCHER_H_
#define OB_FUSE_ROW_CACHE_FETCHER_H_

#include "storage/blocksstable/ob_fuse_row_cache.h"
#include "ob_table_access_param.h"
#include "ob_table_access_context.h"

namespace oceanbase
{
namespace storage
{

class ObTableAccessContext;

class ObFuseRowCacheFetcher final
{
public:
  ObFuseRowCacheFetcher();
  ~ObFuseRowCacheFetcher() = default;
  int init(const StorageScanType type, const ObTabletID &tablet_id, const ObITableReadInfo *read_info, const int64_t tablet_version,
           const int64_t read_start_version, const int64_t read_snapshot_version);
  int get_fuse_row_cache(const blocksstable::ObDatumRowkey &rowkey, blocksstable::ObFuseRowValueHandle &handle);
  int put_fuse_row_cache(const blocksstable::ObDatumRowkey &rowkey, blocksstable::ObDatumRow &row);
private:
  bool is_inited_;
  StorageScanType type_;
  ObTabletID tablet_id_;
  const ObITableReadInfo *read_info_;
  int64_t tablet_version_;
  int64_t read_start_version_;
  int64_t read_snapshot_version_;
};

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OB_FUSE_ROW_CACHE_FETCHER_H_
