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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DML_OB_INLIST_RESOLVER_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DML_OB_INLIST_RESOLVER_H_
#include "sql/resolver/expr/ob_raw_expr.h"

namespace oceanbase
{
namespace sql
{
class ObValuesTableDef;
class ObDMLResolver;
class ObQueryRefRawExpr;
class ParameterizationResult;
struct DistinctObjMeta;
struct InListRewriteInfo;

struct InListRows
{
  // Original membership nodes, borrowed until deferred expression resolution finishes.
  common::ObSEArray<const peg::Node *, 4> segments_;
  int64_t row_count() const;
  int get_copy_assign_ret() const { return segments_.get_copy_assign_ret(); }
};

struct InListGroup
{
  explicit InListGroup(const peg::Node *node = nullptr) : node_(node) {}
  const peg::Node *node_;
  // Empty when this operand was not combined with another membership predicate.
  InListRows rows_;
  int get_copy_assign_ret() const { return rows_.get_copy_assign_ret(); }
};

struct ObInListInfo
{
  ObInListInfo() : in_list_expr_(NULL), row_cnt_(0), column_cnt_(0), is_question_mark_(true) {}
  TO_STRING_KV(K_(in_list_expr));
  InListRows rows_;
  ObQueryRefRawExpr *in_list_expr_;
  int64_t row_cnt_;
  int64_t column_cnt_;
  bool is_question_mark_;
  int get_copy_assign_ret() const { return rows_.get_copy_assign_ret(); }
};

struct ObInListsResolverHelper 
{
  ObIAllocator &alloc_;
  const ParamStore *param_store_;
  const ParameterizationResult *parameterization_;
  ObCollationType connect_collation_;
  ObCollationType nchar_collation_;
  ObCollationType server_collation_;
  bool enable_decimal_int_;
  bool is_prepare_stmt_;

  ObInListsResolverHelper(ObIAllocator &alloc, 
                          const ParamStore *param_store, 
                          const ParameterizationResult *parameter_bindings,
                          ObCollationType connect_collation,
                          ObCollationType nchar_collation, 
                          ObCollationType server_collation, 
                          bool enable_decimal_int,
                          bool is_prepare_stmt)
  : alloc_(alloc), 
    param_store_(param_store), 
    parameterization_(parameter_bindings),
    connect_collation_(connect_collation),
    nchar_collation_(nchar_collation), 
    server_collation_(server_collation), 
    enable_decimal_int_(enable_decimal_int),
    is_prepare_stmt_(is_prepare_stmt) {}

  TO_STRING_KV(K_(connect_collation), 
               K_(nchar_collation), 
               K_(server_collation), 
               K_(enable_decimal_int), 
               K_(is_prepare_stmt));
};

class ObInListResolver
{
public:
  ObInListResolver(ObDMLResolver *cur_resolver)
    : cur_resolver_(cur_resolver) {}
  virtual ~ObInListResolver() {}
  static int check_inlist_rewrite_enable(const InListRows &rows,
                                         const ObItemType op_type,
                                         const ObRawExpr &left_expr,
                                         const ObStmtScope &scope,
                                         const bool is_root_condition,
                                         const bool is_need_print,
                                         const bool is_in_pl_prepare,
                                         const ObSQLSessionInfo *session_info,
                                         const ParamStore *param_store,
                                         const ParameterizationResult *parameter_bindings,
                                         const ObStmt *stmt,
                                         common::ObIAllocator &alloc,
                                         bool &is_question_mark,
                                         bool &is_enable);
  static int group_inlists(ObExprResolveContext &resolve_ctx,
                          bool is_root_condition,
                          const peg::Node &root,
                          common::ObIArray<InListGroup> &groups);
  int resolve_inlist(ObInListInfo &inlist_infos);
private:
  static int get_inlist_rewrite_info(const InListRows &rows,
                                     const int64_t column_cnt,
                                     int64_t col_idx,
                                     ObInListsResolverHelper &helper,
                                     InListRewriteInfo &rewrite_info);
  static int check_can_merge_inlists(const peg::Node *last_in_node,
                                     const peg::Node *cur_in_node,
                                     ObInListsResolverHelper &helper,
                                     InListRewriteInfo &last_info,
                                     bool &can_merge);
  int resolve_values_table_from_inlist(const InListRows &rows,
                                       const int64_t column_cnt,
                                       const int64_t row_cnt,
                                       const bool is_question_mark,
                                       const bool is_prepare_stmt,
                                       const bool is_called_in_sql,
                                       const ParamStore *param_store,
                                       ObSQLSessionInfo *session_info,
                                       ObIAllocator *allocator,
                                       ObValuesTableDef *&table_def);
  int resolve_subquery_from_values_table(ObStmtFactory *stmt_factory,
                                         ObSQLSessionInfo *session_info,
                                         ObIAllocator *allocator,
                                         ObQueryCtx *query_ctx,
                                         ObRawExprFactory *expr_factory,
                                         ObValuesTableDef *table_def,
                                         const bool is_prepare_stmt,
                                         const int64_t column_cnt,
                                         ObQueryRefRawExpr *query_ref);
  int resolve_access_param_values_table(const InListRows &rows,
                                        const int64_t column_cnt,
                                        const ParamStore *param_store,
                                        ObSQLSessionInfo *session_info,
                                        ObIAllocator *allocator,
                                        const bool is_called_in_sql,
                                        ObValuesTableDef &table_def);
  int resolve_access_obj_values_table(const InListRows &rows,
                                      const int64_t column_cnt,
                                      ObSQLSessionInfo *session_info,
                                      ObIAllocator *allocator,
                                      const bool is_prepare_stage,
                                      const bool is_called_in_sql,
                                      ObValuesTableDef &table_def);
private:
  ObDMLResolver *cur_resolver_;
};

}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_DML_OB_INLIST_RESOLVER_H_ */
