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
#include "ob_expr_log2.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprLog2::ObExprLog2(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_LOG_TWO, N_LOG2, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprLog2::~ObExprLog2()
{
}

int ObExprLog2::calc_result_type1(ObExprResType &type,
                                  ObExprResType &type1,
                                  common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (NOT_ROW_DIMENSION != row_dimension_ || ObMaxType == type1.get_type()) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else {
    type.set_double();
    type1.set_calc_type(ObDoubleType);
    ObExprOperator::calc_result_flag1(type, type1);
  }
  return ret;
}

int calc_log2_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *arg = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, arg))) {
    LOG_WARN("eval arg failed", K(ret), K(expr));
  } else if (arg->is_null()) {
    res_datum.set_null();
  } else {
    double val = arg->get_double();
    if (val <= 0) {
      LOG_USER_WARN(OB_EER_INVALID_ARGUMENT_FOR_LOGARITHM);
      res_datum.set_null();
    } else {
      res_datum.set_double(std::log(val) / std::log(2));
    }
  }
  return ret;
}

int ObExprLog2::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (OB_UNLIKELY(1 != rt_expr.arg_cnt_) ||
      (ObDoubleType != rt_expr.args_[0]->datum_meta_.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid arg_cnt_ or arg res type is invalid", K(ret), K(rt_expr));
  } else {
    rt_expr.eval_func_ = calc_log2_expr;
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
