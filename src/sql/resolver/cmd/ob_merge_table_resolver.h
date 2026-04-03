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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_MERGE_TABLE_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_CMD_OB_MERGE_TABLE_RESOLVER_H_

#include "sql/resolver/ob_resolver.h"
#include "sql/resolver/ob_stmt_resolver.h"
#include "share/schema/ob_table_schema.h"
#include "lib/string/ob_sql_string.h"

namespace oceanbase
{
namespace sql
{

class ObMergeTableStmt;
class ObMergeTableResolver : public ObStmtResolver
{
public:
  explicit ObMergeTableResolver(ObResolverParams &params)
    : ObStmtResolver(params)
  {}
  virtual ~ObMergeTableResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  static const int64_t INCOMING_TABLE_NODE = 0;
  static const int64_t CURRENT_TABLE_NODE = 1;
  static const int64_t STRATEGY_NODE = 2;
  static const int64_t MERGE_TABLE_NODE_COUNT = 3;

  int resolve_table_names_and_strategy_(const ParseNode &parse_tree, ObMergeTableStmt &stmt,
                                        common::ObString &inc_table_name, common::ObString &inc_db_name,
                                        common::ObString &cur_table_name, common::ObString &cur_db_name);
  int get_table_schemas_(const uint64_t tenant_id,
                         const common::ObString &cur_db_name, const common::ObString &cur_table_name,
                         const common::ObString &inc_db_name, const common::ObString &inc_table_name,
                         const share::schema::ObTableSchema *&cur_schema,
                         const share::schema::ObTableSchema *&inc_schema);
  int collect_and_validate_columns_(const share::schema::ObTableSchema *cur_schema,
                                    const share::schema::ObTableSchema *inc_schema,
                                    common::ObIArray<common::ObString> &pk_cols,
                                    common::ObIArray<common::ObString> &val_cols);
  int build_merge_sqls_(ObMergeTableStmt &stmt,
                        const common::ObString &cur_db_name, const common::ObString &cur_table_name,
                        const common::ObString &inc_db_name, const common::ObString &inc_table_name,
                        const common::ObIArray<common::ObString> &pk_cols,
                        const common::ObIArray<common::ObString> &val_cols);

  static int append_set_clause_(common::ObSqlString &sql,
                                const common::ObIArray<common::ObString> &cols);

  DISALLOW_COPY_AND_ASSIGN(ObMergeTableResolver);
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_RESOLVER_CMD_OB_MERGE_TABLE_RESOLVER_H_ */
