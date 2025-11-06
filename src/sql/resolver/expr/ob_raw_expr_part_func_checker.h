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

#ifndef OCEANBASE_SQL_RESOLVER_OB_RAW_EXPR_PART_FUNC_CHECKER_H_
#define OCEANBASE_SQL_RESOLVER_OB_RAW_EXPR_PART_FUNC_CHECKER_H_ 1
#include "sql/resolver/expr/ob_raw_expr.h"

namespace oceanbase
{

namespace sql
{
class ObRawExprPartFuncChecker : public ObRawExprVisitor
{
public:
  explicit ObRawExprPartFuncChecker(bool gen_col_check = false, bool accept_charset_function = false, bool interval_check = false)
      : ObRawExprVisitor(), gen_col_check_(gen_col_check), accept_charset_function_(accept_charset_function), interval_check_(interval_check) { }
  virtual ~ObRawExprPartFuncChecker() {}

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
  virtual int visit(ObPlQueryRefRawExpr &expr);
  virtual int visit(ObMatchFunRawExpr &expr);
private:
  // types and constants
  bool gen_col_check_;
  bool accept_charset_function_;
  bool interval_check_;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRawExprPartFuncChecker);
};

} //end of sql
} //end of oceanbase

#endif //OCEANBASE_SQL_RESOLVER_OB_RAW_EXPR_PART_FUNC_CHECKER_H_

