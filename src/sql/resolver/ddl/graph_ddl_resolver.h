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

#ifndef OCEANBASE_SQL_RESOLVER_GRAPH_DDL_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_GRAPH_DDL_RESOLVER_H_
#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "sql/resolver/ddl/graph_ddl_stmt.h"
namespace oceanbase
{
namespace sql
{
class GraphDDLResolver final : public ObDDLResolver
{
public:
  explicit GraphDDLResolver(ObResolverParams &params) : ObDDLResolver(params) {}
  int resolve(const ParseNode &node) override;
private:
  int resolve_base_table(const ParseNode *node, uint64_t database_id,
                         const share::schema::ObTableSchema *&table);
  int resolve_keys(const ParseNode &node, const share::schema::ObTableSchema &table,
                   uint64_t *columns, int64_t &count);
  int resolve_elements(const ParseNode &node, GraphDDLStmt &stmt,
                       common::ObIArray<share::schema::GraphElement> &elements,
                       common::ObIArray<share::schema::GraphProperty> &properties,
                       common::ObIArray<const share::schema::ObTableSchema *> &tables);
  DISALLOW_COPY_AND_ASSIGN(GraphDDLResolver);
};
} // namespace sql
} // namespace oceanbase
#endif
