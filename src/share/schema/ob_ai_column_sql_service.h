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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_AI_COLUMN_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_OB_AI_COLUMN_SQL_SERVICE_H_

#include "ob_ddl_sql_service.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

class ObAIColumnSchema;
class ObTableSchema;

class ObAIColumnSqlService final : public ObDDLSqlService
{
public:
  explicit ObAIColumnSqlService(ObSchemaService &schema_service)
    : ObDDLSqlService(schema_service)
  {}
  virtual ~ObAIColumnSqlService() = default;

  int create_ai_columns(const ObTableSchema &table_schema,
                        common::ObISQLClient &sql_client);
  int upsert_ai_column(const ObAIColumnSchema &schema,
                       common::ObISQLClient &sql_client);
  int delete_ai_column(const uint64_t table_id,
                       const uint64_t column_id,
                       const int64_t schema_version,
                       common::ObISQLClient &sql_client);

private:
  int add_ai_column_fields(const ObAIColumnSchema &schema,
                           share::ObDMLSqlSplicer &dml);
  DISALLOW_COPY_AND_ASSIGN(ObAIColumnSqlService);
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_SCHEMA_OB_AI_COLUMN_SQL_SERVICE_H_
