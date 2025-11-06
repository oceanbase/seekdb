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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_SQL_SERVICE_H_

#include "ob_ddl_sql_service.h"

namespace oceanbase
{
namespace common
{
class ObString;
class ObISQLClient;
}
namespace share
{
class ObDMLSqlSplicer;
namespace schema
{
class ObContextSchema;

class ObContextSqlService : public ObDDLSqlService
{
public:
  ObContextSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObContextSqlService() {}

  virtual int insert_context(const ObContextSchema &context_schema,
                              common::ObISQLClient *sql_client,
                              const common::ObString *ddl_stmt_str = NULL);
  virtual int alter_context(const ObContextSchema &context_schema,
                               common::ObISQLClient *sql_client,
                               const common::ObString *ddl_stmt_str = NULL);
  virtual int delete_context(const uint64_t tenant_id,
                              const uint64_t context_id,
                              const ObString &ctx_namespace,
                              const int64_t new_schema_version,
                              const ObContextType &type,
                              common::ObISQLClient *sql_client,
                              const common::ObString *ddl_stmt_str = NULL);
  virtual int drop_context(const ObContextSchema &context_schema,
                            const int64_t new_schema_version,
                            common::ObISQLClient *sql_client,
                            bool &need_clean,
                            const common::ObString *ddl_stmt_str = NULL);
private:
  int add_context(common::ObISQLClient &sql_client, const ObContextSchema &context_schema,
                   const bool only_history = false);
  int format_dml_sql(const ObContextSchema &context_schema,
                     ObDMLSqlSplicer &dml,
                     bool &is_history);
private:
  DISALLOW_COPY_AND_ASSIGN(ObContextSqlService);
};

} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_SQL_SERVICE_H_
