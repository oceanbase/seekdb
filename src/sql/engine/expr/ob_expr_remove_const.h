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

#ifndef OCEANBASE_EXPR_OB_EXPR_REMOVE_CONST_H_
#define OCEANBASE_EXPR_OB_EXPR_REMOVE_CONST_H_

#include "sql/engine/expr/ob_expr_operator.h"
namespace oceanbase
{
namespace sql
{

//
// remove_const() is added above const expr to resolve the const value overwrite problem
// in static typing engine. e.g.:
//
//   update t1 set (c1, c2) = (select 1, 2 from t2);
//
// will be rewrite to:
//   update t1 set (c1, c2) = (select remove_const(1), remove_const(2) from t2);
//
//
// remove_const(1), remove_const(2) will be overwrite when subquery is empty
// instead of const value: 1, 2
//
// see: 
//
class ObExprRemoveConst : public ObFuncExprOperator
{
public:
  explicit ObExprRemoveConst(common::ObIAllocator &alloc);
  virtual ~ObExprRemoveConst() {}

  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &arg,
                                common::ObExprTypeCtx &type_ctx) const override;

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_remove_const(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRemoveConst);
};

} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_EXPR_OB_EXPR_REMOVE_CONST_H_
