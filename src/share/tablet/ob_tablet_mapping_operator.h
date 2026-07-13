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

#ifndef OCEANBASE_SHARE_OB_TABLET_MAPPING_OPERATOR
#define OCEANBASE_SHARE_OB_TABLET_MAPPING_OPERATOR

#include "lib/container/ob_iarray.h"     // ObIArray
#include "share/tablet/ob_tablet_info.h" // ObTabletTablePair

namespace oceanbase
{
namespace common
{
class ObISQLClient;

namespace sqlclient
{
class ObMySQLResult;
}
} // end nampspace common

namespace share
{
// This operator keeps the tablet-to-table mapping in the legacy inner table.
class ObTabletMappingTableOperator
{
public:
  ObTabletMappingTableOperator() {}
  virtual ~ObTabletMappingTableOperator() {}
  // Get tablets sequentially by range
  //
  static int range_get_tablet_table_pairs(
      common::ObISQLClient &sql_proxy,
      const ObTabletID &start_tablet_id,
      const int64_t range_size,
      common::ObIArray<ObTabletTablePair> &tablets);

  // Updates tablet-to-table pairs in the legacy tablet mapping table.
  //
  // @param [in] sql_proxy, ObMySQLProxy or ObMySQLTransaction
  // @param [in] infos, tablet-to-table pairs for updating
  // @return OB_SUCCESS if success
  static int batch_update(
      common::ObISQLClient &sql_proxy,
      const ObIArray<ObTabletTablePair> &infos);
  // Removes tablet_id from the legacy tablet mapping table.
  //
  // @param [in] sql_proxy, ObMySQLProxy or ObMySQLTransaction
  // @param [in] tablet_ids, ObTabletIDs for removing
  // @return OB_SUCCESS if success
  static int batch_remove(
      common::ObISQLClient &sql_proxy,
      const ObIArray<common::ObTabletID> &tablet_ids);
  static int update_table_to_tablet_id_mapping(
      common::ObISQLClient &sql_proxy,
      const uint64_t table_id,
      const common::ObTabletID &tablet_id);
  // Get tablet-to-table mapping rows according to ObTabletIDs.
  //
  // @param [in] sql_proxy, ObMySQLProxy or ObMySQLTransaction
  // @param [in] tablet_ids, ObTabletIDs for query
  //             (should exist in the legacy tablet mapping table and have no duplicate values)
  // @param [out] infos, ObTabletTablePair corresponding to tablet_ids (not same order)
  //              not same order, not same order, not same order
  // @return OB_SUCCESS if success;
  //         OB_ITEM_NOT_MATCH if tablet_ids have duplicates or nonexistent tablets;
  //         Other error according to unexpected situation
  static int batch_get(
      common::ObISQLClient &sql_proxy,
      const ObIArray<common::ObTabletID> &tablet_ids,
      ObIArray<ObTabletTablePair> &infos);

  const static int64_t MAX_BATCH_COUNT = 200;
private:
  static int inner_batch_get_(
      common::ObISQLClient &sql_proxy,
      const ObIArray<common::ObTabletID> &tablet_ids,
      const int64_t start_idx,
      const int64_t end_idx,
      ObIArray<ObTabletTablePair> &infos);
  static int inner_batch_update_by_sql_(
      common::ObISQLClient &sql_proxy,
      const ObIArray<ObTabletTablePair> &infos,
      const int64_t start_idx,
      const int64_t end_idx);
  static int inner_batch_remove_by_sql_(
      common::ObISQLClient &sql_proxy,
      const ObIArray<common::ObTabletID> &tablet_ids,
      const int64_t start_idx,
      const int64_t end_idx);

  static int construct_results_(
      common::sqlclient::ObMySQLResult &res,
      ObIArray<ObTabletTablePair> &infos);
};

} // end namespace share
} // end namespace oceanbase
#endif // OCEANBASE_SHARE_OB_TABLET_MAPPING_OPERATOR
