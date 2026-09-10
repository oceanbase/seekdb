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

#ifndef OCEANBASE_SHARE_SCHEMA_GRAPH_SQL_SERVICE_H_
#define OCEANBASE_SHARE_SCHEMA_GRAPH_SQL_SERVICE_H_

#include "share/schema/ob_ddl_sql_service.h"
#include "share/schema/graph_schema.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
// All writes must use the caller's DDL transaction. The definition contains the
// complete set of base-table/column dependencies, so it is published atomically.
class GraphSqlService final : public ObDDLSqlService
{
public:
  explicit GraphSqlService(ObSchemaService &service) : ObDDLSqlService(service) {}
  int create_graph(const GraphSchema &schema, const common::ObString &ddl,
                   common::ObISQLClient &client);
  int drop_graph(const GraphSchema &schema, int64_t version,
                 const common::ObString &ddl, common::ObISQLClient &client);
  static int load_database_graphs(uint64_t database_id, common::ObISQLClient &client,
                                   common::ObIArray<GraphSchema> &graphs);
  static int check_relation_name(common::ObISQLClient &client, const ObTableSchema &table);
  static int check_table_ddl(common::ObISQLClient &client, const ObTableSchema &original,
                             const ObTableSchema *replacement,
                             uint64_t deleted_column = common::OB_INVALID_ID,
                             const ObColumnSchemaV2 *changed_column = nullptr);
private:
  int write_history(const GraphSchema &schema, int64_t version, bool deleted,
                    common::ObISQLClient &client);
  int log_graph_operation(const GraphSchema &schema, int64_t version, bool deleted,
                          const common::ObString &ddl, common::ObISQLClient &client);
  DISALLOW_COPY_AND_ASSIGN(GraphSqlService);
};
} // namespace schema
} // namespace share
} // namespace oceanbase
#endif
