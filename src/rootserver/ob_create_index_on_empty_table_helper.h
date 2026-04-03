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

#ifndef OCEANBASE_ROOTSERVER_OB_CREATE_INDEX_ON_EMPTY_TABLE_HELPER_H
#define OCEANBASE_ROOTSERVER_OB_CREATE_INDEX_ON_EMPTY_TABLE_HELPER_H
#include "lib/mysqlclient/ob_mysql_transaction.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/ob_rpc_struct.h"
namespace oceanbase
{
namespace share
{
class SCN;
}
namespace rootserver
{
class ObDDLService;
class ObCreateIndexOnEmptyTableHelper {
public:
  static int check_create_index_on_empty_table_opt(
    rootserver::ObDDLService &ddl_service,
    ObMySQLTransaction &trans,
    const share::schema::ObSysVariableSchema &sys_var_schema,
    const ObString &database_name,
    const share::schema::ObTableSchema &table_schema,
    ObIndexType index_type,
    uint64_t executor_data_version,
    const ObSQLMode sql_mode,
    bool &is_create_index_on_empty_table_opt);

  static int get_major_frozen_scn(
    const uint64_t tenant_id,
    share::SCN &major_frozen_scn);
};

} //namespace rootserver
} //namespace oceanbase
#endif
