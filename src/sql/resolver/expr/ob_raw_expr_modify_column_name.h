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

#ifndef _OB_RAW_EXPR_MODIFY_COLUMN_NAME_H
#define _OB_RAW_EXPR_MODIFY_COLUMN_NAME_H 1

#include "lib/string/ob_string.h"
#include "lib/worker.h"
#include "sql/resolver/expr/ob_raw_expr.h"

namespace oceanbase
{
namespace sql
{
class ObRawExprModifyColumnName : public ObRawExprVisitor
{
public:
  ObRawExprModifyColumnName(
      common::ObString new_column_name,
      common::ObString orig_column_name,
      const lib::Worker::CompatMode compat_mode)
    : ObRawExprVisitor() {
    orig_column_name_ = orig_column_name;
    new_column_name_ = new_column_name;
    compat_mode_ = compat_mode;
  }
  virtual ~ObRawExprModifyColumnName() {}

  int modifyColumnName(ObRawExpr &expr);

  // interface of ObRawExprVisitor
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

private:
  DISALLOW_COPY_AND_ASSIGN(ObRawExprModifyColumnName);
  common::ObString orig_column_name_;
  common::ObString new_column_name_;
  lib::Worker::CompatMode compat_mode_;
};
} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_RAW_EXPR_MODIFY_COLUMN_NAME_H */
