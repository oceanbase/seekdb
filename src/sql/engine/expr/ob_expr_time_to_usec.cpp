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
#include "sql/engine/expr/ob_expr_time_to_usec.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprTimeToUsec::ObExprTimeToUsec(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_TIME_TO_USEC, N_TIME_TO_USEC, 1, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{}

ObExprTimeToUsec::~ObExprTimeToUsec()
{
}


int ObExprTimeToUsec::calc_result_type1(ObExprResType &type,
                                        ObExprResType &date,
                                        common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(!date.is_varchar()
      && !date.is_temporal_type()
      && !date.is_null())) {
    ret = common::OB_INVALID_ARGUMENT_FOR_TIME_TO_USEC;

    LOG_WARN("invalid type", K(date.get_type()));
  } else {
    type.set_int();
    type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
    type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
    //set calc type
    date.set_calc_type(ObTimestampType);
  }
  return ret;
}

int calc_time_to_usec_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                  ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *arg_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, arg_datum))) {
    LOG_WARN("eval arg failed", K(ret));
  } else if (arg_datum->is_null()) {
    res_datum.set_null();
  } else {
    res_datum.set_int(arg_datum->get_timestamp());
  }
  return ret;
}

int ObExprTimeToUsec::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_time_to_usec_expr;
  return ret;
}

}
}
