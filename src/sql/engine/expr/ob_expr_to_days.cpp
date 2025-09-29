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

#define USING_LOG_PREFIX SQL_EXE
#include "ob_expr_to_days.h"
#include "sql/session/ob_sql_session_info.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprToDays::ObExprToDays(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_TO_DAYS, N_TO_DAYS, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {};

ObExprToDays::~ObExprToDays()
{
}

int ObExprToDays::calc_result_type1(ObExprResType &type,
                                           ObExprResType &date,
                                           common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  type.set_int();
  type.set_scale(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].scale_);
  type.set_precision(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].precision_);
  date.set_calc_type(ObDateType);
  return ret;
}

int calc_todays_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *day_datum = NULL;
  // Here there is no judgment like in the old framework, whether the return value of cast is OB_INVALID_DATE_VALUE
  // If the conversion fails, the result will be set to ZERO_DATE (cast mode default is ZERO_ON_WARN)
  // MySQL's behavior is, when the result is zero date, it returns NULL
  if (OB_FAIL(expr.args_[0]->eval(ctx, day_datum))) {
    LOG_WARN("eval arg failed", K(ret));
  } else if (day_datum->is_null()) {
    res_datum.set_null();
  } else {
    int64_t day_int = day_datum->get_date() + DAYS_FROM_ZERO_TO_BASE;
    if (day_int < 0 || ObTimeConverter::ZERO_DATE == day_int) {
      res_datum.set_null();
    } else {
      res_datum.set_int(day_int);
    }
  }
  return ret;
}

int ObExprToDays::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  if (OB_UNLIKELY(1 != raw_expr.get_param_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("raw_expr should got one child", K(ret), K(raw_expr));
  } else if (ObDateType != rt_expr.args_[0]->datum_meta_.type_) {
    // Type inference part has a calc type set for enum/set, but the new framework cast currently does not fully support enum/set
    // Here we report an error first, subsequent handling of enum/set will be added
    // enum/set->varchar->date
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("param type should be date", K(ret), K(rt_expr));
  } else {
    rt_expr.eval_func_ = calc_todays_expr;
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
