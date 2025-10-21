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

#ifndef OCEANBASE_ROOTSERVER_OB_BACKUP_PARAM_OPERATOR_H_
#define OCEANBASE_ROOTSERVER_OB_BACKUP_PARAM_OPERATOR_H_

#include "lib/mysqlclient/ob_isql_client.h"
#include "storage/backup/ob_backup_data_struct.h"
#include "storage/backup/ob_backup_data_store.h"

namespace oceanbase {
namespace backup {
class ObBackupParamOperator final
{
public:
  //sys tenant get cluster parameters, user tenant get tenant parameters.
  static int get_backup_parameters_info(const uint64_t tenant_id, ObExternParamInfoDesc &param_info,
    common::ObISQLClient &sql_client);
  // observer call to backup cluster param
  static int backup_cluster_parameters(const share::ObBackupPathString &backup_dest_str);
private:
  const static char *TENANT_BLACK_PARAMETER_LIST[];
  const static char *CLUSTER_BLACK_PARAMETER_LIST[];
  static int construct_tenant_param_sql_(const uint64_t tenant_id, common::ObSqlString &sql);
  static int construct_cluster_param_sql_(common::ObSqlString &sql);
  static int construct_query_sql_(const uint64_t tenant_id, common::ObSqlString &sql);
  static int handle_one_result_(ObExternParamInfoDesc &param_info,
    common::sqlclient::ObMySQLResult &result);
};
}  // namespace backup
}  // namespace oceanbase

#endif
