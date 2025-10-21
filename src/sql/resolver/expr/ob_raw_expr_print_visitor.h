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

#ifndef _OB_RAW_EXPR_PRINT_VISITOR_H
#define _OB_RAW_EXPR_PRINT_VISITOR_H
#include "lib/utility/ob_print_utils.h"
#include "sql/resolver/expr/ob_raw_expr.h"
namespace oceanbase
{
namespace sql
{
class ObRawExprPrintVisitor: public ObRawExprVisitor
{
public:
  explicit ObRawExprPrintVisitor(ObRawExpr &expr_root);
  virtual ~ObRawExprPrintVisitor();

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
  virtual int visit(ObPlQueryRefRawExpr &expr);
  virtual int visit(ObMatchFunRawExpr &expr);
  int64_t to_string(char* buf, const int64_t buf_len) const;
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRawExprPrintVisitor);
  // function members
private:
  // data members
  ObRawExpr &expr_root_;
  mutable char* buf_;
  mutable int64_t buf_len_;
  mutable int64_t pos_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_RAW_EXPR_PRINT_VISITOR_H */
