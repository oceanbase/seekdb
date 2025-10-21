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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_DATABASE_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_DATABASE_SQL_SERVICE_H_

#include "ob_ddl_sql_service.h"
#include "share/schema/ob_schema_service.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace share
{
namespace schema
{
class ObDatabaseSchema;

class ObDatabaseSqlService : public ObDDLSqlService
{
public:
  ObDatabaseSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service) {}
  virtual ~ObDatabaseSqlService() {}

  virtual int insert_database(const ObDatabaseSchema &database_schema,
                              common::ObISQLClient &sql_client,
                              const common::ObString *ddl_stmt_str = NULL,
                              const bool is_only_history = false);
  virtual int update_database(const ObDatabaseSchema &database_schema,
                              common::ObISQLClient &sql_client,
                              const ObSchemaOperationType op_type,
                              const common::ObString *ddl_stmt_str = NULL);
  virtual int delete_database(const ObDatabaseSchema &db_schema,
                              const int64_t new_schema_version,
                              common::ObISQLClient &sql_client,
                              const common::ObString *ddl_stmt_str = NULL);


  DISALLOW_COPY_AND_ASSIGN(ObDatabaseSqlService);
};

} //end of namespace schema
} //end of namespace share
} //end of namespace oceanbase

#endif //OCEANBASE_SHARE_SCHEMA_OB_DATABASE_SQL_SERVICE_H_
