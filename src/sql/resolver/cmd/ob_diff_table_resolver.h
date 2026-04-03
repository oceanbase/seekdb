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

#include "sql/resolver/dml/ob_select_resolver.h"
#include "share/schema/ob_table_schema.h"
#include "lib/string/ob_sql_string.h"

namespace oceanbase
{
namespace sql
{

class ObDiffTableResolver : public ObSelectResolver
{
public:
  explicit ObDiffTableResolver(ObResolverParams &params)
    : ObSelectResolver(params)
  {}
  virtual ~ObDiffTableResolver() {}
  virtual int resolve(const ParseNode &parse_tree);

private:
  static const int64_t INCOMING_TABLE_NODE = 0;
  static const int64_t CURRENT_TABLE_NODE = 1;
  static const int64_t DIFF_TABLE_NODE_COUNT = 2;

  int resolve_table_names_(const ParseNode &parse_tree,
                           common::ObString &cur_table_name, common::ObString &cur_db_name,
                           common::ObString &inc_table_name, common::ObString &inc_db_name);
  int get_table_schemas_(const uint64_t tenant_id,
                         const common::ObString &cur_db_name, const common::ObString &cur_table_name,
                         const common::ObString &inc_db_name, const common::ObString &inc_table_name,
                         const share::schema::ObTableSchema *&cur_schema,
                         const share::schema::ObTableSchema *&inc_schema);
  int collect_and_validate_columns_(const share::schema::ObTableSchema *cur_schema,
                                    const share::schema::ObTableSchema *inc_schema,
                                    common::ObIArray<common::ObString> &pk_cols,
                                    common::ObIArray<common::ObString> &val_cols);
  int build_diff_sql_(const common::ObString &cur_db_name, const common::ObString &cur_table_name,
                      const common::ObString &inc_db_name, const common::ObString &inc_table_name,
                      const common::ObIArray<common::ObString> &pk_cols,
                      const common::ObIArray<common::ObString> &val_cols,
                      common::ObSqlString &diff_sql);
  int parse_and_resolve_select_sql(const common::ObString &select_sql);

  DISALLOW_COPY_AND_ASSIGN(ObDiffTableResolver);
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_RESOLVER_CMD_OB_DIFF_TABLE_RESOLVER_H_ */
