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

#define USING_LOG_PREFIX  SQL_ENG
#include "sql/engine/expr/ob_expr_func_round.h"
#include <string.h>
#include "share/object/ob_obj_cast.h"
#include "sql/parser/ob_item_type.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/expr/ob_datum_cast.h"
#include "sql/engine/ob_exec_context.h"
#include "rpc/obmysql/ob_mysql_util.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
#define GET_SCALE_FOR_CALC(scale) (scale < 0 ? max(ROUND_MIN_SCALE, scale) : min(ROUND_MAX_SCALE, scale))
#define GET_SCALE_FOR_DEDUCE(scale) ((scale < 0 ? 0 : min(ROUND_MAX_SCALE, scale)))
namespace oceanbase
{
namespace sql
{

ObExprFuncRound::ObExprFuncRound(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_ROUND, N_ROUND, ONE_OR_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprFuncRound::~ObExprFuncRound()
{
}

int ObExprFuncRound::calc_result_typeN(ObExprResType &type,
                                       ObExprResType *params,
                                       int64_t param_num,
                                       ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = type_ctx.get_session();
  if (OB_UNLIKELY(NULL == params || param_num <= 0 || param_num > 2) || OB_ISNULL(session)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument.", K(ret), K(params), K(param_num), K(type_ctx.get_session()));
  } else {
    OZ(se_deduce_type(type, params, param_num, type_ctx));
  }
  return ret;
}

int ObExprFuncRound::se_deduce_type(ObExprResType &type,
                                    ObExprResType *params,
                                    int64_t param_num,
                                    ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObObjType res_type = ObMaxType;
  if (OB_FAIL(set_res_and_calc_type(params, param_num, res_type))) {
  } else if (OB_FAIL(set_res_scale_prec(type_ctx, params, param_num, res_type, type))) {
  } else {
    ObExprOperator::calc_result_flag1(type, params[0]);
    type.set_type(res_type);
  }
  return ret;
}

int ObExprFuncRound::set_res_and_calc_type(ObExprResType *params, int64_t param_num,
                                       ObObjType &res_type)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObExprResultTypeUtil::get_round_result_type(res_type, params[0].get_type()))) {
  } else if (1 == param_num) {
    params[0].set_calc_type(res_type);
  } else if (2 == param_num) {
    params[0].set_calc_type(res_type);
    params[1].set_calc_type(ObIntType);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected param_num", K(ret), K(param_num));
  }
  return ret;
}

int ObExprFuncRound::set_res_scale_prec(ObExprTypeCtx &type_ctx, ObExprResType *params,
                                        int64_t param_num, const ObObjType &res_type,
                                        ObExprResType &type)
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  ObPrecision res_prec = PRECISION_UNKNOWN_YET;
  ObScale res_scale = SCALE_UNKNOWN_YET;

  if (OB_UNLIKELY(1 != param_num && 2 != param_num)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected param_num", K(ret), K(param_num));
  } else {
    if (1 == param_num) {
      res_scale = DEFAULT_SCALE_FOR_INTEGER;
    } else if (2 == param_num && params[1].is_null()) {
      res_scale = DEFAULT_SCALE_FOR_INTEGER; // compatible with mysql
    } else if (2 == param_num && params[1].is_literal()
               && !params[0].is_integer_type()) {
      const ObObj &obj = params[1].get_param();
      ObArenaAllocator oballocator(ObModIds::BLOCK_ALLOC);
      ObCastMode cast_mode = CM_NONE;
      ObCollationType cast_coll_type = type_ctx.get_coll_type();
      const ObDataTypeCastParams dtc_params = type_ctx.get_dtc_params();
      ObSQLUtils::get_default_cast_mode(type_ctx.get_sql_mode(), cast_mode);
      cast_mode |= CM_WARN_ON_FAIL;
      ObCastCtx cast_ctx(&oballocator, &dtc_params, 0, cast_mode, cast_coll_type);
      int64_t scale = 0;
      EXPR_GET_INT64_V2(obj, scale);
      if (OB_SUCC(ret)) {
        res_scale = static_cast<ObScale>(GET_SCALE_FOR_DEDUCE(scale));
      } else {
        res_scale = static_cast<ObScale>(scale);
      }
      if ((ob_is_number_tc(params[0].get_type()) || ob_is_decimal_int_tc(params[0].get_type()))
          && params[0].get_scale() < res_scale) {
        // eg : select round(123.123, 100); -> result is 123.123
        res_scale = params[0].get_scale();
      }
    } else {
      if (ob_is_numeric_type(res_type)) {
        if (ob_is_int_tc(res_type)) {
          res_prec = ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_;
          res_scale = ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_;
        } else if (ob_is_uint_tc(res_type)) {
          res_prec = ObAccuracy::DDL_DEFAULT_ACCURACY[ObUInt64Type].precision_;
          res_scale = ObAccuracy::DDL_DEFAULT_ACCURACY[ObUInt64Type].scale_;
        } else {
          res_prec = params[0].get_precision();
          res_scale = params[0].get_scale();
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (ob_is_number_tc(res_type) || ob_is_decimal_int_tc(res_type)) {
      ObPrecision tmp_res_prec = -1;
      if (1 == param_num) {
        tmp_res_prec = static_cast<ObPrecision>(params[0].get_precision() -
                                                params[0].get_scale() + 1);
        res_prec = tmp_res_prec >= 0 ? tmp_res_prec : res_prec;
        res_scale = 0;
      } else {
        tmp_res_prec = static_cast<ObPrecision>(params[0].get_precision() -
                                                params[0].get_scale() + res_scale + 1);
      }
      res_prec = tmp_res_prec >= 0 ? tmp_res_prec : res_prec;
    } else if (ob_is_real_type(res_type)) {
      res_prec = (SCALE_UNKNOWN_YET == res_scale) ?
        PRECISION_UNKNOWN_YET : obmysql::ObMySQLUtil::float_length(res_scale);
    } else if (ob_is_integer_type(res_type)) {
      if (PRECISION_UNKNOWN_YET == res_prec) {
        res_prec = ObAccuracy::DDL_DEFAULT_ACCURACY[res_type].precision_;
      }
    }
    type.set_scale(res_scale);
    type.set_precision(res_prec);
  }
  return ret;
}

int ObExprFuncRound::do_round_decimalint(
    const int16_t in_prec, const int16_t in_scale,
    const int16_t out_prec, const int16_t out_scale, const int64_t round_scale,
    const ObDatum &in_datum, ObDecimalIntBuilder &res_val)
{
  int ret = OB_SUCCESS;
  const ObDecimalInt *decint = in_datum.get_decimal_int();
  const int32_t int_bytes = in_datum.get_int_bytes();
  if (in_scale != round_scale || get_decimalint_type(in_prec) != get_decimalint_type(out_prec)) {
    ObDecimalIntBuilder scaled_down_val;
    ObDecimalIntBuilder scaled_up_val;
    int32_t expected_int_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
    if (OB_FAIL(wide::common_scale_decimalint(
                decint, int_bytes, in_scale, round_scale, scaled_down_val))) {
    } else if ((round_scale < out_scale)
               && OB_FAIL(wide::common_scale_decimalint(scaled_down_val.get_decimal_int(),
                   int_bytes, round_scale, out_scale, scaled_up_val))) {
      LOG_WARN("scale decimal int failed", K(ret), K(int_bytes), K(in_scale),
               K(out_scale), K(round_scale));
    } else if (OB_FAIL(ObDatumCast::align_decint_precision_unsafe(
      round_scale < out_scale ? scaled_up_val.get_decimal_int() : scaled_down_val.get_decimal_int(),
      round_scale < out_scale ? scaled_up_val.get_int_bytes() : scaled_down_val.get_int_bytes(),
      expected_int_bytes, res_val))) {
    }
  } else {
    res_val.from(decint, int_bytes);
  }
  return ret;
}

int ObExprFuncRound::calc_round_decimalint(
    const ObDatumMeta &in_meta, const ObDatumMeta &out_meta, const int64_t round_scale,
    const ObDatum &in_datum, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  if (in_meta.scale_ != round_scale
      || get_decimalint_type(in_meta.precision_) != get_decimalint_type(out_meta.precision_)) {
    ObDecimalIntBuilder res_val;
    if (OB_FAIL(do_round_decimalint(
        in_meta.precision_, in_meta.scale_, out_meta.precision_, out_meta.scale_, round_scale,
        in_datum, res_val))) {
    } else {
      res_datum.set_decimal_int(res_val.get_decimal_int(), res_val.get_int_bytes());
    }
  } else {
    res_datum.set_decimal_int(in_datum.get_decimal_int(), in_datum.len_); // need deep copy
  }
  return ret;
}

static int do_round_by_type(
    const ObDatumMeta &in_meta, const ObDatumMeta &out_meta, const int64_t round_scale,
    const ObDatum &x_datum, ObEvalCtx &ctx,
    ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  const ObObjType &x_type = in_meta.get_type();
  UNUSED(ctx);
  switch (x_type) {
    case ObNumberType:
    case ObUNumberType: {
      const number::ObNumber x_nmb(x_datum.get_number());
      number::ObNumber res_nmb;
      ObNumStackOnceAlloc tmp_alloc;
      if (OB_FAIL(res_nmb.from(x_nmb, tmp_alloc))) {
      } else if (OB_FAIL(res_nmb.round(GET_SCALE_FOR_CALC(round_scale)))) {
      } else {
        res_datum.set_number(res_nmb);
      }
      break;
    }
    case ObDecimalIntType: {
      if (OB_FAIL(ObExprFuncRound::calc_round_decimalint(
                  in_meta, out_meta, GET_SCALE_FOR_CALC(round_scale), x_datum, res_datum))) {
      }
      break;
    }
    case ObFloatType: {
      // Float inputs are rounded with the supplied scale.
      res_datum.set_float(ObExprUtil::round_double(x_datum.get_float(), round_scale));
      break;
    }
    case ObDoubleType: {
      // Double inputs are rounded with the supplied scale.
      res_datum.set_double(ObExprUtil::round_double(x_datum.get_double(), round_scale));
      break;
    }
    case ObIntType: {
      int64_t x_int = x_datum.get_int();
      bool neg = x_int < 0;
      x_int = neg ? -x_int : x_int;
      int64_t res_int = static_cast<int64_t>(ObExprUtil::round_uint64(x_int, round_scale));
      res_int = neg ? -res_int : res_int;
      res_datum.set_int(res_int);
      break;
    }
    case ObUInt64Type: {
      uint64_t x_uint = x_datum.get_uint();
      uint64_t res_uint = ObExprUtil::round_uint64(x_uint, round_scale);
      res_datum.set_uint(res_uint);
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected arg type", K(ret), K(x_type));
      break;
    }
  }
  return ret;
}

/**
 * @brief Check whether the batch has a null value, 
 * and check the skip flags and eval flags of the round expression.
 * If there is no null value and the skip flags and eval flags are both false, 
 * the function returns true, otherwise it returns false.
 * 
 * @param x_datums 
 * @param skip 
 * @param eval_flags 
 * @param batch_size 
 * @return true 
 * @return false 
 */
static bool is_batch_need_cal_all(const ObDatum *x_datums,
                            const ObBitVector &skip,
                            const ObBitVector &eval_flags,
                            const int64_t batch_size)
{
  bool is_need = ObBitVector::bit_op_zero(skip, eval_flags, batch_size,
                                      [](uint64_t l, uint64_t r) { return l | r; });
  for (int64_t i = 0; is_need && i < batch_size; ++i) {
    is_need = !(x_datums[i].is_null());
  }
  return is_need;
}

static int do_round_by_type_batch_with_check(const int64_t scale, const ObExpr &expr,
                                  ObEvalCtx &ctx, const ObBitVector &skip,
                                  const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObDatum *results = expr.locate_batch_datums(ctx);
  ObDatum *x_datums = expr.args_[0]->locate_batch_datums(ctx);
  const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
  switch (x_type) {
    case ObNumberType:
    case ObUNumberType: {
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
        if (skip.at(i) || eval_flags.at(i)) {
            continue;
        }
        ObDatum &x_datum = x_datums[i];
        eval_flags.set(i);
        if (x_datum.is_null()) {
          results[i].set_null();
        } else{
          const number::ObNumber x_nmb(x_datum.get_number());
          number::ObNumber res_nmb;
          ObNumStackOnceAlloc tmp_alloc;
          if (OB_FAIL(res_nmb.from(x_nmb, tmp_alloc))) {
            LOG_WARN("get num from x failed", K(ret), K(x_nmb));
            break;
          } else if (OB_FAIL(res_nmb.round(GET_SCALE_FOR_CALC(scale)))) {
            LOG_WARN("eval round of res_nmb failed", K(ret), K(scale), K(res_nmb));
            break;
          } else {
            results[i].set_number(res_nmb);
          }
        }
      }
      break;
    }
    case ObDecimalIntType: {
      const ObDatumMeta &in_meta = expr.args_[0]->datum_meta_;
      const ObDatumMeta &out_meta = expr.datum_meta_;
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
        if (skip.at(i) || eval_flags.at(i)) {
          continue;
        } else {
          ObDatum &x_datum = x_datums[i];
          eval_flags.set(i);
          if (x_datum.is_null()) {
            results[i].set_null();
          } else if (OB_FAIL(ObExprFuncRound::calc_round_decimalint(
                             in_meta, out_meta, GET_SCALE_FOR_CALC(scale), x_datum, results[i]))) {
          }
        }
      }
      break;
    }
    case ObFloatType: {
      for (int64_t i = 0; i < batch_size; ++i) {
        if (skip.at(i) || eval_flags.at(i)) {
            continue;
        }
        ObDatum &x_datum = x_datums[i];
        eval_flags.set(i);
        if (x_datum.is_null()) {
          results[i].set_null();
        } else{
          // Float inputs are rounded with the supplied scale.
          results[i].set_float(ObExprUtil::round_double(x_datum.get_float(), scale));
        }
      }
      break;
    }
    case ObDoubleType: {
      for (int64_t i = 0; i < batch_size; ++i) {
        if (skip.at(i) || eval_flags.at(i)) {
            continue;
        }
        ObDatum &x_datum = x_datums[i];
        eval_flags.set(i);
        if (x_datum.is_null()) {
          results[i].set_null();
        } else{
          // Double inputs are rounded with the supplied scale.
          results[i].set_double(ObExprUtil::round_double(x_datum.get_double(), scale));
        }
      }
      break;
    }
    case ObIntType: {
      for (int64_t i = 0; i < batch_size; ++i) {
        if (skip.at(i) || eval_flags.at(i)) {
            continue;
        }
        ObDatum &x_datum = x_datums[i];
        eval_flags.set(i);
        if (x_datum.is_null()) {
          results[i].set_null();
        } else{
          int64_t x_int = x_datum.get_int();
          bool neg = x_int < 0;
          x_int = neg ? -x_int : x_int;
          int64_t res_int = static_cast<int64_t>(ObExprUtil::round_uint64(x_int, scale));
          res_int = neg ? -res_int : res_int;
          results[i].set_int(res_int);
        }
      }
      break;
    }
    case ObUInt64Type: {
      for (int64_t i = 0; i < batch_size; ++i) {
        if (skip.at(i) || eval_flags.at(i)) {
            continue;
        }
        ObDatum &x_datum = x_datums[i];
        eval_flags.set(i);
        if (x_datum.is_null()) {
          results[i].set_null();
        } else{
          uint64_t x_uint = x_datum.get_uint();
          uint64_t res_uint = ObExprUtil::round_uint64(x_uint, scale);
          results[i].set_uint(res_uint);
        }
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected arg type", K(ret), K(x_type));
      break;
    }
  }
  return ret;
}

static int do_round_by_type_batch_without_check(const int64_t scale, const ObExpr &expr,
                                  ObEvalCtx &ctx, const int64_t batch_size)
{
  // This function only calculates batch that do not contain null value
  int ret = OB_SUCCESS;
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObDatum *results = expr.locate_batch_datums(ctx);
  ObDatum *x_datums = expr.args_[0]->locate_batch_datums(ctx);
  const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
  switch (x_type) {
    case ObNumberType:
    case ObUNumberType: {
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
        const number::ObNumber x_nmb(x_datums[i].get_number());
        number::ObNumber res_nmb;
        ObNumStackOnceAlloc tmp_alloc;
        if (OB_FAIL(res_nmb.from(x_nmb, tmp_alloc))) {
        } else if (OB_FAIL(res_nmb.round(GET_SCALE_FOR_CALC(scale)))) {
        } else {
          results[i].set_number(res_nmb);
        }
      }
      break;
    }
    case ObDecimalIntType: {
      const ObDatumMeta &in_meta = expr.args_[0]->datum_meta_;
      const ObDatumMeta &out_meta = expr.datum_meta_;
      for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
        if (OB_FAIL(ObExprFuncRound::calc_round_decimalint(
                    in_meta, out_meta, GET_SCALE_FOR_CALC(scale), x_datums[i], results[i]))) {
        }
      }
      break;
    }
    case ObFloatType: {
      for (int64_t i = 0; i < batch_size; ++i) {
        // Float inputs are rounded with the supplied scale.
        results[i].set_float(ObExprUtil::round_double(x_datums[i].get_float(), scale));
      }
      break;
    }
    case ObDoubleType: {
      for (int64_t i = 0; i < batch_size; ++i) {
        // Double inputs are rounded with the supplied scale.
        results[i].set_double(ObExprUtil::round_double(x_datums[i].get_double(), scale));
      }
      break;
    }
    case ObIntType: {
      for (int64_t i = 0; i < batch_size; ++i) {
        int64_t x_int = x_datums[i].get_int();
        bool neg = x_int < 0;
        x_int = neg ? -x_int : x_int;
        int64_t res_int = static_cast<int64_t>(ObExprUtil::round_uint64(x_int, scale));
        res_int = neg ? -res_int : res_int;
        results[i].set_int(res_int);
      }
      break;
    }
    case ObUInt64Type: {
      for (int64_t i = 0; i < batch_size; ++i) {
        uint64_t x_uint = x_datums[i].get_uint();
        uint64_t res_uint = ObExprUtil::round_uint64(x_uint, scale);
        results[i].set_uint(res_uint);
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected arg type", K(ret), K(x_type));
      break;
    }
  }
  if (OB_SUCC(ret)) {
    eval_flags.set_all(batch_size);
  }
  return ret;
}

int calc_round_expr_numeric1(const sql::ObExpr &expr, sql::ObEvalCtx &ctx,
                              sql::ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *x_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, x_datum))) {
  } else if (x_datum->is_null()) {
    res_datum.set_null();
  } else if (OB_FAIL(do_round_by_type(
              expr.args_[0]->datum_meta_, expr.datum_meta_, 0, *x_datum, ctx, res_datum))) {
  }
  return ret;
}

int ObExprFuncRound::calc_round_expr_numeric1_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else {
    ObDatum *x_datums = expr.args_[0]->locate_batch_datums(ctx);
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    if (is_batch_need_cal_all(x_datums, skip, eval_flags, batch_size)) {
      if (OB_FAIL(do_round_by_type_batch_without_check(0, expr, ctx, batch_size))) {
        const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
        LOG_WARN("calc round by type failed", K(ret), K(x_type), K(expr));
      }
    } else {
      if (OB_FAIL(do_round_by_type_batch_with_check(0, expr, ctx, skip, batch_size))) {
        const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
        LOG_WARN("calc round by type failed", K(ret), K(x_type), K(expr));
      }
    }
  }
  return ret;
}

int calc_round_expr_numeric2(const sql::ObExpr &expr, sql::ObEvalCtx &ctx,
                              sql::ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *x_datum = NULL;
  ObDatum *fmt_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, x_datum)) ||
      OB_FAIL(expr.args_[1]->eval(ctx, fmt_datum))) {
    LOG_WARN("eval arg failed", K(ret), K(expr));
  } else if (x_datum->is_null() || fmt_datum->is_null()) {
    res_datum.set_null();
  } else {
    int64_t scale = 0;
    // get scale
    const ObObjType fmt_type = expr.args_[1]->datum_meta_.type_;
    if (ObNumberType == fmt_type) {
      const number::ObNumber fmt_nmb(fmt_datum->get_number());
      if (OB_FAIL(fmt_nmb.extract_valid_int64_with_trunc(scale))) {
      }
    } else if (ObIntType == fmt_type) {
      scale = fmt_datum->get_int();
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected fmt type", K(ret), K(fmt_type), K(expr));
    }
    if (OB_SUCC(ret)) {
      if (ob_is_number_tc(expr.args_[0]->datum_meta_.get_type())
              || ob_is_decimal_int_tc(expr.args_[0]->datum_meta_.get_type())) {
        if (expr.args_[0]->datum_meta_.scale_ < scale
            // eg : select round(123.123, 100);
            //      -> result is 123.123
            || expr.datum_meta_.scale_ < scale) {
            // eg : select round(123.123456789123456789123456789123456789, 50);
            //      -> result accuracy is precision:34, scale:30 (max result scale is 30)
          scale = expr.datum_meta_.scale_;
        }
      }
      if (OB_FAIL(do_round_by_type(
                  expr.args_[0]->datum_meta_, expr.datum_meta_, scale, *x_datum, ctx, res_datum))) {
      }
    }
  }
  return ret;
}

int ObExprFuncRound::calc_round_expr_numeric2_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  ObDatum *fmt_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size)) ||
      OB_FAIL(expr.args_[1]->eval(ctx, fmt_datum))) {
    LOG_WARN("eval arg failed", K(ret), K(expr));
  } else {
    int64_t scale = 0;
    // get scale
    const ObObjType fmt_type = expr.args_[1]->datum_meta_.type_;
    if (fmt_datum->is_null()) {
      // do nothing
    } else if (ObNumberType == fmt_type) {
      const number::ObNumber fmt_nmb(fmt_datum->get_number());
      if (OB_FAIL(fmt_nmb.extract_valid_int64_with_trunc(scale))) {
      }
    } else if (ObIntType == fmt_type) {
      scale = fmt_datum->get_int();
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected fmt type", K(ret), K(fmt_type), K(expr));
    }
    if (OB_SUCC(ret)) {
      if (fmt_datum->is_null()) {
        ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
        ObDatum *results = expr.locate_batch_datums(ctx);
        for (int64_t i = 0; i < batch_size; ++i) {
          eval_flags.set(i);
          results[i].set_null();
        }
      } else {
        if (ob_is_number_tc(expr.args_[0]->datum_meta_.get_type())
                || ob_is_decimal_int_tc(expr.args_[0]->datum_meta_.get_type())) {
          if (expr.args_[0]->datum_meta_.scale_ < scale
              // eg : select round(123.123, 100);
              //      -> result is 123.123
              || expr.datum_meta_.scale_ < scale) {
              // eg : select round(123.123456789123456789123456789123456789, 50);
              //      -> result accuracy is precision:34, scale:30 (max result scale is 30)
            scale = expr.datum_meta_.scale_;
          }
        }
        ObDatum *x_datums = expr.args_[0]->locate_batch_datums(ctx);
        ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
        if (is_batch_need_cal_all(x_datums, skip, eval_flags, batch_size)) {
          if (OB_FAIL(do_round_by_type_batch_without_check(scale, expr, ctx, batch_size))) {
            const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
            LOG_WARN("calc round by type failed", K(ret), K(x_type), K(expr));
          }
        } else {
          if (OB_FAIL(do_round_by_type_batch_with_check(scale, expr, ctx, skip, batch_size))) {
            const ObObjType x_type = expr.args_[0]->datum_meta_.type_;
            LOG_WARN("calc round by type failed", K(ret), K(x_type), K(expr));
          }
        }
      }
    }
  }
  return ret;
}

int ObExprFuncRound::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                             ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  // round(x, fmt)
  if (OB_UNLIKELY(1 != rt_expr.arg_cnt_ && 2 != rt_expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid arg cnt of expr", K(ret), K(rt_expr));
  } else {
    const ObObjType &x_type = rt_expr.args_[0]->datum_meta_.type_;
    const ObObjType &res_type = rt_expr.datum_meta_.type_;
    if (OB_UNLIKELY(x_type != res_type)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid arg type or res type", K(ret), K(x_type), K(res_type));
    } else if (2 == rt_expr.arg_cnt_) {
      rt_expr.eval_func_ = calc_round_expr_numeric2;
      // Only implement vectorization when parameter 0 is batch and parameter 1 is constant
      if (rt_expr.args_[0]->is_batch_result() && !(rt_expr.args_[1]->is_batch_result())) {
        rt_expr.eval_batch_func_ = calc_round_expr_numeric2_batch;
      }
    } else {
      rt_expr.eval_func_ = calc_round_expr_numeric1;
      rt_expr.eval_batch_func_ = calc_round_expr_numeric1_batch;
    }
  }
  return ret;
}

DEF_SET_LOCAL_SESSION_VARS(ObExprFuncRound, raw_expr) {
  int ret = OB_SUCCESS;
  SET_LOCAL_SYSVAR_CAPACITY(3);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_SQL_MODE);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_TIME_ZONE);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_COLLATION_CONNECTION);
  return ret;
}

} // sql
} // oceanbase
