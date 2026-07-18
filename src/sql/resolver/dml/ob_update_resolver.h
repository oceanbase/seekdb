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

#ifndef OCEANBASE_SQL_RESOLVER_DML_OB_UPDATE_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_DML_OB_UPDATE_RESOLVER_H_

#include "lib/hash/ob_placement_hashset.h"
#include "sql/resolver/dml/ob_del_upd_resolver.h"
#include "sql/resolver/dml/ob_update_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObUpdateResolver : public ObDelUpdResolver
{
public:
  static const int64_t WITH_MYSQL = 0;         /*10. with_clause node in mysql mode*/
  static const int64_t TABLE = 1;              /* 0. table node */
  static const int64_t UPDATE_LIST = 2;       /* 1. update list */
  static const int64_t WHERE = 3;              /* 2. where node */
  static const int64_t ORDER_BY = 4;        /* 3. order by node */
  static const int64_t LIMIT = 5;              /* 4. limit node */
  static const int64_t WHEN = 6;                /* 5. when node */
  static const int64_t HINT = 7;                /* 6. hint node */
  static const int64_t IGNORE = 8;               /*7. ignore node */
  static const int64_t RETURNING = 9;           /*8. returning node */
  static const int64_t ERRORLOGGING = 10;           /*9. error_logging node */
  
public:
  explicit ObUpdateResolver(ObResolverParams &params);
  virtual ~ObUpdateResolver();

  virtual int resolve(const ParseNode &parse_tree);
  inline ObUpdateStmt *get_update_stmt() { return static_cast<ObUpdateStmt*>(stmt_); }
private:
  int resolve_table_list(const ParseNode &parse_tree);
  int generate_update_table_info(ObTableAssignment &table_assign);
  int check_multi_update_table_conflict();
  int check_join_update_conflict();
  int is_join_table_update(const ObDMLStmt *stmt, bool &is_multi_table);
  int check_view_updatable();
  int try_expand_returning_exprs();
  int try_add_remove_const_expr_for_assignments();
  bool is_parent_col_self_ref_fk(uint64_t parent_col_id,
                                 const common::ObIArray<share::schema::ObForeignKeyInfo> &fk_infos);

  int check_safe_update_mode(ObUpdateStmt *update_stmt);
  int resolve_update_constraints();
  int generate_batched_stmt_info();
};

} // namespace sql
} // namespace oceanbase
#endif /* OCEANBASE_SQL_RESOLVER_DML_OB_UPDATE_RESOLVER_H_ */
