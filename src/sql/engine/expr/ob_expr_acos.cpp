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
#include "ob_expr_acos.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprAcos::ObExprAcos(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ACOS, N_ACOS, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}

ObExprAcos::~ObExprAcos()
{
}

int ObExprAcos::calc_result_type1(ObExprResType &type,
                                  ObExprResType &type1,
                                  common::ObExprTypeCtx &type_ctx) const
{
  return calc_trig_function_result_type1(type, type1, type_ctx);
}

DEF_CALC_TRIGONOMETRIC_EXPR(acos, arg > 1 || arg < -1, OB_ERR_ARGUMENT_OUT_OF_RANGE);

int ObExprAcos::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_acos_expr;
  return ret;
}
} //namespace sql
} //namespace oceanbase
