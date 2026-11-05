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

#include "sql/engine/expr/ob_expr_stmt_id.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{
ObExprStmtId::ObExprStmtId(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_STMT_ID, "stmt_id", 0, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION,
                         INTERNAL_IN_MYSQL_MODE, INTERNAL_IN_ORACLE_MODE)
{
}

int ObExprStmtId::calc_result_type0(ObExprResType &type,
                                    ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  type.set_int();
  return OB_SUCCESS;
}

int ObExprStmtId::eval_stmt_id(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  UNUSED(expr);
  ObPhysicalPlanCtx *plan_ctx = ctx.exec_ctx_.get_physical_plan_ctx();

  if (OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("plan_ctx is null", K(ret));
  } else {
    res.set_int(plan_ctx->get_cur_stmt_id());
  }

  return ret;
}

int ObExprStmtId::cg_expr(ObExprCGCtx &expr_cg_ctx,
                          const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_stmt_id;
  return ret;
}

} //end sql
} //end oceanbase
