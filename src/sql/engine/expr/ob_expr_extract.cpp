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

#define USING_LOG_PREFIX  SQL_ENG
#include "sql/engine/expr/ob_expr_extract.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_datum_cast.h"

#define STR_LEN 20

namespace oceanbase
{
using namespace common;
namespace sql
{
ObExprExtract::ObExprExtract(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_EXTRACT, N_EXTRACT, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprExtract::~ObExprExtract()
{
}

int ObExprExtract::calc_result_type2(ObExprResType &type,
                                     ObExprResType &date_unit,
                                     ObExprResType &date,
                                     ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  if (ob_is_enumset_tc(date.get_type())) {
    date.set_calc_type(ObVarcharType);
  }
  type.set_int();
  type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
  type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
  return ret;
}

template <bool with_date>
inline int obj_to_time(const ObDatum &date, ObObjType type, const ObScale scale,
                       const ObTimeZoneInfo *tz_info, ObTime &ob_time, const int64_t cur_ts_value,
                       const ObDateSqlMode date_sql_mode, bool has_lob_header)
{
  if (with_date) {
    return ob_datum_to_ob_time_with_date(
          date, type, scale, tz_info, ob_time, cur_ts_value, date_sql_mode, has_lob_header);
  } else {
    return ob_datum_to_ob_time_without_date(date, type, scale, tz_info, ob_time, has_lob_header);
  }
}

int ObExprExtract::calc(
      ObObjType date_type,
      const ObDatum &date,
      const ObDateUnitType extract_field,
      const ObScale scale,
      const ObCastMode cast_mode,
      const ObTimeZoneInfo *tz_info,
      const int64_t cur_ts_value,
      const ObDateSqlMode date_sql_mode,
      bool has_lob_header,
      bool &is_null,
      int64_t &res)
{
  int ret = OB_SUCCESS;
  if (date.is_null()) {
    is_null = true;
  } else {
    class ObTime ob_time;
    memset(&ob_time, 0, sizeof(ob_time));
    int warning = OB_SUCCESS;
    int &cast_ret = CM_IS_ERROR_ON_FAIL(cast_mode) ? ret : warning;
    switch (extract_field){
      case DATE_UNIT_DAY:
      case DATE_UNIT_WEEK:
      case DATE_UNIT_MONTH:
      case DATE_UNIT_QUARTER:
      case DATE_UNIT_YEAR:
      case DATE_UNIT_DAY_MICROSECOND:
      case DATE_UNIT_DAY_SECOND:
      case DATE_UNIT_DAY_MINUTE:
      case DATE_UNIT_DAY_HOUR:
      case DATE_UNIT_YEAR_MONTH:
        cast_ret =  obj_to_time<true>(
                    date, date_type, scale, tz_info, ob_time, cur_ts_value, date_sql_mode, has_lob_header);
        break;
      default:
        cast_ret = obj_to_time<false>(date, date_type, scale, tz_info, ob_time, cur_ts_value, 0,
                                         has_lob_header);
     }

     if (OB_SUCC(ret)) {
       if (OB_LIKELY(OB_SUCCESS == warning)) {
         res = ObTimeConverter::ob_time_to_int_extract(ob_time, extract_field);
       } else {
         is_null = true;
       }
     }
  }
  return ret;
}

int ObExprExtract::cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  if (rt_expr.arg_cnt_ != 2) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("extract expr should have 2 params", K(ret), K(rt_expr.arg_cnt_));
  } else if (OB_ISNULL(rt_expr.args_) || OB_ISNULL(rt_expr.args_[0])
            || OB_ISNULL(rt_expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of extract expr is null", K(ret), K(rt_expr.args_));
  } else {
    rt_expr.eval_func_ = ObExprExtract::calc_extract_mysql;
    // For static engine batch
    // Actually, the first param is always constant, can't be batch result
    if (!rt_expr.args_[0]->is_batch_result() && rt_expr.args_[1]->is_batch_result()) {
      rt_expr.eval_batch_func_ = ObExprExtract::calc_extract_mysql_batch;
      // vectorizetion 2.0
      rt_expr.eval_vector_func_ = ObExprExtract::calc_extract_mysql_vector;
    }
  }
  return ret;
}

int ObExprExtract::calc_extract_mysql(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *param_datum1 = NULL;
  ObDatum *param_datum2 = NULL;
  const ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, param_datum1))) {
    LOG_WARN("eval param1 value failed");
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, param_datum2))) {
    LOG_WARN("eval param2 value failed");
  } else {
    ObDateUnitType extract_field = static_cast<ObDateUnitType>(param_datum1->get_int());
    ObObjType date_type = expr.args_[1]->datum_meta_.type_;
    const ObTimeZoneInfo *tz_info = get_timezone_info(session);
    const int64_t cur_ts_value = get_cur_time(ctx.exec_ctx_.get_physical_plan_ctx());
    const ObScale scale = expr.args_[1]->datum_meta_.scale_;
    bool has_lob_header = expr.args_[1]->obj_meta_.has_lob_header();
    uint64_t cast_mode = 0;
    ObSQLUtils::get_default_cast_mode(session->get_stmt_type(), session, cast_mode);
    ObDateSqlMode date_sql_mode;
    date_sql_mode.init(session->get_sql_mode());
    bool is_null = false;
    int64_t value = 0;
    if (OB_FAIL(ObExprExtract::calc(date_type, *param_datum2,
                                    extract_field, 
                                    scale, 
                                    cast_mode, tz_info,
                                    cur_ts_value, 
                                    date_sql_mode, has_lob_header, is_null, value))) {
      LOG_WARN("failed to calculate extract expression", K(ret));
    } else if (is_null) {
      expr_datum.set_null();
    } else {
      expr_datum.set_int(value);
    }
  }
  return ret;
}

int ObExprExtract::calc_extract_mysql_batch(
    const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size)
{
  LOG_DEBUG("eval mysql extract in batch mode", K(batch_size));
  int ret = OB_SUCCESS;
  ObDatum *results = expr.locate_batch_datums(ctx);

  if (OB_ISNULL(results)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr results frame is not init", K(ret));
  } else {
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    ObDatum *date_unit_datum = NULL;
    const ObSQLSessionInfo *session = NULL;
    if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is null", K(ret));
    } else if (OB_FAIL(expr.args_[0]->eval(ctx, date_unit_datum))) {
      LOG_WARN("eval date_unit_datum failed", K(ret));
    } else if (OB_FAIL(expr.args_[1]->eval_batch(ctx, skip, batch_size))) {
      LOG_WARN("failed to eval batch result args0", K(ret));
    } else {
      uint64_t cast_mode = 0;
      ObSQLUtils::get_default_cast_mode(session->get_stmt_type(), session, cast_mode);
      ObObjType date_type = expr.args_[1]->datum_meta_.type_;
      ObDatum *datum_array = expr.args_[1]->locate_batch_datums(ctx);
      const ObTimeZoneInfo *tz_info = get_timezone_info(session);
      const int64_t cur_ts_value = get_cur_time(ctx.exec_ctx_.get_physical_plan_ctx());
      const ObScale scale = expr.args_[1]->datum_meta_.scale_;
      ObDateSqlMode date_sql_mode;
      date_sql_mode.init(session->get_sql_mode());
      bool has_lob_header = expr.args_[1]->obj_meta_.has_lob_header();
      ObDateUnitType extract_field = static_cast<ObDateUnitType>(date_unit_datum->get_int());
      for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
        if (skip.at(j) || eval_flags.at(j)) {
          continue;
        } else if (datum_array[j].is_null()) {
          results[j].set_null();
          eval_flags.set(j);
        } else {
          bool is_null = false;
          int64_t value = 0;
          if (OB_FAIL(ObExprExtract::calc(
                  date_type, datum_array[j], extract_field, scale, cast_mode,
                  tz_info, cur_ts_value, date_sql_mode, has_lob_header, is_null, value))) {
            LOG_WARN("failed to calculate extract expression", K(ret));
          } else {
            if (is_null) {
              results[j].set_null();
            } else {
              results[j].set_int(value);
            }
            eval_flags.set(j);
          }
        }
      }
    }
  }

  return ret;
}

template <typename T_ARG_VEC, typename T_RES_VEC>
int process_vector_mysql(const ObExpr &expr, const T_ARG_VEC &arg_date_vec, ObObjType date_type,
                    const EvalBound &bound, const ObBitVector &skip, ObBitVector &eval_flags,
                    const ObSQLSessionInfo *session, ObEvalCtx &ctx,
                    ObDateUnitType extract_field, T_RES_VEC &res_vec)
{
  int ret = OB_SUCCESS;
  uint64_t cast_mode = 0;
  ObSQLUtils::get_default_cast_mode(session->get_stmt_type(), session, cast_mode);
  bool is_with_date = false;
  int warning = OB_SUCCESS;
  int &cast_ret = CM_IS_ERROR_ON_FAIL(cast_mode) ? ret : warning;
  const ObTimeZoneInfo *tz_info = get_timezone_info(session);
  const int64_t cur_ts_value = get_cur_time(ctx.exec_ctx_.get_physical_plan_ctx());
  const ObScale scale = expr.args_[1]->datum_meta_.scale_;
  ObDateSqlMode date_sql_mode;
  date_sql_mode.init(session->get_sql_mode());
  bool has_lob_header = expr.args_[1]->obj_meta_.has_lob_header();
  for (int64_t i = bound.start(); OB_SUCC(ret) && i < bound.end(); ++i) {
    if (skip.at(i) || eval_flags.at(i)) {
      continue;
    } else {
      const char *payload = NULL;
      bool is_null = false;
      ObLength len = 0;
      arg_date_vec.get_payload(i, is_null, payload, len);
      ObDatum date(payload, len, is_null);
      int64_t value = 0;
      if (OB_FAIL(ObExprExtract::calc(date_type,
                          date,
                          extract_field,
                          scale,
                          cast_mode,
                          tz_info,
                          cur_ts_value,
                          date_sql_mode,
                          has_lob_header,
                          is_null,
                          value))) {
        LOG_WARN("failed to calculate extract expression", K(ret));
      } else {
        if (is_null) {
          res_vec.set_null(i);
        } else {
          res_vec.set_int(i, value);
        }
        eval_flags.set(i);
      }
    }
  }

  return ret;
}

int sql::ObExprExtract::calc_extract_mysql_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                                  const ObBitVector &skip, const EvalBound &bound)
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_FAIL(expr.args_[0]->eval_vector(ctx, skip, bound)) ||
             OB_FAIL(expr.args_[1]->eval_vector(ctx, skip, bound))) {
    LOG_WARN("fail to eval params", K(ret));
  } else {
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    ObIVector *arg_unit_vector = expr.args_[0]->get_vector(ctx);
    ObDateUnitType extract_field = static_cast<ObDateUnitType>(arg_unit_vector->get_int(0));
    ObIVector *arg_date_vec = expr.args_[1]->get_vector(ctx);
    ObIVector *res_vec = expr.get_vector(ctx);
    ObObjType date_type = expr.args_[1]->datum_meta_.type_;
    VectorFormat arg_format = arg_date_vec->get_format();
    VectorFormat res_format = res_vec->get_format();
    if (arg_format == VEC_FIXED && res_format == VEC_FIXED) {
      VecValueTypeClass date_vec_tc = expr.args_[1]->get_vec_value_tc();
      VecValueTypeClass res_vec_tc = expr.get_vec_value_tc();
      if (res_vec_tc != VEC_TC_INTEGER) {
        ret = process_vector_mysql<ObVectorBase, ObVectorBase>(
            expr, static_cast<ObVectorBase &>(*arg_date_vec), date_type, bound,
            skip, eval_flags, session, ctx, extract_field,
            static_cast<ObVectorBase &>(*res_vec));
      } else {
        switch (date_vec_tc) {
        case (VEC_TC_DATETIME):
          ret = process_vector_mysql<
              ObFixedLengthFormat<RTCType<VEC_TC_DATETIME>>,
              ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>>(
              expr,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_DATETIME>> &>(
                  *arg_date_vec),
              date_type, bound, skip, eval_flags, session, ctx, extract_field,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>> &>(
                  *res_vec));
          break;
        case (VEC_TC_DATE):
          ret = process_vector_mysql<
              ObFixedLengthFormat<RTCType<VEC_TC_DATE>>,
              ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>>(
              expr,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_DATE>> &>(
                  *arg_date_vec),
              date_type, bound, skip, eval_flags, session, ctx, extract_field,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>> &>(
                  *res_vec));
          break;

        case (VEC_TC_TIME):
          ret = process_vector_mysql<
              ObFixedLengthFormat<RTCType<VEC_TC_TIME>>,
              ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>>(
              expr,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_TIME>> &>(
                  *arg_date_vec),
              date_type, bound, skip, eval_flags, session, ctx, extract_field,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>> &>(
                  *res_vec));
          break;
        case (VEC_TC_YEAR):
          ret = process_vector_mysql<
              ObFixedLengthFormat<RTCType<VEC_TC_YEAR>>,
              ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>>(
              expr,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_YEAR>> &>(
                  *arg_date_vec),
              date_type, bound, skip, eval_flags, session, ctx, extract_field,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>> &>(
                  *res_vec));
          break;

        case (VEC_TC_TIMESTAMP_TZ):
          ret = process_vector_mysql<
              ObFixedLengthFormat<RTCType<VEC_TC_TIMESTAMP_TZ>>,
              ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>>(
              expr,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_TIMESTAMP_TZ>> &>(
                  *arg_date_vec),
              date_type, bound, skip, eval_flags, session, ctx, extract_field,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>> &>(
                  *res_vec));
          break;
        case (VEC_TC_TIMESTAMP_TINY):
          ret = process_vector_mysql<
              ObFixedLengthFormat<RTCType<VEC_TC_TIMESTAMP_TINY>>,
              ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>>(
              expr,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_TIMESTAMP_TINY>>
                              &>(*arg_date_vec),
              date_type, bound, skip, eval_flags, session, ctx, extract_field,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>> &>(
                  *res_vec));
          break;
        default:
          ret = process_vector_mysql<
              ObVectorBase, ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>>(
              expr, static_cast<ObVectorBase &>(*arg_date_vec), date_type,
              bound, skip, eval_flags, session, ctx, extract_field,
              static_cast<ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>> &>(
                  *res_vec));
        }
      }
    } else {
      ret = process_vector_mysql<ObVectorBase, ObVectorBase>(
          expr, static_cast<ObVectorBase &>(*arg_date_vec), date_type, bound,
          skip, eval_flags, session, ctx, extract_field,
          static_cast<ObVectorBase &>(*res_vec));
    }
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to calculate extract expression in vector format", K(ret));
    }
  }

  return ret;
}
#undef EXTRACT_FUN_VAL
#undef EXTRACT_FUN_SWITCH_CASE_MSQ
#undef EXTRACT_FUN_SWITCH_CASE_ORA
} // namespace sql
} // namespace oceanbase
