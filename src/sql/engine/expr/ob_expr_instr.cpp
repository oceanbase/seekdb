/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
