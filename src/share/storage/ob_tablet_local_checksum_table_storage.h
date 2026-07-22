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

#ifndef OCEANBASE_SHARE_STORAGE_OB_TABLET_LOCAL_CHECKSUM_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_TABLET_LOCAL_CHECKSUM_TABLE_STORAGE_H_

#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/container/ob_iarray.h"
#include "share/scn.h"
#include "share/ob_tablet_local_checksum_operator.h"
#include "share/tablet/ob_tablet_info.h"

namespace oceanbase
{
namespace share
{

class ObTabletLocalChecksumTableStorage
{
public:
  ObTabletLocalChecksumTableStorage();
  virtual ~ObTabletLocalChecksumTableStorage();

  // Initialize with shared connection pool instance
  int init(ObSQLiteConnectionPool *pool);

  bool is_inited() const { return nullptr != pool_; }

  // Get checksum items for local tablets.
  int batch_get(
      const ObIArray<common::ObTabletID> &tablet_ids,
      const SCN &compaction_scn,
      ObLocalTabletChecksumArray &items,
      const bool include_larger_than = false);

  // Get row_count for a local tablet.
  int get_row_count(const common::ObTabletID &tablet_id,
      int64_t &row_count);

private:
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletLocalChecksumTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_TABLET_LOCAL_CHECKSUM_TABLE_STORAGE_H_
