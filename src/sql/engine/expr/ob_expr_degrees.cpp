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
#include "sql/engine/expr/ob_expr_degrees.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase
{
namespace sql
{

const double ObExprDegrees::degrees_ratio_ = 180.0/std::acos(-1);

ObExprDegrees::ObExprDegrees(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_DEGREES, N_DEGREES, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprDegrees::~ObExprDegrees()
{
}

int ObExprDegrees::calc_result_type1(ObExprResType &type,
                                    ObExprResType &radian,
                                    ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (NOT_ROW_DIMENSION != row_dimension_ || ObMaxType == radian.get_type()) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else {
    type.set_double();
    radian.set_calc_type(ObDoubleType);
    ObExprOperator::calc_result_flag1(type, radian);
  }
  return ret;
}

int ObExprDegrees::calc_degrees_expr(const ObExpr &expr, ObEvalCtx &ctx,
                      ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum * radian = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, radian))) {
    LOG_WARN("eval radian arg failed", K(ret), K(expr));
  } else if (radian->is_null()) {
    res_datum.set_null();
  } else {
    const double val = radian->get_double();
    // cal result;
    res_datum.set_double(val * degrees_ratio_);
  }
  return ret;
}

int ObExprDegrees::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (OB_UNLIKELY(1 != rt_expr.arg_cnt_) ||
      (ObDoubleType != rt_expr.args_[0]->datum_meta_.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid arg_cnt_ or res type is invalid", K(ret), K(rt_expr));
  } else {
    rt_expr.eval_func_ = calc_degrees_expr;
  }
  return ret;
}

}
}
