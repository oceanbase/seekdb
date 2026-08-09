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
#include "sql/engine/expr/ob_expr_convert_tz.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprConvertTZ::ObExprConvertTZ(common::ObIAllocator &alloc):
ObFuncExprOperator(alloc, T_FUN_SYS_CONVERT_TZ, "convert_TZ", 3, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION){
}

int ObExprConvertTZ::calc_result_type3(ObExprResType &type,
                                        ObExprResType &input1,
                                        ObExprResType &input2,
                                        ObExprResType &input3,
                                        common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(session = type_ctx.get_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else {
    int16_t scale1 = MIN(input1.get_scale(), MAX_SCALE_FOR_TEMPORAL);
    scale1 = (SCALE_UNKNOWN_YET == scale1) ? MAX_SCALE_FOR_TEMPORAL : scale1;
    type.set_scale(scale1);
    type.set_datetime();
    input1.set_calc_type(ObDateTimeType);
    input2.set_calc_type(ObVarcharType);
    input3.set_calc_type(ObVarcharType);
    input2.set_calc_collation_ascii_compatible();
    input3.set_calc_collation_ascii_compatible();
  }
  return ret;
}

int ObExprConvertTZ::calc_convert_tz(int64_t timestamp_data,
                                    const ObString &tz_str_s,//source time zone (input2)
                                    const ObString &tz_str_d,//destination time zone (input3)
                                    ObSQLSessionInfo *session,
                                    ObDatum &result)
{
  int ret = OB_SUCCESS;
  int32_t offset_couple =0;
  if (OB_FAIL(get_offset_by_couple_tz(timestamp_data, offset_couple, tz_str_s, tz_str_d, session))) {
    if (OB_ERR_UNKNOWN_TIME_ZONE == ret) {
      ret = OB_SUCCESS;
      if(OB_FAIL(ObExprConvertTZ::parse_string(timestamp_data, tz_str_s, session, false))){
      } else if(OB_FAIL(ObExprConvertTZ::parse_string(timestamp_data, tz_str_d, session, true))){
      }
    } else if (OB_SUCCESS != ret) {
    }
  }
  if (OB_FAIL(ret)) {
    ret = OB_SUCCESS;
    result.set_null();
  } else {
    int64_t res_value = timestamp_data + (static_cast<int64_t>(offset_couple)) * 1000000;
    if (OB_UNLIKELY(res_value < MYSQL_TIMESTAMP_MIN_VAL || res_value > MYSQL_TIMESTAMP_MAX_VAL)) {
      result.set_null();
    } else {
      result.set_datetime(res_value);
    }
  }
  return ret;
}

int ObExprConvertTZ::calc_convert_tz_timestamp(const ObExpr &expr, 
                                               ObEvalCtx &ctx, 
                                               int64_t &timestamp_data, 
                                               const ObString &tz_str_s, 
                                               const ObString &tz_str_d, 
                                               ObSQLSessionInfo *session) {
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = &ctx.exec_ctx_;
  ObExprConvertTZCtx *cvrt_ctx = nullptr;

  bool is_batched_multi_stmt = true;
  if (OB_NOT_NULL(ctx.exec_ctx_.get_sql_ctx())) {
    is_batched_multi_stmt = ctx.exec_ctx_.get_sql_ctx()->multi_stmt_item_.is_batched_multi_stmt();
  }

  cvrt_ctx = static_cast<ObExprConvertTZCtx*>(exec_ctx->get_expr_op_ctx(expr.expr_ctx_id_));
  
  // for batched_multi_stmt, expr_op_ctx may be shared, so we need get tz_info each time
  if (!is_batched_multi_stmt && OB_NOT_NULL(cvrt_ctx)) {
    // reuse existing expr_op_ctx
  } else {
    if (OB_ISNULL(cvrt_ctx) && OB_FAIL(exec_ctx->create_expr_op_ctx(expr.expr_ctx_id_, cvrt_ctx))) {
      LOG_WARN("create expr op ctx failed", K(ret));
    } else if (OB_FAIL(get_cvrt_tz_info(tz_str_s, session, cvrt_ctx->tz_info_wrap_src_))) {
      cvrt_ctx->find_tz_ret_ = ret;
      LOG_WARN("get tz_st_pos failed", K(ret));
    } else if (OB_FAIL(get_cvrt_tz_info(tz_str_d, session, cvrt_ctx->tz_info_wrap_dst_))) {
      cvrt_ctx->find_tz_ret_ = ret;
      LOG_WARN("get tz_dst_pos failed", K(ret));
    }
  }

  if (OB_FAIL(ret) || OB_FAIL(cvrt_ctx->find_tz_ret_)) {
  } else if (OB_FAIL(handle_timezone_offset(timestamp_data, cvrt_ctx->tz_info_wrap_src_, false))) {
  } else if (OB_FAIL(handle_timezone_offset(timestamp_data, cvrt_ctx->tz_info_wrap_dst_, true))) {
  }

  return ret;
}

int ObExprConvertTZ::handle_timezone_offset(int64_t &timestamp_data, const ConvertTZInfoWrap &tz_info_wrap, bool is_destination) {
  int ret = OB_SUCCESS;
  if (tz_info_wrap.is_position_class()) {
    if (OB_FAIL(calc(timestamp_data, tz_info_wrap.get_tz_info_pos(), is_destination))) {
    }
  } else if (tz_info_wrap.is_offset_class()) {
    int32_t offset = tz_info_wrap.get_tz_offset();
    if (is_destination) {
      timestamp_data += (offset * USECS_PER_SEC);
    } else {
      timestamp_data -= (offset * USECS_PER_SEC);
    }
  }
  return ret;
}


int ObExprConvertTZ::calc_convert_tz_const(
    const ObExpr &expr, ObEvalCtx &ctx, int64_t &timestamp_data,
    const ObString &tz_str_s, // source time zone (input4)
    const ObString &tz_str_d, // destination time zone (input5)
    ObSQLSessionInfo *session, ObDatum &result) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(calc_convert_tz_timestamp(expr, ctx, timestamp_data, tz_str_s, tz_str_d, session))) {
    LOG_WARN("calc_timestamp_value failed", K(ret), K(tz_str_s), K(tz_str_d));
    ret = OB_SUCCESS;
    result.set_null();
  } else {
    if (OB_UNLIKELY(timestamp_data < MYSQL_TIMESTAMP_MIN_VAL || timestamp_data > MYSQL_TIMESTAMP_MAX_VAL)) {
      result.set_null();
    } else {
      result.set_datetime(timestamp_data);
    }
  }
  return ret;
}

int ObExprConvertTZ::get_cvrt_tz_info(const ObString &tz_str,
                                         ObSQLSessionInfo *session,
                                         ConvertTZInfoWrap &tz_info_wrap) {
  int ret = OB_SUCCESS;
  int ret_more = 0;
  int32_t offset = 0;
  if (OB_FAIL(ObTimeConverter::str_to_offset(tz_str, offset, ret_more,
                              true /* need_check_valid */))) {
    LOG_WARN("get time zone failed", K(ret), K(tz_str));
    if (OB_LIKELY(OB_ERR_UNKNOWN_TIME_ZONE == ret)){
      const ObTimeZoneInfo *tz_info = NULL;
      ObTimeZoneInfoPos *target_tz_pos = NULL;
      if (OB_ISNULL(tz_info = TZ_INFO(session))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tz info is null", K(ret), K(session));
      } else if (OB_FAIL(find_time_zone_pos(tz_str, *tz_info, tz_info_wrap.get_tz_info_pos()))){
        LOG_WARN("find time zone position failed", K(ret), K(ret_more));
        if (OB_ERR_UNKNOWN_TIME_ZONE == ret && OB_SUCCESS != ret_more) {
          ret = ret_more;
        }
      } else {
        ret = OB_SUCCESS;
        tz_info_wrap.set_position_class();
      }
    }
  } else {
    tz_info_wrap.set_tz_offset(offset);
  }
  return ret;
}

int ObExprConvertTZ::get_offset_by_couple_tz(int64_t timestamp_data, int32_t &offset, const ObString &tz_str_s, const ObString &tz_str_d, ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  int32_t *tz_offset = NULL;
  const ObTimeZoneInfo *tz_info = NULL;
  ObTZInfoMap *tz_info_map = NULL;
  if (OB_ISNULL(tz_info = TZ_INFO(session))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tz info is null", K(ret), K(session));
  } else if (OB_ISNULL(tz_info_map = const_cast<ObTZInfoMap *>(tz_info->get_tz_info_map()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tz_info_map is NULL", K(ret));
  } else if (OB_FAIL(tz_info_map->get_offset_by_couple_tz_name(timestamp_data, tz_str_s, tz_str_d, offset))) {
    if (OB_ERR_UNKNOWN_TIME_ZONE != ret && OB_FAIL(ret)) {
      LOG_WARN("get offset by couple tz failed", K(ret), K(tz_str_s), K(tz_str_d));
    }
  } 
  return ret;
}

int ObExprConvertTZ::parse_string(int64_t &timestamp_data, const ObString &tz_str,
                                ObSQLSessionInfo *session, const bool input_utc_time)
{
  int ret = OB_SUCCESS;
  int ret_more = 0;
  int32_t offset = 0;
  const ObTimeZoneInfo *tz_info = NULL;
  ObTimeZoneInfoPos *target_tz_pos = NULL;

  if (OB_ISNULL(tz_info = TZ_INFO(session))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tz info is null", K(ret), K(session));
  } else if (OB_SUCC(find_time_zone_pos(tz_str, *tz_info, target_tz_pos))) {
    // Successfully found in timezone map, proceed with calculation
    if (OB_FAIL(calc(timestamp_data, *target_tz_pos, input_utc_time))) {
    }
    if (NULL != target_tz_pos) {
      const_cast<ObTZInfoMap *>(tz_info->get_tz_info_map())->revert_tz_info_pos(target_tz_pos);
      target_tz_pos = NULL;
    }
  } else if (OB_ERR_UNKNOWN_TIME_ZONE == ret) {
    // Fallback to str_to_offset when timezone not found in table
    if (OB_FAIL(ObTimeConverter::str_to_offset(tz_str, offset, ret_more,
                                true /* need_check_valid */))) {
    } else if(OB_FAIL(ret_more)) {
      ret = ret_more;
    } else {
      // str_to_offset succeeded, apply offset directly
      ret = OB_SUCCESS;
      timestamp_data += (input_utc_time ? 1 : -1) * offset * USECS_PER_SEC;
    }
  } else {
    LOG_WARN("find_time_zone_pos failed with unexpected error", K(ret), K(tz_str));
  }

  return ret;
}

int ObExprConvertTZ::find_time_zone_pos(const ObString &tz_name,
                                        const ObTimeZoneInfo &tz_info,
                                        ObTimeZoneInfoPos *&tz_info_pos)
{
  int ret = OB_SUCCESS;
  ObTZInfoMap *tz_info_map = NULL;
  if (OB_ISNULL(tz_info_map = const_cast<ObTZInfoMap *>(tz_info.get_tz_info_map()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tz_info_map is NULL", K(ret));
  } else if (OB_FAIL(tz_info_map->get_tz_info_by_name(tz_name, tz_info_pos))) {
  } else {
    tz_info_pos->set_error_on_overlap_time(tz_info.is_error_on_overlap_time());
  }
  return ret;
}


int ObExprConvertTZ::find_time_zone_pos(const ObString &tz_name,
                                        const ObTimeZoneInfo &tz_info,
                                        ObTimeZoneInfoPos &tz_info_pos)
{
  int ret = OB_SUCCESS;
  ObTZInfoMap *tz_info_map = NULL;
  if (OB_ISNULL(tz_info_map = const_cast<ObTZInfoMap *>(tz_info.get_tz_info_map()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tz_info_map is NULL", K(ret));
  } else if (OB_FAIL(tz_info_map->get_tz_info_by_name(tz_name, tz_info_pos))) {
  } else {
    tz_info_pos.set_error_on_overlap_time(tz_info.is_error_on_overlap_time());
  }
  return ret;
}

int ObExprConvertTZ::calc(int64_t &timestamp_data, const ObTimeZoneInfoPos &tz_info_pos,
                          const bool input_utc_time)
{
  int ret = OB_SUCCESS;
  const int64_t input_value = timestamp_data;
  if (input_utc_time) {
    if (OB_FAIL(ObTimeConverter::timestamp_to_datetime(input_value, &tz_info_pos, timestamp_data))) {
    }
  } else {
    if (OB_FAIL(ObTimeConverter::datetime_to_timestamp(input_value, &tz_info_pos, timestamp_data))) {
    }
  }
  return ret;
}

int ObExprConvertTZ::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                                  ObExpr &expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);

  if (3 != expr.arg_cnt_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_ISNULL(expr.args_) || OB_ISNULL(expr.args_[0])
    || OB_ISNULL(expr.args_[1]) || OB_ISNULL(expr.args_[2])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of convert_tz expr is null", K(ret), K(expr.args_));
  } else if (ObDateTimeType != expr.args_[0]->datum_meta_.type_
    || ObDateTimeType != expr.datum_meta_.type_
    || ObVarcharType != expr.args_[1]->datum_meta_.type_
    || ObVarcharType != expr.args_[2]->datum_meta_.type_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument type", K(ret), K(expr.args_[0]->datum_meta_),
             K(expr.args_[1]->datum_meta_), K(expr.args_[2]->datum_meta_));
  } else {
    expr.eval_func_ = ObExprConvertTZ::eval_convert_tz;
  }
  return ret;
}

int ObExprConvertTZ::eval_convert_tz(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *timestamp = NULL;
  ObDatum *time_zone_s = NULL;
  ObDatum *time_zone_d = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, timestamp, time_zone_s, time_zone_d))) {
  } else if (OB_UNLIKELY(timestamp->is_null() || time_zone_s->is_null() || time_zone_d->is_null())) {
    res.set_null();
  } else {
    int64_t timestamp_data = timestamp->get_datetime();
    if (expr.args_[1]->is_const_expr() && expr.args_[2]->is_const_expr()) {
      if(OB_FAIL(calc_convert_tz_const(expr, ctx, timestamp_data, time_zone_s->get_string(), time_zone_d->get_string(),
                                  ctx.exec_ctx_.get_my_session(), res))) {
      }
    } else {
      if (OB_FAIL(calc_convert_tz(timestamp_data, time_zone_s->get_string(), time_zone_d->get_string(),
                                    ctx.exec_ctx_.get_my_session(), res))) {
      }
    }
  }
  return ret;
}

}
}
