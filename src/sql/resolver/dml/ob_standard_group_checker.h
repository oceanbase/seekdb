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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_DML_OB_STANDARD_GROUP_CHECKER_H_
#define OCEANBASE_SRC_SQL_RESOLVER_DML_OB_STANDARD_GROUP_CHECKER_H_
#include "lib/hash/ob_hashset.h"
#include "lib/container/ob_array.h"
#include "sql/resolver/expr/ob_raw_expr.h"
namespace oceanbase
{
namespace sql
{
class ObSelectStmt;
class ObFdItemFactory;
class ObStandardGroupChecker
{
public:
  ObStandardGroupChecker()
    : has_group_(false),
      is_scalar_aggr_(true) {}
  ~ObStandardGroupChecker() {}
  friend class ObStandardGroupVisitor;

  void set_has_group(bool has_group) { has_group_ = has_group; }
  int init(const ObSelectStmt* select_stmt,
           ObSQLSessionInfo *session_info,
           ObSchemaChecker *schema_checker);
  int add_group_by_expr(ObRawExpr *expr);
  int add_unsettled_expr(ObRawExpr *expr);
  int check_only_full_group_by();
private:
  int deduce_settled_exprs(ObArenaAllocator *alloc,
                           ObRawExprFactory *expr_factory,
                           ObFdItemFactory *fd_item_factory);
  int expr_exists_in_group_by(ObRawExpr *expr, bool &is_existed);
  int check_unsettled_column(const ObColumnRefRawExpr *unsettled_column);
private:
  const ObSelectStmt *select_stmt_;
  ObSQLSessionInfo *session_info_;
  ObSchemaChecker *schema_checker_;
  bool has_group_;
  bool is_scalar_aggr_;
  //all unsettled exprs that needed be check whether meet the only full group by semantic constraints in current stmt
  common::ObArray<ObRawExpr*> unsettled_exprs_;
  //all exprs in the group by scope of current stmt
  common::ObArray<ObRawExpr*> group_by_exprs_;
  //exprs that meet the only full group by semantic constraints in current stmt
  common::ObArray<ObRawExpr*> settled_exprs_;
};

class ObStandardGroupVisitor: public ObRawExprVisitor
{
public:
  ObStandardGroupVisitor(ObStandardGroupChecker* checker,
                         bool is_in_subquery = false) 
    : checker_(checker),
      is_in_subquery_(is_in_subquery),
      skip_expr_(NULL) {}
  virtual ~ObStandardGroupVisitor() {}
  /// interface of ObRawExprVisitor
  virtual int visit(ObColumnRefRawExpr &expr);
  virtual int visit(ObSysFunRawExpr &expr);
  virtual int visit(ObAggFunRawExpr &expr);
  virtual int visit(ObExecParamRawExpr &expr);
  virtual int visit(ObQueryRefRawExpr &expr);
  virtual int visit(ObConstRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObVarRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObOpPseudoColumnRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObOpRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObCaseOpRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObSetOpRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObAliasRefRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObWinFunRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObPseudoColumnRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObPlQueryRefRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual int visit(ObMatchFunRawExpr &expr) { UNUSED(expr); return OB_SUCCESS; }
  virtual bool skip_child(ObRawExpr &expr);
private:
  ObStandardGroupChecker *checker_;
  bool is_in_subquery_;
  ObRawExpr *skip_expr_;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObStandardGroupVisitor);
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_DML_OB_STANDARD_GROUP_CHECKER_H_ */
