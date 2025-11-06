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

#ifndef _OB_EXPR_ANY_VALUE_H_
#define _OB_EXPR_ANY_VALUE_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprAnyValue : public ObFuncExprOperator
{
public:
  explicit ObExprAnyValue(common::ObIAllocator &alloc);
  virtual ~ObExprAnyValue();

  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &arg,
                                common::ObExprTypeCtx &type_ctx) const override;

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
                      
  static int eval_any_value(const ObExpr &expr,
                      ObEvalCtx &ctx,
                      ObDatum &expr_datum);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprAnyValue) const;
};
}
}
#endif  /* _OB_EXPR_ANY_VALUE_H_ */

// select any_value();
