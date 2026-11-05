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

#include "sql/engine/expr/ob_expr_cos.h"
#include "sql/engine/expr/sin_asin_compat.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace common::number;
namespace sql
{

ObExprCos::ObExprCos(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_COS, N_COS, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprCos::~ObExprCos()
{
}

int ObExprCos::calc_result_type1(ObExprResType &type,
                                 ObExprResType &radian,
                                 ObExprTypeCtx &type_ctx) const
{
  return calc_trig_function_result_type1(type, radian, type_ctx);
}

int calc_cos_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *radian = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, radian))) {
    LOG_WARN("eval radian arg failed", K(ret), K(expr));
  } else if (radian->is_null()) {
    res_datum.set_null();
  } else if (ObNumberType == expr.args_[0]->datum_meta_.type_) {
    number::ObNumber res_nmb;
    number::ObNumber radian_nmb(radian->get_number());
    ObEvalCtx::TempAllocGuard alloc_guard(ctx);
    if (OB_FAIL(radian_nmb.cos(res_nmb, alloc_guard.get_allocator()))) {
      LOG_WARN("calc expr failed", K(ret), K(radian_nmb), K(expr));
    } else {
      res_datum.set_number(res_nmb);
    }
  } else if (ObDoubleType == expr.args_[0]->datum_meta_.type_) {
    const double arg = radian->get_double();
    res_datum.set_double(math_compat::cos(arg));
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObExprCos::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_cos_expr;
  if (ObDoubleType != rt_expr.args_[0]->datum_meta_.type_
      && ObNumberType != rt_expr.args_[0]->datum_meta_.type_) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

} /* sql */
} /* oceanbase */
