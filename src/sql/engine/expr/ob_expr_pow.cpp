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
#include "sql/engine/expr/ob_expr_pow.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace oceanbase::common;

namespace sql
{

ObExprPow::ObExprPow(ObIAllocator &alloc)
    : ObExprOperator(alloc, T_OP_POW, N_POW, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

int ObExprPow::calc_result_type2(ObExprResType &type,
                                 ObExprResType &type1,
                                 ObExprResType &type2,
                                 ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  UNUSED(type2);
  if (!lib::is_mysql_mode()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pow is only for mysql mode", K(ret));
  } else if (NOT_ROW_DIMENSION == row_dimension_) {
    if (ObMaxType == type1.get_type()) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
    } else {
      type.set_double();
      type1.set_calc_type(ObDoubleType);
      type2.set_calc_type(ObDoubleType);
      // pow donot return error code in mysql mode
      //  eg: insert into t1 select pow('a', 2);
      type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_WARN_ON_FAIL);
    }
    ObExprOperator::calc_result_flag2(type, type1, type2);
    //just keep enumset as origin
  } else {
    ret = OB_ERR_INVALID_TYPE_FOR_OP; // arithmetic not support row
  }
  return ret;
}


int ObExprPow::safe_set_double(ObDatum &datum, double value)
{
  int ret = OB_SUCCESS;
  if (lib::is_mysql_mode() &&
         (isinfl(value) || isnan(static_cast<float>(value)))) {
    ret = OB_OPERATE_OVERFLOW;
  } else {
    datum.set_double(value);
  }
  return ret;
}

int ObExprPow::calc_pow_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  // pow(base, exp)
  ObDatum *base = NULL;
  ObDatum *exp = NULL;
  // Verified that when the first parameter is NULL, MySQL still calculates the second parameter
  int ret_base = expr.args_[0]->eval(ctx, base);
  int ret_exp  = expr.args_[1]->eval(ctx, exp);
  if (OB_SUCCESS == ret_base && OB_SUCCESS == ret_exp) {
    if (base->is_null() || exp->is_null()) {
      res_datum.set_null();
    } else {
      ret = ObExprPow::safe_set_double(res_datum,
          std::pow(base->get_double(), exp->get_double()));
    }
  } else {
    ret = (OB_SUCCESS == ret_base) ? ret_exp : ret_base;
  }
  return ret;
}

int ObExprPow::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  CK (2 == raw_expr.get_param_count());
  OX (rt_expr.eval_func_ = calc_pow_expr);
  return ret;
}

} // namespace sql
} // namespace oceanbase
