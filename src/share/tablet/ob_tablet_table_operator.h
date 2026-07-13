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

#ifndef OCEANBASE_SHARE_OB_TABLET_TABLE_OPERATOR
#define OCEANBASE_SHARE_OB_TABLET_TABLE_OPERATOR

#include "lib/container/ob_iarray.h" //ObIArray
#include "storage/compaction/ob_tablet_check_info.h"
#include "share/tablet/ob_tablet_info.h" // ObTabletReplica, ObTabletInfo
#include "share/compaction/ob_array_with_map.h"
#include "share/tablet/ob_tablet_meta_table_storage.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
class ObAddr;
class ObTabletID;
}
namespace share
{
class ObDMLSqlSplicer;

// Operator for __all_tablet_meta_table.
// Providing (batch-)get, (batch-)update, remove capabilities by sql.
// Notes:
//   __all_tablet_meta_table in SYS_TENANT: record tablet meta for itself;
//   __all_tablet_meta_table in META_TENANT: recod tablet meta for META_TENANT and USER_TENANT;
//   USER_TENANT dosen't have __all_tablet_meta_table.
class ObTabletTableOperator
{
public:
  ObTabletTableOperator();
  virtual ~ObTabletTableOperator();
  // Initialize with SQLite storage
  int init(share::ObSQLiteConnectionPool *pool);
  void reset();
  void set_batch_size(int64_t batch_size) {batch_size_ = batch_size;}
  // update ObTabletReplica into __all_tablet_meta_table
  //
  // @param [in] replica, ObTabletReplica for update
  // batch get ObTabletInfos according to tablet_ids
  //
  // @param [in] tablet_ids, tablet ids for query
  // @param [out] tablet_infos, array of tablet infos from __all_tablet_meta_table.
  // @return empty tablet_info if tablet does not exist in meta table.
  int batch_get(
      const ObIArray<common::ObTabletID> &tablet_ids,
      ObIArray<ObTabletInfo> &tablet_infos);
  // range get tablet infos from start_tablet_id
  //
  // @param [in] tenant, tenant for query
  // @param [in] start_tablet_id, starting point of the range (not included in output!)
  //             Usually start from 0.
  // @param [in] range_size, range size of the query
  // @param [out] tablet_infos, ObTabletInfos from __all_tablet_meta_table
  // @return OB_SUCCESS if success
  int range_get(const common::ObTabletID &start_tablet_id,
      const int64_t range_size,
      ObIArray<ObTabletInfo> &tablet_infos);
  // batch update tablet meta rows into __all_tablet_meta_table
  //
  // @param [in] tenant, tenant for query
  // @param [in] replicas, ObTabletReplicas for updating(should belong to the same tenant!)
  int batch_update(
      const ObIArray<ObTabletReplica> &replicas);
  // batch update tablet meta rows within an external SQLite transaction
  int batch_update(
      ObSQLiteConnection *conn,
      const ObIArray<ObTabletReplica> &replicas);
  // batch remove tablet meta rows from __all_tablet_meta_table
  //
  // @param [in] tenant, target tenant
  // @param [in] replicas, ObTabletReplicas for removing(should belong to the same tenant!)
  //             (only tablet_id and server are used in this interface)
  int batch_remove(
      const ObIArray<ObTabletReplica> &replicas);
  // batch remove tablet meta rows within an external SQLite transaction
  int batch_remove(
      ObSQLiteConnection *conn,
      const ObIArray<ObTabletReplica> &replicas);
  // remove residual tablet in __all_tablet_meta_table for ObServerMetaTableChecker
  //
  // @param [in] sql_client, client for executing query
  // @param [in] tenant, tenant for query
  // @param [in] server, target ObAddr
  // @param [in] limit, limit number for delete sql
  // @param [out] residual_count, count of residual tablets in table
  int remove_residual_tablet(
      ObISQLClient &sql_client,
      const ObAddr &server,
      const int64_t limit,
      int64_t &affected_rows);
public:
  static int batch_get_tablet_info(
      common::ObISQLClient *sql_proxy,
      const ObIArray<compaction::ObTabletCheckInfo> &tablet_check_infos,
      const int32_t group_id,
      ObArrayWithMap<ObTabletInfo> &tablet_infos);
private:
  const static int64_t MAX_BATCH_COUNT = 100;
  bool inited_;
  ObTabletMetaTableStorage storage_;
  int64_t batch_size_;
  int32_t group_id_;
};
} // end namespace share
} // end namespace oceanbase
#endif
