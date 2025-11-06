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

#ifndef OCEANBASE_ROOTSERVER_MVIEW_OB_MVIEW_ALTER_SERVICE_H_
#define OCEANBASE_ROOTSERVER_MVIEW_OB_MVIEW_ALTER_SERVICE_H_

#include "lib/ob_define.h"
#include "share/schema/ob_table_schema.h"
#include "src/share/ob_rpc_struct.h"
#include "rootserver/ob_ddl_service.h"

namespace oceanbase
{
namespace rootserver
{
class ObMviewAlterService
{
public:
  static int alter_mview_or_mlog_in_trans(obrpc::ObAlterTableArg &alter_table_arg,
                                          obrpc::ObAlterTableRes &res,
                                          ObSchemaGetterGuard &schema_guard,
                                          share::schema::ObMultiVersionSchemaService *schema_service,
                                          common::ObMySQLProxy *sql_proxy,
                                          const uint64_t tenant_data_version);

private:
  static int alter_mview_attributes(const uint64_t tenant_id,
                                    const ObTableSchema *orig_table_schema,
                                    obrpc::ObAlterTableArg &alter_table_arg,
                                    ObDDLOperator &ddl_operator, ObSchemaGetterGuard &schema_guard,
                                    ObDDLSQLTransaction &trans);
  static int alter_mlog_attributes(const uint64_t tenant_id, const ObTableSchema *orig_table_schema,
                                   obrpc::ObAlterTableArg &alter_table_arg,
                                   ObDDLOperator &ddl_operator, ObSchemaGetterGuard &schema_guard,
                                   ObDDLSQLTransaction &trans);
};

} // namespace rootserver
} // namespace oceanbase
#endif
