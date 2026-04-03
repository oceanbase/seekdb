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

#ifndef OCEANBASE_SHARE_OB_ZONE_MERGE_TABLE_OPERATOR_
#define OCEANBASE_SHARE_OB_ZONE_MERGE_TABLE_OPERATOR_

#include "lib/container/ob_iarray.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "common/ob_zone.h"
#include "share/storage/ob_zone_merge_info_table_storage.h"

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace share
{
class ObZoneMergeInfo;

// CRUD operation to __all_zone_merge_info table
class ObZoneMergeTableOperator
{
public:
  // Initialize SQLite storage (called once at startup)
  static int init();
  static int get_zone_list(common::ObISQLClient &sql_client, 
                           const uint64_t tenant_id,
                           common::ObIArray<common::ObZone> &zone_list);
  static int load_zone_merge_info(common::ObISQLClient &sql_client,
                                  const uint64_t tenant_id,
                                  share::ObZoneMergeInfo &info,
                                  const bool print_sql = false);
  static int load_zone_merge_infos(common::ObISQLClient &sql_client,
                                   const uint64_t tenant_id,
                                   common::ObIArray<share::ObZoneMergeInfo> &infos,
                                   const bool print_sql = false);

  static int insert_zone_merge_infos(common::ObISQLClient &sql_client,
                                     const uint64_t tenant_id,
                                     const common::ObIArray<share::ObZoneMergeInfo> &infos);
  // According to each filed's <need_update_> to decide whether need to be updated
  static int update_partial_zone_merge_info(common::ObISQLClient &sql_client,
                                            const uint64_t tenant_id,
                                            const share::ObZoneMergeInfo &info);
  static int update_zone_merge_infos(common::ObISQLClient &sql_client,
                                     const uint64_t tenant_id,
                                     const common::ObIArray<share::ObZoneMergeInfo> &infos);

private:
  static ObZoneMergeInfoTableStorage storage_;
};

} // end namespace share
} // end namespace oceanbase

#endif  // OCEANBASE_SHARE_OB_ZONE_MERGE_TABLE_OPERATOR_
