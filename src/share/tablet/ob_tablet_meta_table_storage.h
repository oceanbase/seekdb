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

#ifndef OCEANBASE_SHARE_TABLET_OB_TABLET_META_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_TABLET_OB_TABLET_META_TABLE_STORAGE_H_

#include "share/tablet/ob_tablet_info.h"
#include "lib/container/ob_iarray.h"
#include "share/storage/ob_sqlite_connection_pool.h"

namespace oceanbase
{
namespace share
{

class ObTabletRuntimeInfoConstructor
{
public:
  int operator()(share::ObSQLiteRowReader &reader, ObTabletRuntimeInfo &info);
};

class ObTabletMetaTableStorage
{
public:
  ObTabletMetaTableStorage();
  virtual ~ObTabletMetaTableStorage();

  // Initialize with shared connection pool instance
  int init(ObSQLiteConnectionPool *pool);

  bool is_inited() const { return nullptr != pool_; }

  // Batch get tablet infos
  int batch_get(
      const ObIArray<common::ObTabletID> &tablet_ids,
      ObIArray<ObTabletRuntimeInfo> &tablet_infos);

  // Batch update local tablet runtime metadata.
  int batch_update(
      const ObIArray<ObTabletRuntimeInfo> &tablet_infos);

  // Batch update local tablet runtime metadata within an external transaction.
  int batch_update(
      ObSQLiteConnection *conn,
      const ObIArray<ObTabletRuntimeInfo> &tablet_infos);

  // Batch remove local tablet runtime metadata.
  int batch_remove(
      const ObIArray<ObTabletRuntimeInfo> &tablet_infos);

  // Batch remove local tablet runtime metadata within an external transaction.
  int batch_remove(
      ObSQLiteConnection *conn,
      const ObIArray<ObTabletRuntimeInfo> &tablet_infos);

  // Get data_size for a local tablet.
  int get_data_size(const common::ObTabletID &tablet_id,
      int64_t &data_size);

  // Get report_scn and status for a local tablet.
  int get_report_scn_and_status(const common::ObTabletID &tablet_id,
      int64_t &report_scn,
      int64_t &status);
  int get_max_report_scn_and_status(const common::ObTabletID &tablet_id,
      int64_t &report_scn,
      int64_t &status)
  {
    return get_report_scn_and_status(tablet_id, report_scn, status);
  }

  // Get the runtime's minimum compaction SCN.
  int get_min_compaction_scn(uint64_t &min_compaction_scn);

  int get_tablet_count(int64_t &tablet_count);

  // Batch update report_scn for tablets
  int batch_update_report_scn(
      const ObIArray<common::ObTabletID> &tablet_ids,
      const uint64_t report_scn,
      const uint64_t compaction_scn_min,
      const int64_t except_status,
      int64_t &affected_rows);

  // Batch update report_scn for tablets in a range
  int batch_update_report_scn_range(const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const uint64_t report_scn,
      const uint64_t compaction_scn_min,
      const int64_t except_status,
      int64_t &affected_rows);

  // Batch update status for tablets in a range
  int batch_update_status_range(const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const int64_t from_status,
      const int64_t to_status,
      int64_t &affected_rows);

  // Get local tablet IDs after a starting tablet ID.
  int get_tablet_ids(const common::ObTabletID &start_tablet_id,
      const int64_t limit,
      ObIArray<common::ObTabletID> &tablet_ids);

  // Get tablet_ids whose local report_scn is behind the target.
  int get_tablet_ids_with_report_scn_before(
      const ObIArray<common::ObTabletID> &tablet_ids,
      const uint64_t report_scn_max,
      ObIArray<common::ObTabletID> &result_tablet_ids);

  // Get max tablet_id in a range
  int get_max_tablet_id_in_range(const common::ObTabletID &start_tablet_id,
      const int64_t batch_size,
      common::ObTabletID &max_tablet_id);

  // Range scan local compaction metadata, optionally skipping already reported rows.
  int range_scan_for_compaction(const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const int64_t compaction_scn,
      const bool only_unreported,
      ObIArray<ObTabletRuntimeInfo> &tablet_infos);

  // Batch update report_scn for tablets with conditions (for unequal report_scn update)
  int batch_update_report_scn_unequal(
      const ObIArray<common::ObTabletID> &tablet_ids,
      const uint64_t major_frozen_scn,
      int64_t &affected_rows);

private:
  // Create table if not exists
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_TABLET_OB_TABLET_META_TABLE_STORAGE_H_
