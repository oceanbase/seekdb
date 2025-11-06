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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DML_OB_GROUP_BY_CHECKER_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DML_OB_GROUP_BY_CHECKER_H_
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/dml/ob_select_stmt.h"

namespace oceanbase
{
namespace sql
{

class ObGroupByChecker: public ObRawExprVisitor
{
public:
// if param_store is not null, group by checker will extract plan cache const constraint
  ObGroupByChecker(const ParamStore *param_store,
                   ObIArray<ObRawExpr*> *group_by_exprs,
                   ObIArray<ObRawExpr*> *rollup_exprs = nullptr,
                   ObIArray<ObGroupbyExpr> *groupby_exprs = nullptr)
    : ObRawExprVisitor(),
      level_(-1),
      group_by_exprs_(group_by_exprs),
      rollup_exprs_(rollup_exprs),
      cur_stmts_(),
      skip_expr_(nullptr),
      top_stmt_(nullptr),
      query_ctx_(nullptr),
      has_nested_aggr_(false),
      is_check_order_by_(false),
      dblink_groupby_expr_(NULL),
      only_need_contraints_(false),
      param_store_(param_store)
      
  {}
  virtual ~ObGroupByChecker()
  {}

  /// interface of ObRawExprVisitor
  virtual int visit(ObConstRawExpr &expr);
  virtual int visit(ObExecParamRawExpr &expr);
  virtual int visit(ObVarRawExpr &expr);
  virtual int visit(ObOpPseudoColumnRawExpr &expr);
  virtual int visit(ObQueryRefRawExpr &expr);
  virtual int visit(ObColumnRefRawExpr &expr);
  virtual int visit(ObOpRawExpr &expr);
  virtual int visit(ObCaseOpRawExpr &expr);
  virtual int visit(ObAggFunRawExpr &expr);
  virtual int visit(ObSysFunRawExpr &expr);
  virtual int visit(ObSetOpRawExpr &expr);
  virtual int visit(ObAliasRefRawExpr &expr);
  virtual int visit(ObWinFunRawExpr &expr);
  virtual int visit(ObPseudoColumnRawExpr &expr);
  virtual int visit(ObPlQueryRefRawExpr &expr);
  virtual int visit(ObMatchFunRawExpr &expr);

  // set expr skip
  virtual bool skip_child(ObRawExpr &expr)
  { return skip_expr_ == &expr || expr.is_query_ref_expr(); }
private:
  int64_t level_;
  ObIArray<ObRawExpr*> *group_by_exprs_;
  ObIArray<ObRawExpr*> *rollup_exprs_;
  ObArray<const ObSelectStmt*> cur_stmts_;
  ObRawExpr *skip_expr_;
  const ObSelectStmt *top_stmt_;
  ObQueryCtx *query_ctx_;
  bool has_nested_aggr_;
  bool is_check_order_by_;
  common::ObIArray<ObRawExpr*> *dblink_groupby_expr_;
  // if true, only add constraints for shared exprs which will be replaced in replace_stmt_expr_with_groupby_exprs
  bool only_need_contraints_;
  const ParamStore *param_store_;
private:
  // Top select stmt refers to the current select_stmt calling the group by checker, not the level of the select_stmt
  // Other select_stmt will increment by one level each time it enters a layer, and decrement by one level when checking for exit and exiting
  bool is_top_select_stmt() { return 0 == level_; }
  void set_skip_expr(ObRawExpr *expr) { skip_expr_ = expr; }
  void set_query_ctx(ObQueryCtx *query_ctx) { query_ctx_ = query_ctx; }
  bool find_in_group_by(ObRawExpr &expr);
  bool find_in_rollup(ObRawExpr &expr);
  int belongs_to_check_stmt(ObRawExpr &expr, bool &belongs_to);
  int colref_belongs_to_check_stmt(ObColumnRefRawExpr &expr, bool &belongs_to);
  int check_select_stmt(const ObSelectStmt *ref_stmt);
  int add_pc_const_param_info(ObExprEqualCheckContext &check_context);
  void set_nested_aggr(bool has_nested_aggr) { has_nested_aggr_ = has_nested_aggr; }
  bool has_nested_aggr() { return has_nested_aggr_; }
  void set_check_order_by(bool check_order_by) { is_check_order_by_ = check_order_by; }
  static int check_groupby_valid(ObRawExpr *expr);
  static int check_scalar_groupby_valid(ObRawExpr *expr);
// disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObGroupByChecker);
public:
  void set_level(int64_t level) { level_ = level; }
  // all functions below should only called in resolver
  static int check_group_by(const ParamStore *param_store,
                            ObSelectStmt *ref_stmt,
                            bool has_having_self_column,
                            bool has_group_by_clause,
                            bool only_need_constraints);
  static int check_analytic_function(const ParamStore *param_store,
                                     ObSelectStmt *ref_stmt,
                                     common::ObIArray<ObRawExpr *> &arg_exp_arr,        // equivalent to expressions in the query items
                                     common::ObIArray<ObRawExpr *> &partition_exp_arr); // equivalent to group by items
};

}
}
#endif
