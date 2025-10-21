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

#ifndef _OB_RAW_EXPR_ANALYZER_H
#define _OB_RAW_EXPR_ANALYZER_H 1
#include "sql/resolver/expr/ob_raw_expr.h"
namespace oceanbase
{
namespace sql
{
class ObRawExprInfoExtractor : public ObRawExprVisitor
{
public:
ObRawExprInfoExtractor()
    : ObRawExprVisitor() {}
  virtual ~ObRawExprInfoExtractor() {}

  int analyze(ObRawExpr &expr);

  /// interface of ObRawExprVisitor
  virtual int visit(ObConstRawExpr &expr);
  virtual int visit(ObVarRawExpr &expr);
  virtual int visit(ObOpPseudoColumnRawExpr &expr);
  virtual int visit(ObExecParamRawExpr &expr);
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
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRawExprInfoExtractor);
  // function members
  int visit_interm_node(ObRawExpr &expr);
  int visit_subquery_node(ObOpRawExpr &expr);
  int visit_left_param(ObRawExpr &expr);
  // Need to modify the root node information based on the operator on the right, so the topmost root node needs to be passed in
  int visit_right_param(ObOpRawExpr &expr);
  int clear_info(ObRawExpr &expr);
  int pull_info(ObRawExpr &expr);
  int add_const(ObRawExpr &expr);
  int add_deterministic(ObRawExpr &expr);

  ObItemType get_subquery_comparison_type(ObItemType cmp_type) const;
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_RAW_EXPR_ANALYZER_H */
