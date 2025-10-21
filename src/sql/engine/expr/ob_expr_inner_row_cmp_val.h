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

#ifndef _OB_EXPR_INNER_ROW_CMP_VAL_H_
#define _OB_EXPR_INNER_ROW_CMP_VAL_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprInnerRowCmpVal : public ObFuncExprOperator
{
public:
  explicit ObExprInnerRowCmpVal(common::ObIAllocator &alloc);
  virtual ~ObExprInnerRowCmpVal() {};

  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprResType &type3,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_inner_row_cmp_val(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);

private:
  //disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprInnerRowCmpVal);
};
}
}
#endif  /* _OB_EXPR_INNER_ROW_CMP_VAL_H_ */
