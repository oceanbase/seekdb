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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_FROM_DAYS_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_FROM_DAYS_H_

#include "sql/engine/expr/ob_expr_operator.h"
namespace oceanbase
{
namespace sql
{
class ObExprFromDays : public ObFuncExprOperator
{
public:
  explicit  ObExprFromDays(common::ObIAllocator &alloc);
  virtual ~ObExprFromDays();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &date,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_fromdays(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int calc_fromdays_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const EvalBound &bound);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprFromDays);
};

inline int ObExprFromDays::calc_result_type1(ObExprResType &type, ObExprResType &date, common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  UNUSED(date);
  type.set_date();
  type.set_scale(common::DEFAULT_SCALE_FOR_DATE);
  //set calc type
  date.set_calc_type(common::ObInt32Type);
  return common::OB_SUCCESS;
}
}
}


#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_FROM_DAYS_H_ */
