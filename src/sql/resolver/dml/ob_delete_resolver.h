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

#ifndef OCEANBASE_SQL_RESOLVER_DML_OB_DELETE_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_DML_OB_DELETE_RESOLVER_H_
#include "sql/resolver/dml/ob_del_upd_resolver.h"
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/dml/ob_delete_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObDeleteResolver : public ObDelUpdResolver
{
public:
public:
  explicit ObDeleteResolver(ObResolverParams &params);
  virtual ~ObDeleteResolver();
  virtual int resolve(const peg::Node &query);
  ObDeleteStmt *get_delete_stmt() { return static_cast<ObDeleteStmt*>(stmt_); }
private:
  int resolve_table_list(const peg::Node &query, bool &is_multi_table_delete);
  int generate_delete_table_info(const TableItem &table_item);
  int check_multi_delete_table_conflict();
  // MySQL multi-table DELETE target table search rule.
  int find_delete_table_with_mysql_rule(const common::ObString &db_name,
                                        const common::ObString &table_name,
                                        TableItem *&table_item);
  int check_view_deletable();
  int check_safe_update_mode(ObDeleteStmt *delete_stmt, bool is_multi_table_delete);
  common::ObSEArray<TableItem*, 2, common::ModulePageAllocator, true> delete_tables_;
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_DML_OB_DELETE_RESOLVER_H_ */
