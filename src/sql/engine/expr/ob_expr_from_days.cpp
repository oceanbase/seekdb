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

#define USING_LOG_PREFIX SQL_EXE
#include "ob_expr_from_days.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprFromDays::ObExprFromDays(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_FROM_DAYS, N_FROM_DAYS, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
};

ObExprFromDays::~ObExprFromDays()
{
}

int ObExprFromDays::cg_expr(ObExprCGCtx &op_cg_ctx,
                              const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  if (rt_expr.arg_cnt_ != 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fromdays expr should have one param", K(ret), K(rt_expr.arg_cnt_));
  } else if (OB_ISNULL(rt_expr.args_) || OB_ISNULL(rt_expr.args_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of fromdays expr is null", K(ret), K(rt_expr.args_));
  } else {
    CK(ObInt32Type == rt_expr.args_[0]->datum_meta_.type_);
    rt_expr.eval_func_ = ObExprFromDays::calc_fromdays;
  }
  return ret;
}

int ObExprFromDays::calc_fromdays(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *param_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, param_datum))) {
  } else if (OB_UNLIKELY(param_datum->is_null())) {
    expr_datum.set_null();
  } else {
    // max: 9999-12-31 min:0000-00-00
    int32_t value = param_datum->get_int32();
    if (value >= MIN_DAYS_OF_DATE
        && value <= MAX_DAYS_OF_DATE) {
      expr_datum.set_date(value - DAYS_FROM_ZERO_TO_BASE);
    } else {
      expr_datum.set_date(ObTimeConverter::ZERO_DATE);
    }
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
