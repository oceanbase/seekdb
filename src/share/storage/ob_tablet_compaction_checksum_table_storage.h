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

#ifndef OCEANBASE_SHARE_STORAGE_OB_TABLET_COMPACTION_CHECKSUM_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_TABLET_COMPACTION_CHECKSUM_TABLE_STORAGE_H_

#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/container/ob_iarray.h"
#include "share/scn.h"
#include "share/ob_tablet_compaction_checksum_operator.h"

namespace oceanbase
{
namespace share
{

class ObTabletCompactionChecksumTableStorage
{
public:
  ObTabletCompactionChecksumTableStorage();
  virtual ~ObTabletCompactionChecksumTableStorage();

  // Initialize with shared connection pool instance
  int init(ObSQLiteConnectionPool *pool);

  bool is_inited() const { return nullptr != pool_; }

  // Get checksum items for local tablets.
  int batch_get(
      const ObIArray<common::ObTabletID> &tablet_ids,
      const SCN &compaction_scn,
      ObTabletCompactionChecksumArray &items,
      const bool include_larger_than = false);

  // Range get checksum items
  int range_get(const common::ObTabletID &start_tablet_id,
      const int64_t range_size,
      ObIArray<ObTabletCompactionChecksumItem> &items,
      int64_t &tablet_cnt);

  // Get minimum compaction_scn from the local checksum table.
  int get_min_compaction_scn(uint64_t &min_compaction_scn);

  // Get max row_count for a local tablet.
  int get_max_row_count(const common::ObTabletID &tablet_id,
      int64_t &max_row_count);

  // Batch check tablet checksum for multiple tablets
  int batch_check_checksum(const ObIArray<common::ObTabletID> &tablet_ids,
      const int64_t start_idx,
      const int64_t end_idx,
      bool &has_error);

private:
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletCompactionChecksumTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_TABLET_COMPACTION_CHECKSUM_TABLE_STORAGE_H_
