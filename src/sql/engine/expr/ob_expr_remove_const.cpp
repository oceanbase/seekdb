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

#include "ob_expr_remove_const.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprRemoveConst::ObExprRemoveConst(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_REMOVE_CONST, N_REMOVE_CONST, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION,
                         INTERNAL_IN_MYSQL_MODE, INTERNAL_IN_ORACLE_MODE)
{
}

int ObExprRemoveConst::calc_result_type1(ObExprResType &type,
                                           ObExprResType &arg,
                                           common::ObExprTypeCtx &) const
{
  int ret = OB_SUCCESS;
  type = arg;
  return ret;
}

int ObExprRemoveConst::cg_expr(ObExprCGCtx &,
                                 const ObRawExpr &,
                                 ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  CK(1 == rt_expr.arg_cnt_);
  rt_expr.eval_func_ = &ObExprRemoveConst::eval_remove_const;
  return ret;
}

int ObExprRemoveConst::eval_remove_const(const ObExpr &expr,
                                         ObEvalCtx &ctx,
                                         ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *arg = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, arg))) {
    LOG_WARN("expr evaluate parameter failed", K(ret));
  } else {
    expr_datum.set_datum(*arg);
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
