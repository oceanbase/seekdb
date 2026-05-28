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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_DIFF_TABLE_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_CMD_OB_DIFF_TABLE_RESOLVER_H_

#include "sql/resolver/ob_stmt_resolver.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace sql
{

class ObDiffTableStmt;

// CMD resolver for DIFF TABLE. Builds an ObDiffTableStmt that the
// executor consumes; no SQL synthesis is performed here.
class ObDiffTableResolver : public ObStmtResolver
{
public:
  explicit ObDiffTableResolver(ObResolverParams &params)
    : ObStmtResolver(params)
  {}
  virtual ~ObDiffTableResolver() = default;
  virtual int resolve(const ParseNode &parse_tree) override;

private:
  static const int64_t INCOMING_TABLE_NODE = 0;
  static const int64_t CURRENT_TABLE_NODE = 1;
  static const int64_t DIFF_TABLE_NODE_COUNT = 2;

  int resolve_table_names_(const ParseNode &parse_tree,
                           common::ObString &cur_table, common::ObString &cur_db,
                           common::ObString &inc_table, common::ObString &inc_db);
  int get_schemas_(uint64_t tenant_id,
                   const common::ObString &cur_db, const common::ObString &cur_table,
                   const common::ObString &inc_db, const common::ObString &inc_table,
                   const share::schema::ObTableSchema *&cur_schema,
                   const share::schema::ObTableSchema *&inc_schema);
  int collect_columns_(const share::schema::ObTableSchema &cur_schema,
                       const share::schema::ObTableSchema &inc_schema,
                       ObDiffTableStmt &stmt);
  int build_output_cols_(const share::schema::ObTableSchema &src_schema,
                         ObDiffTableStmt &stmt);

  DISALLOW_COPY_AND_ASSIGN(ObDiffTableResolver);
};

} // namespace sql
} // namespace oceanbase
#endif
