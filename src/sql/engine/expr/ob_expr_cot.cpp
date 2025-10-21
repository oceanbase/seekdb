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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_cot.h"

namespace oceanbase
{
using namespace common;
using namespace common::number;
namespace sql
{

ObExprCot::ObExprCot(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_COT, N_COT, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprCot::~ObExprCot()
{
}

int ObExprCot::calc_result_type1(ObExprResType &type,
                                 ObExprResType &radian,
                                 ObExprTypeCtx &type_ctx) const
{
  return calc_trig_function_result_type1(type, radian, type_ctx);
}

int ObExprCot::calc_cot_expr(const ObExpr &expr, ObEvalCtx &ctx,
                               ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *radian = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, radian))) {
    LOG_WARN("eval radian arg failed", K(ret), K(expr));
  } else if (radian->is_null()) {
    /* radian is already be cast to number type, no need to is_null_oracle */
    res_datum.set_null();
  } else if (ObDoubleType == expr.args_[0]->datum_meta_.type_) {
    const double arg = radian->get_double();
    double tan_out = tan(arg);
    // Check if tan(arg) is approaching 0, when it is too small, consider it as 0
    if (0.0 == tan_out) {
      ret = OB_DOUBLE_OVERFLOW;
      LOG_WARN("tan(x) is zero", K(ret));
    } else {
      double res = 1.0/tan_out;
      if (!std::isfinite(res)) {
        ret = OB_DOUBLE_OVERFLOW;
        LOG_WARN("tan(x) is zero", K(ret));
      } else {
        res_datum.set_double(res);
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObExprCot::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_cot_expr;
  return ret;
}

} /* sql */
} /* oceanbase */
