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
#include "ob_expr_sqrt.h"
#include "sql/session/ob_sql_session_info.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprSqrt::ObExprSqrt(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_SQRT, N_SQRT, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprSqrt::~ObExprSqrt()
{
}

int ObExprSqrt::calc_result_type1(ObExprResType &type,
                                  ObExprResType &type1,
                                  common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (NOT_ROW_DIMENSION != row_dimension_ || ObMaxType == type1.get_type()) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else {
    type.set_double();
  }
  type1.set_calc_type(type.get_type());
  ObExprOperator::calc_result_flag1(type, type1);
  return ret;
}

int calc_sqrt_expr_mysql(const ObExpr &expr, ObEvalCtx &ctx,
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
    if (val < 0) {
      res_datum.set_null();
    } else {
      res_datum.set_double(std::sqrt(val));
    }
  }
  return ret;
}

int calc_sqrt_expr_mysql_in_batch(const ObExpr &expr,
                                  ObEvalCtx &ctx,
                                  const ObBitVector &skip,
                                  const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
    LOG_WARN("vectorized evaluate failed", K(ret), K(expr));
  } else {
    ObBitVector &eval_flag = expr.get_evaluated_flags(ctx);
    ObDatumVector arg_datums = expr.args_[0]->locate_expr_datumvector(ctx);
    ObDatum *res_datums = expr.locate_batch_datums(ctx);
    for (int64_t i = 0; i < batch_size; ++i) {
      if (!eval_flag.contain(i) && !skip.contain(i)) {
        if (arg_datums.at(i)->is_null() || arg_datums.at(i)->get_double() < 0) {
          res_datums[i].set_null();
        } else {
          res_datums[i].set_double(std::sqrt(arg_datums.at(i)->get_double()));
        }
        eval_flag.set(i);
      }
    }
  }
  return ret;
}

int ObExprSqrt::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  rt_expr.extra_ = raw_expr.get_aggr_type();
  if (OB_UNLIKELY(1 != rt_expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid arg_cnt_ of expr", K(ret), K(rt_expr));
  } else {
    ObObjType arg_res_type = rt_expr.args_[0]->datum_meta_.type_;
    if (ObDoubleType == arg_res_type) {
      rt_expr.eval_func_ = calc_sqrt_expr_mysql;
      rt_expr.eval_batch_func_ = calc_sqrt_expr_mysql_in_batch;
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("arg_res_type must be double in mysql mode", K(ret), K(arg_res_type));
    }
  }
  return ret;
}
} //namespace sql
} //namespace oceanbase
