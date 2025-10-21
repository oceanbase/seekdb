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
#include "lib/container/ob_fixed_array.h"
#include "lib/container/ob_fixed_array_iterator.h"
#include "ob_expr_instr.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase
{
namespace sql
{

ObExprInstr::ObExprInstr(ObIAllocator &alloc)
    : ObLocationExprOperator(alloc, T_FUN_SYS_INSTR, N_INSTR, 2, NOT_ROW_DIMENSION)
{
  need_charset_convert_ = false;
}

ObExprInstr::~ObExprInstr() {}

int ObExprInstr::calc_mysql_instr_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(2 != expr.arg_cnt_) || OB_ISNULL(expr.args_) ||
      OB_ISNULL(expr.args_[0]) || OB_ISNULL(expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid expr", K(ret), K(expr));
  } else if (OB_FAIL(ObLocationExprOperator::calc_(expr, *expr.args_[1], *expr.args_[0],
                                                   ctx, res_datum))) {
    LOG_WARN("ObLocationExprOperator::calc_ faied", K(ret));
  }
  return ret;
}

int ObExprInstr::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr,
                                    ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_mysql_instr_expr;
  return OB_SUCCESS;
}

} //namespace sql
} //namespace oceanbase
