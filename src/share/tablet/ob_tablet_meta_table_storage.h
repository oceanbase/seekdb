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
#include <functional>

namespace oceanbase
{
namespace share
{

// Constructor for ObTabletReplica from SQLite result
class ObTabletReplicaConstructor
{
public:
  int operator()(share::ObSQLiteRowReader &reader, ObTabletReplica &replica);
};

class ObTabletMetaTableStorage
{
public:
  ObTabletMetaTableStorage();
  virtual ~ObTabletMetaTableStorage();

  // Initialize with shared connection pool instance
  int init(ObSQLiteConnectionPool *pool);

  bool is_inited() const { return nullptr != pool_; }

  // Get tablet replica by primary keys
  int get(
      const uint64_t tenant_id,
      const common::ObTabletID &tablet_id,
      const ObLSID &ls_id,
      const common::ObAddr &addr,
      ObTabletReplica &tablet_replica);

  // Get tablet info (all replicas for a tablet)
  int get(
      const uint64_t tenant_id,
      const common::ObTabletID &tablet_id,
      const ObLSID &ls_id,
      ObTabletInfo &tablet_info);

  // Batch get tablet infos
  int batch_get(
      const uint64_t tenant_id,
      const ObIArray<ObTabletLSPair> &tablet_ls_pairs,
      ObIArray<ObTabletInfo> &tablet_infos);

  // Range get tablet infos
  int range_get(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const int64_t range_size,
      ObIArray<ObTabletInfo> &tablet_infos);

  // Batch update replicas
  int batch_update(
      const uint64_t tenant_id,
      const ObIArray<ObTabletReplica> &replicas);

  // Batch update replicas within an external transaction
  int batch_update(
      ObSQLiteConnection *conn,
      const uint64_t tenant_id,
      const ObIArray<ObTabletReplica> &replicas);

  // Batch remove replicas
  int batch_remove(
      const uint64_t tenant_id,
      const ObIArray<ObTabletReplica> &replicas);

  // Batch remove replicas within an external transaction
  int batch_remove(
      ObSQLiteConnection *conn,
      const uint64_t tenant_id,
      const ObIArray<ObTabletReplica> &replicas);

  // Remove residual tablets for a server
  int remove_residual_tablet(
      const uint64_t tenant_id,
      const common::ObAddr &server,
      const int64_t limit,
      int64_t &affected_rows);

  // Get max data_size for a tablet-ls pair
  int get_max_data_size(
      const uint64_t tenant_id,
      const common::ObTabletID &tablet_id,
      const ObLSID &ls_id,
      int64_t &data_size);

  // Get max report_scn and max status for a tablet-ls pair
  int get_max_report_scn_and_status(
      const uint64_t tenant_id,
      const common::ObTabletID &tablet_id,
      const ObLSID &ls_id,
      int64_t &report_scn,
      int64_t &status);

  // Get min compaction_scn for a tenant
  int get_min_compaction_scn(
      const uint64_t tenant_id,
      uint64_t &min_compaction_scn);

  // Get tablet replica count for a tenant
  int get_tablet_replica_cnt(
      const uint64_t tenant_id,
      int64_t &tablet_replica_cnt);

  // Batch update status for specific tablet-ls pairs with compaction_scn
  int batch_update_status(
      const uint64_t tenant_id,
      const ObIArray<ObTabletLSPair> &tablet_ls_pairs,
      const ObIArray<int64_t> &compaction_scns,
      const int64_t status,
      int64_t &affected_rows);

  // Batch update report_scn for tablets
  int batch_update_report_scn(
      const uint64_t tenant_id,
      const ObIArray<ObTabletLSPair> &tablet_ls_pairs,
      const uint64_t report_scn,
      const uint64_t compaction_scn_min,
      const int64_t except_status,
      int64_t &affected_rows);

  // Batch update report_scn for tablets in a range
  int batch_update_report_scn_range(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const uint64_t report_scn,
      const uint64_t compaction_scn_min,
      const int64_t except_status,
      int64_t &affected_rows);

  // Batch update status for tablets in a range
  int batch_update_status_range(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const int64_t from_status,
      const int64_t to_status,
      int64_t &affected_rows);

  // Get distinct tablet_ids for a tenant, starting from a tablet_id
  int get_distinct_tablet_ids(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const int64_t limit,
      ObIArray<common::ObTabletID> &tablet_ids);

  // Get distinct tablet_ids for a tenant-ls with conditions
  int get_distinct_tablet_ids_with_conditions(
      const uint64_t tenant_id,
      const ObLSID &ls_id,
      const ObIArray<common::ObTabletID> &tablet_ids,
      const uint64_t report_scn_max,
      ObIArray<common::ObTabletID> &result_tablet_ids);

  // Get max tablet_id in a range
  int get_max_tablet_id_in_range(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const int64_t offset,
      common::ObTabletID &max_tablet_id);

  // Range scan for compaction with filters
  int range_scan_for_compaction(
      const uint64_t tenant_id,
      const common::ObTabletID &start_tablet_id,
      const common::ObTabletID &end_tablet_id,
      const int64_t compaction_scn,
      const bool add_report_scn_filter,
      ObIArray<ObTabletInfo> &tablet_infos);

  // Batch update report_scn for tablets with conditions (for unequal report_scn update)
  int batch_update_report_scn_unequal(
      const uint64_t tenant_id,
      const ObLSID &ls_id,
      const ObIArray<common::ObTabletID> &tablet_ids,
      const uint64_t major_frozen_scn,
      int64_t &affected_rows);

private:
  // Create table if not exists
  int create_table_if_not_exists();

  // Helper to construct tablet infos from replicas (grouping logic)
  int group_replicas_to_tablet_infos(
      const ObIArray<ObTabletReplica> &replicas,
      ObIArray<ObTabletInfo> &tablet_infos);

  ObSQLiteConnectionPool *pool_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_TABLET_OB_TABLET_META_TABLE_STORAGE_H_
