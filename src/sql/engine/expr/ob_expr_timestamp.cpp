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
#include "sql/engine/expr/ob_expr_timestamp.h"

#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace sql
{
ObExprTimestamp::ObExprTimestamp(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_TIMESTAMP, N_TIMESTAMP, ONE_OR_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprTimestamp::~ObExprTimestamp()
{
}

int ObExprTimestamp::calc_result_typeN(ObExprResType &type,
                                  ObExprResType *types_array,
                                  int64_t param_num,
                                  ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(1 != param_num && 2 != param_num)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid argument count of funtion timestmap", K(ret));
  } else {
    //param will be casted to ObDatetimeType before calculation
    bool use_mysql_compatible = type_ctx.enable_mysql_compatible_dates()
                                && types_array[0].get_type() != ObDateTimeType && 2 != param_num;
    type.set_type(use_mysql_compatible ? ObMySQLDateTimeType : ObDateTimeType);
    types_array[0].set_calc_type(type.get_type());
    if (2 == param_num) {
      types_array[1].set_calc_type(ObTimeType);
    }
    //deduce scale now.
    int16_t scale1 = MIN(types_array[0].get_scale(), MAX_SCALE_FOR_TEMPORAL);
    scale1 = (SCALE_UNKNOWN_YET == scale1) ? MAX_SCALE_FOR_TEMPORAL : scale1;
    int16_t scale2 = 0;
    if (2 == param_num) {
      scale2 = MIN(types_array[1].get_scale(), MAX_SCALE_FOR_TEMPORAL);
      scale2 = (SCALE_UNKNOWN_YET == scale2) ? MAX_SCALE_FOR_TEMPORAL : scale2;
    }
    type.set_scale(MAX(scale1, scale2));
    type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_NULL_ON_WARN);
  }
  return ret;
}

int ObExprTimestamp::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                    ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (1 == rt_expr.arg_cnt_) {
    ObObjType type = rt_expr.args_[0]->datum_meta_.type_;
    CK(ObNullType == type || ObDateTimeType == type || ObMySQLDateTimeType == type);
    if (OB_SUCC(ret)) {
      rt_expr.eval_func_ = ObExprTimestamp::calc_timestamp1;
    }
  } else if (2 == rt_expr.arg_cnt_) {
    ObObjType type1 = rt_expr.args_[0]->datum_meta_.type_;
    ObObjType type2 = rt_expr.args_[1]->datum_meta_.type_;
    CK(ObNullType == type1 || ObDateTimeType == type1);
    CK(ObNullType == type2 || ObTimeType == type2);
    if (OB_SUCC(ret)) {
      rt_expr.eval_func_ = ObExprTimestamp::calc_timestamp2;
    }
  } else {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid argument count of funtion timestmap", K(ret));
  }
  return ret;
}

int ObExprTimestamp::calc_timestamp1(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result)
{
  int ret = OB_SUCCESS;
  ObDatum *param = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, param))) {
    LOG_WARN("calc param failed", K(ret));
  } else if (param->is_null()) {
    result.set_null();
  } else {
    result.set_datetime(param->get_datetime());
  }
  return ret;
}

int ObExprTimestamp::calc_timestamp2(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result)
{
  int ret = OB_SUCCESS;
  ObDatum *datetime = NULL;
  ObDatum *time = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, datetime, time))) {
    LOG_WARN("calc param failed", K(ret));
  } else if (datetime->is_null() || time->is_null()) {
    result.set_null();
  } else {
    result.set_datetime(datetime->get_datetime() + time->get_time());
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
