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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_SYS_VARIABLE_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_SYS_VARIABLE_SQL_SERVICE_H_

#include "ob_ddl_sql_service.h"
#include "share/ob_dml_sql_splicer.h"
//#include "share/schema/ob_schema_struct.h"

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
class ObSysVariableSchema;
class ObSysVarSchema;

class ObSysVariableSqlService : public ObDDLSqlService
{
public:
  ObSysVariableSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObSysVariableSqlService() {}

  virtual int replace_sys_variable(ObSysVariableSchema &sys_variable_schema,
                                   common::ObISQLClient &sql_client,
                                   const ObSchemaOperationType &operation_type,
                                   const common::ObString *ddl_stmt_str = NULL);
private:
  int replace_system_variable(const ObSysVarSchema &sysvar_schema, common::ObISQLClient &sql_client);
  int gen_sys_variable_dml(ObDMLSqlSplicer &dml, const ObSysVarSchema &sysvar_schema, bool is_history);
private:
  DISALLOW_COPY_AND_ASSIGN(ObSysVariableSqlService);
};


} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_SYS_VARIABLE_SQL_SERVICE_H_
