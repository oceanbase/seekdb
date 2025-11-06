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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_TENANT_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_TENANT_SQL_SERVICE_H_

#include "share/schema/ob_ddl_sql_service.h"

namespace oceanbase
{
namespace common
{
class ObISQlClient;
}
namespace share
{
namespace schema
{
struct ObSchemaOperation;
class ObTenantSchema;

class ObTenantSqlService : public ObDDLSqlService
{
public:
  ObTenantSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObTenantSqlService() {}

  virtual int insert_tenant(const ObTenantSchema &tenant_schema,
                            const ObSchemaOperationType op,
                            common::ObISQLClient &sql_client,
                            const common::ObString *ddl_stmt_str = NULL);
  virtual int alter_tenant(const ObTenantSchema &tenant_schema,
                           common::ObISQLClient &sql_client,
                           const ObSchemaOperationType op,
                           const common::ObString *ddl_stmt_str = NULL);
  virtual int rename_tenant(const ObTenantSchema &tenant_schema,
                            common::ObISQLClient &sql_client,
                            const common::ObString *ddl_stmt_str = NULL);
  virtual int delay_to_drop_tenant(const ObTenantSchema &tenant_schema,
                                   ObMySQLTransaction &trans,
                                   const ObString *ddl_stmt_str);
  virtual int drop_tenant_to_recyclebin(const ObTenantSchema &tenant_schema,
                                        common::ObISQLClient &sql_client,
                                        const ObSchemaOperationType op,
                                        const common::ObString *ddl_stmt_str = NULL);
  virtual int delete_tenant(const uint64_t tenant_id,
                            const int64_t new_schema_version,
                            common::ObISQLClient &sql_client,
                            const common::ObString *ddl_stmt_str = NULL);
private:
  int replace_tenant(const ObTenantSchema &tenant_schema,
                     const ObSchemaOperationType op,
                     common::ObISQLClient &sql_client,
                     const common::ObString *ddl_stmt_str);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTenantSqlService);
};


} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_TENANT_SQL_SERVICE_H_
