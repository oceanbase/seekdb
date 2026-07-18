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
#include "sql/engine/expr/ob_expr_pi.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

const double ObExprPi::mysql_pi_ = 3.14159265358979323846264338327950288;

ObExprPi::ObExprPi(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_PI, N_PI, 0, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprPi::~ObExprPi()
{
}

int ObExprPi::calc_result_type0(ObExprResType &type, ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  type.set_double();
  type.set_precision(8);
  type.set_scale(6);
  return OB_SUCCESS;
}

int ObExprPi::eval_pi(const ObExpr &expr, ObEvalCtx &ctx,
    ObDatum &expr_datum)
{
  UNUSED(expr);
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  expr_datum.set_double(mysql_pi_);
  return ret;
}

int ObExprPi::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr,
    ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprPi::eval_pi;
  return OB_SUCCESS;
}

} //namespace sql
} //namespace oceanbase
