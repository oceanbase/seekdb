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

#define USING_LOG_PREFIX  SQL_ENG

#include "sql/engine/expr/ob_expr_date_diff.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprDateDiff::ObExprDateDiff(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_DATE_DIFF, N_DATE_DIFF, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprDateDiff::~ObExprDateDiff()
{
}


int ObExprDateDiff::cg_expr(ObExprCGCtx &op_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprDateDiff::eval_date_diff;

  return ret;
}

int ObExprDateDiff::eval_date_diff(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *left = NULL;
  ObDatum *right = NULL;
  int32_t date_left = 0;
  int32_t date_right = 0;
  if (OB_FAIL(expr.args_[0]->eval(ctx, left))
      || OB_FAIL(expr.args_[1]->eval(ctx, right))) {
    LOG_WARN("fail to eval conv", K(ret), K(expr));
  } else if (left->is_null() || right->is_null()) {
    res_datum.set_null();
  } else {
    if (ob_is_mysql_date_tc(expr.args_[0]->datum_meta_.type_)) {
      date_left = ObTimeConverter::calc_date(left->get_mysql_date());
      date_right = ObTimeConverter::calc_date(right->get_mysql_date());
    } else {
      date_left = left->get_date();
      date_right = right->get_date();
    }
    if (OB_UNLIKELY(ObTimeConverter::ZERO_DATE == date_left ||
                      ObTimeConverter::ZERO_DATE == date_right)) {
      res_datum.set_null();
    } else {
      res_datum.set_int(date_left - date_right);
    }
  }

  return ret;
}

} //namespace sql
} //namespace oceanbase
