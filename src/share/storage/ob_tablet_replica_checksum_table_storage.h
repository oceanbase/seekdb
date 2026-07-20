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

#ifndef OCEANBASE_SHARE_STORAGE_OB_TABLET_REPLICA_CHECKSUM_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_TABLET_REPLICA_CHECKSUM_TABLE_STORAGE_H_

#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/container/ob_iarray.h"
#include "lib/net/ob_addr.h"
#include "share/scn.h"
#include "share/ob_tablet_replica_checksum_operator.h"
#include "share/tablet/ob_tablet_info.h"

namespace oceanbase
{
namespace share
{

// Tablet replica checksum entry structure for SQLite storage
struct ObTabletReplicaChecksumEntry
{
  int64_t tablet_id_;
  common::ObAddr svr_addr_;
  uint64_t compaction_scn_;
  int64_t row_count_;
  int64_t data_checksum_;
  common::ObString column_checksums_;  // Serialized column checksums (TEXT)
  common::ObString b_column_checksums_;  // Binary column checksums (BLOB)
  int64_t data_checksum_type_;

  ObTabletReplicaChecksumEntry()
    : tablet_id_(0),
      svr_addr_(),
      compaction_scn_(0),
      row_count_(0),
      data_checksum_(0),
      column_checksums_(),
      b_column_checksums_(),
      data_checksum_type_(0)
  {}

  void reset()
  {
    tablet_id_ = 0;
    svr_addr_.reset();
    compaction_scn_ = 0;
    row_count_ = 0;
    data_checksum_ = 0;
    column_checksums_.reset();
    b_column_checksums_.reset();
    data_checksum_type_ = 0;
  }
};

class ObTabletReplicaChecksumTableStorage
{
public:
  ObTabletReplicaChecksumTableStorage();
  virtual ~ObTabletReplicaChecksumTableStorage();

  // Initialize with shared connection pool instance
  int init(ObSQLiteConnectionPool *pool);

  bool is_inited() const { return nullptr != pool_; }

  // Batch insert or update checksum items
  int batch_insert_or_update(const ObIArray<ObTabletReplicaChecksumItem> &items);

  // Batch remove checksum items
  int batch_remove(
      const ObIArray<ObTabletReplica> &replicas);

  // Remove residual checksum for a server
  int remove_residual(const common::ObAddr &server,
      const int64_t limit,
      int64_t &affected_rows);

  // Get checksum items for tablets
  int batch_get(
      const ObIArray<common::ObTabletID> &tablet_ids,
      const SCN &compaction_scn,
      ObReplicaCkmArray &items,
      const bool include_larger_than = false);

  // Range get checksum items
  int range_get(const common::ObTabletID &start_tablet_id,
      const int64_t range_size,
      ObIArray<ObTabletReplicaChecksumItem> &items,
      int64_t &tablet_cnt);

  // Get minimum compaction_scn for a tenant
  int get_min_compaction_scn(uint64_t &min_compaction_scn);

  // Get max row_count for a tablet-ls pair
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
  DISALLOW_COPY_AND_ASSIGN(ObTabletReplicaChecksumTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_TABLET_REPLICA_CHECKSUM_TABLE_STORAGE_H_
