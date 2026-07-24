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

#include "sql/engine/expr/ob_expr_abs.h"
#include "share/datum/ob_datum_util.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "rpc/obmysql/ob_mysql_util.h"

namespace oceanbase
{
using namespace common;
using namespace common::number;

namespace sql
{

#define DEF_EVAL_ABS_FUNC(type)                                \
  template <>                                                  \
  int eval_datum_abs<type>(const ObExpr &expr, ObEvalCtx &ctx, \
                           ObDatum &expr_datum)

static int check_expr_and_eval(const ObExpr &expr, ObEvalCtx &ctx,
                               ObDatum *&param_datum, bool &found_null)
{
  int ret = OB_SUCCESS;
  found_null = false;
  if (OB_UNLIKELY(expr.type_ != T_OP_ABS)
      || OB_UNLIKELY(expr.arg_cnt_ != 1) || OB_ISNULL(expr.args_)
      || OB_ISNULL(expr.args_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, param_datum))) {
    LOG_WARN("failed to eval", K(ret));
  } else if (param_datum->is_null()) {
    found_null = true;
  } else {
    // do nothing
  }
  return ret;
}

template<ObObjType obj_type>
int eval_datum_abs(const ObExpr &expr,
                          ObEvalCtx &ctx,
                          ObDatum &expr_datum)
{
  UNUSED(expr);
  UNUSED(ctx);
  UNUSED(expr_datum);
  return OB_NOT_SUPPORTED;
}

DEF_EVAL_ABS_FUNC(ObNullType)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  UNUSED(expr);
  expr_datum.set_null();
  return ret;
}

DEF_EVAL_ABS_FUNC(ObNumberType)
{
  int ret = OB_SUCCESS;
  ObDatum *param_datum = NULL;
  bool found_null = false;
  if (OB_FAIL(check_expr_and_eval(expr, ctx, param_datum, found_null))) {
    LOG_WARN("failed to check expr and eval", K(ret));
  } else if (found_null) {
    expr_datum.set_null();
  } else {
    number::ObNumber param_nmb(param_datum->get_number());
    number::ObNumber res_num = param_nmb;
    if (param_nmb.is_negative()) {
      res_num = param_nmb.negate();
    }
    expr_datum.set_number(res_num);
  }
  return ret;
}

DEF_EVAL_ABS_FUNC(ObUNumberType)
{
  int ret = OB_SUCCESS;
  ObDatum *param_datum = NULL;
  bool found_null = false;
  if (OB_FAIL(check_expr_and_eval(expr, ctx, param_datum, found_null))) {
    LOG_WARN("failed to check expr and eval", K(ret));
  } else if (found_null) {
    expr_datum.set_null();
  } else {
    number::ObNumber param_nmb(param_datum->get_number());
    number::ObNumber res_num = param_nmb;
    if (param_nmb.is_negative()) {
      res_num = param_nmb.negate();
    }
    expr_datum.set_number(res_num);
  }
  return ret;
}

DEF_EVAL_ABS_FUNC(ObFloatType)
{
  int ret = OB_SUCCESS;
  ObDatum *param = NULL;
  bool found_null = false;
  if (OB_FAIL(check_expr_and_eval(expr, ctx, param, found_null))) {
    LOG_WARN("check expr and eval", K(ret));
  } else if (found_null) {
    expr_datum.set_null();
  } else {
    expr_datum.set_float(param->get_float() >= 0.0
                         ? param->get_float() : -param->get_float());
  }
  return ret;
}

DEF_EVAL_ABS_FUNC(ObDoubleType)
{
  int ret = OB_SUCCESS;
  ObDatum *param = NULL;
  bool found_null = false;
  if (OB_FAIL(check_expr_and_eval(expr, ctx, param, found_null))) {
    LOG_WARN("failed to check expr and eval", K(ret));
  } else if (found_null) {
    expr_datum.set_null();
  } else {
    expr_datum.set_double(param->get_double() >= 0
                          ? param->get_double() : -param->get_double());
  }
  return ret;
}

DEF_EVAL_ABS_FUNC(ObUDoubleType)
{
  int ret = OB_SUCCESS;
  ObDatum *param = NULL;
  bool found_null = false;
  if (OB_FAIL(check_expr_and_eval(expr, ctx, param, found_null))) {
    LOG_WARN("failed to check expr and eval", K(ret));
  } else if (found_null) {
    expr_datum.set_null();
  } else {
    expr_datum.set_double(param->get_udouble());
  }
  return ret;
}

DEF_EVAL_ABS_FUNC(ObIntType)
{
  int ret = OB_SUCCESS;
  ObDatum *param = NULL;
  bool found_null = false;
  if (OB_FAIL(check_expr_and_eval(expr, ctx, param, found_null))) {
    LOG_WARN("failed to check expr and eval", K(ret));
  } else if (found_null) {
    expr_datum.set_null();
  } else {
    int64_t param_int = param->get_int();
    // Only mysql mode will call this function, if INT64_MIN is found, out of range needs to be reported
    if (INT64_MIN == param_int) {
      ret = OB_OPERATE_OVERFLOW;
      LOG_WARN("value out of range", K(ret));
    } else {
      expr_datum.set_int(param_int >= 0 ? param_int : -param_int);
    }
  }
  return ret;
}

DEF_EVAL_ABS_FUNC(ObUInt64Type)
{
  int ret = OB_SUCCESS;
  ObDatum *param = NULL;
  bool found_null = false;
  if (OB_FAIL(check_expr_and_eval(expr, ctx, param, found_null))) {
    LOG_WARN("failed to check expr and eval", K(ret));
  } else if (found_null) {
    expr_datum.set_null();
  } else {
    expr_datum.set_uint(param->get_uint64());
  }
  return ret;
}

#define MAKE_DECIMAL_INT_OPPOSITE(TYPE)            \
  case sizeof(TYPE##_t): {                         \
    res_val.from(-(*(decint->TYPE##_v_)));         \
    break;                                         \
  }

DEF_EVAL_ABS_FUNC(ObDecimalIntType)
{
  int ret = OB_SUCCESS;
  ObDatum *param_datum = NULL;
  bool found_null = false;
  if (OB_FAIL(check_expr_and_eval(expr, ctx, param_datum, found_null))) {
    LOG_WARN("failed to check expr and eval", K(ret));
  } else if (found_null) {
    expr_datum.set_null();
  } else {
    const ObDecimalInt *decint = param_datum->get_decimal_int();
    const int32_t int_bytes = param_datum->get_int_bytes();
    bool is_neg = wide::is_negative(decint, int_bytes);
    if (is_neg) {
      ObDecimalIntBuilder res_val;
      switch (int_bytes) {
        MAKE_DECIMAL_INT_OPPOSITE(int32)
        MAKE_DECIMAL_INT_OPPOSITE(int64)
        MAKE_DECIMAL_INT_OPPOSITE(int128)
        MAKE_DECIMAL_INT_OPPOSITE(int256)
        MAKE_DECIMAL_INT_OPPOSITE(int512)
        default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("int_bytes is unexpected", K(ret), K(int_bytes));
          break;
        }
      }
      if (OB_SUCC(ret)) {
        expr_datum.set_decimal_int(res_val.get_decimal_int(), int_bytes);
      }
    } else {
      expr_datum.set_decimal_int(decint, int_bytes);
    }
  }
  return ret;
}

ObExpr::EvalFunc abs_funcs[ObMaxType];


template<int IDX>
struct AbsFuncIniter
{
  static bool init_array()
  {
    abs_funcs[IDX] = &eval_datum_abs<static_cast<ObObjType>(IDX)>;
    return true;
  }
};

static bool abs_eval_func_init_ret = ObArrayConstIniter<ObMaxType, AbsFuncIniter>::init();

static_assert(ObMaxType == sizeof(abs_funcs) / sizeof(void *), "unexpected size");

ObExprAbs::ObExprAbs(ObIAllocator &alloc)
    : ObExprOperator(alloc, T_OP_ABS, N_ABS, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION),
      func_(NULL) {}

int ObExprAbs::assign(const ObExprOperator &other)
{
  int ret = OB_SUCCESS;
  const ObExprAbs *tmp_other = dynamic_cast<const ObExprAbs *>(&other);
  if (OB_UNLIKELY(NULL == tmp_other)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument. wrong type for other", K(ret), K(other));
  } else if (OB_LIKELY(this != tmp_other)) {
    if (OB_FAIL(ObExprOperator::assign(other))) {
      LOG_WARN("copy in Base class ObExprOperator failed", K(ret));
    } else {
      this->func_ = tmp_other->func_;
    }
  }
  return ret;
}

int ObExprAbs::calc_result_type1(ObExprResType &type, ObExprResType &type1,
                                 ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = type_ctx.get_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else if (NOT_ROW_DIMENSION == row_dimension_) {
    // result type
    ObObjType itype;
    if (OB_SUCC(ObExprResultTypeUtil::get_abs_result_type(itype, type1.get_type()))) {
      if (ObMaxType == itype) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
      } else {
        type.set_type(itype);
      }
    }

    // collation
    // The result cannot be of character type, no need to set collation
    if (type.is_double() && type1.get_scale() != SCALE_UNKNOWN_YET) {
      type.set_scale(type1.get_scale());
      type.set_precision(static_cast<ObPrecision>(obmysql::ObMySQLUtil::float_length(type1.get_scale())));
    } else {
      type.set_accuracy(type1.get_accuracy());
    }

    // null flag
    ObExprOperator::calc_result_flag1(type, type1);

    if (OB_SUCC(ret)) {
      // set calc type for param
      ObObjType param_calc_type = calc_param_type(type1.get_type());
      if (OB_UNLIKELY(ObMaxType == param_calc_type)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid param calc type", K(ret), K(type1.get_type()), K(param_calc_type));
      } else {
        type1.set_calc_type(param_calc_type);
        if (type1.get_type() == ObJsonType) {
          type1.set_calc_type(ObDoubleType);
          type.set_type(ObDoubleType);
        }
      }
    }
  } else {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  }
  return ret;
}

//tinyint, mediumint, smallint, int32
//int64
//utiniyint, umediumint, usmallint
//uint32 uint64
//float
//double
//ufloat
//udouble
//number
//unumber

//null

//others. (datetime time varchar, etc)

//extended types. (datetime time varchar, etc)



//bit
//enum_set

int ObExprAbs::cg_expr(ObExprCGCtx &ctx,
                       const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  UNUSED(raw_expr);
  if (OB_UNLIKELY(T_OP_ABS != rt_expr.type_)
      || OB_ISNULL(rt_expr.args_)
      || OB_UNLIKELY(rt_expr.arg_cnt_ !=  1)
      || OB_ISNULL(rt_expr.args_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_UNLIKELY(rt_expr.args_[0]->datum_meta_.type_ >= ObMaxType)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg type for abs", K(ret));
  } else {
    rt_expr.eval_func_ = abs_funcs[rt_expr.args_[0]->datum_meta_.type_];
  }
  return ret;
}

ObObjType ObExprAbs::calc_param_type(const ObObjType orig_param_type)
{
  ObObjType calc_type = ObMaxType;
  switch (orig_param_type)
  {
  case ObTinyIntType:
  case ObSmallIntType:
  case ObMediumIntType:
  case ObInt32Type:
  case ObIntType: {
    calc_type = ObIntType;
    break;
  }
  case ObUTinyIntType:
  case ObUSmallIntType:
  case ObUMediumIntType:
  case ObUInt32Type:
  case ObUInt64Type: {
    calc_type = ObUInt64Type;
    break;
  }
  case ObFloatType:
  case ObDoubleType: {
    calc_type = ObDoubleType;
    break;
  }
  case ObUFloatType:
  case ObUDoubleType: {
    calc_type = ObUDoubleType;
    break;
  }
  case ObNumberType: {
    calc_type = ObNumberType;
    break;
  }
  case ObUNumberType: {
    calc_type = ObUNumberType;
    break;
  }
  case ObNullType: {
    calc_type = ObNullType;
    break;
  }
  case ObYearType: {
    calc_type = ObUInt64Type;
    break;
  }
  case ObDateTimeType:
  case ObTimestampType:
  case ObDateType:
  case ObTimeType:
  case ObVarcharType:
  case ObCharType:
  case ObUnknownType:
  case ObHexStringType:
  case ObTextType:
  case ObTinyTextType:
  case ObMediumTextType:
  case ObLongTextType:
  case ObMySQLDateType:
  case ObMySQLDateTimeType: {
    calc_type = ObDoubleType;
    break;
  }
  case ObBitType: {
    calc_type = ObUInt64Type;
    break;
  }
  case ObEnumType:
  case ObSetType:
  case ObJsonType: {
    calc_type = ObDoubleType;
    break;
  }
  case ObDecimalIntType: {
    calc_type = ObDecimalIntType;
    break;
  }
  default: {
    break;
  }
  }
  return calc_type;
}

} // namespace sql
} // namespace oceanbase
