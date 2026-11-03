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

#pragma once

#include "share/schema/ob_table_schema.h"
#include "common/mysqlclient/ob_isql_client.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

// Keeps the __all_ai_column metadata in sync with the AI columns of a table.
// The rows describe the generation rule of every AI column and are the
// user visible description of the feature (SHOW AI COLUMNS, DBA views).
class ObAiColumnSqlService
{
public:
  // Remove the metadata rows of the columns that are no longer AI columns
  // (origin_table_schema, may be NULL) and insert the rows of the current AI
  // columns of table_schema.
  static int sync_ai_column_meta(common::ObISQLClient &sql_client,
                                  const ObTableSchema &table_schema,
                                  const ObTableSchema *origin_table_schema);
  // Remove all metadata rows of a table (table dropped).
  static int delete_ai_column_meta(common::ObISQLClient &sql_client,
                                    const uint64_t table_id);
  // Write the metadata row of a single AI column.
  static int write_ai_column_meta(common::ObISQLClient &sql_client,
                                   const ObTableSchema &table_schema,
                                   const ObColumnSchemaV2 &ai_column);
  // Remove the metadata row of a single AI column.
  static int remove_ai_column_meta(common::ObISQLClient &sql_client,
                                    const uint64_t table_id,
                                    const uint64_t column_id);
};

} // namespace schema
} // namespace share
} // namespace oceanbase
