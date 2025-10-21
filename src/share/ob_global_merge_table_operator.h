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

#ifndef OCEANBASE_SHARE_OB_GLOBAL_MERGE_TABLE_OPERATOR_
#define OCEANBASE_SHARE_OB_GLOBAL_MERGE_TABLE_OPERATOR_

#include "lib/mysqlclient/ob_isql_connection_pool.h"
#include "lib/container/ob_iarray.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "common/ob_zone.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace share
{
struct ObGlobalMergeInfo;

// CRUD operation to __all_merge_info table
class ObGlobalMergeTableOperator
{
public:
  static int load_global_merge_info(common::ObISQLClient &sql_client,
                                    const uint64_t tenant_id,
                                    share::ObGlobalMergeInfo &info,
                                    const bool print_sql = false);
  static int insert_global_merge_info(common::ObISQLClient &sql_client,
                                      const uint64_t tenant_id,
                                      const share::ObGlobalMergeInfo &info);
  // According to each filed's <need_update_> to decide whether need to be updated
  static int update_partial_global_merge_info(common::ObISQLClient &sql_client,
                                              const uint64_t tenant_id,
                                              const share::ObGlobalMergeInfo &info);

private:
  static int check_scn_revert(common::ObISQLClient &sql_client,
                              const uint64_t tenant_id,
                              const share::ObGlobalMergeInfo &info);
};

} // end namespace share
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_OB_GLOBAL_MERGE_TABLE_OPERATOR_
