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
#include "sql/engine/expr/ob_expr_is.h"
#include "ob_expr_json_func_helper.h"
#include "common/datum/datum_value.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprIsBase::ObExprIsBase(ObIAllocator &alloc,
                           ObExprOperatorType type,
                           const char *name)
    : ObRelationalExprOperator(alloc, type, name, 2)
{};

int ObExprIsBase::calc_result_type2(ObExprResType &type,
                                    ObExprResType &type1,
                                    ObExprResType &type2,
                                    ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObRawExpr *raw_expr = get_raw_expr();
  ObOpRawExpr *op_expr = static_cast<ObOpRawExpr *>(raw_expr);
  if (OB_ISNULL(op_expr) || OB_UNLIKELY(op_expr->get_param_count() != 2)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // always allow NULL value
    type.set_result_flag(NOT_NULL_FLAG);
    //is operator second parameter must keep the original NULL or FALSE, TRUE
    type2.set_calc_type(type2.get_type());

    ObRawExpr *param2 = op_expr->get_param_expr(1);
    ObConstRawExpr *const_param2 = static_cast<ObConstRawExpr *>(param2);
    if (OB_ISNULL(const_param2)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (ObDoubleType == const_param2->get_value_meta().get_type()) {
        type1.set_calc_type(ObDoubleType);
        type1.set_calc_accuracy(type1.get_accuracy());
    } else if (!const_param2->get_value_datum().is_null()) {  // is true/false
      if (ob_is_numeric_type(type1.get_type())) {
        type1.set_calc_meta(type1.get_sql_type_meta());
        type1.set_calc_accuracy(type1.get_accuracy());
      } else if (ob_is_json(type1.get_type())) {
        type1.set_calc_meta(type1.get_sql_type_meta());
        type1.set_calc_accuracy(type1.get_accuracy());
      } else {
        type1.set_calc_type(ObNumberType);
        const ObAccuracy &calc_acc = ObAccuracy::DDL_DEFAULT_ACCURACY[ObNumberType];
        type1.set_calc_accuracy(calc_acc);
      }
    } else {
      type1.set_calc_type(type1.get_type());
      if (ob_is_collection_sql_type(type1.get_type())) {
        type1.set_calc_subschema_id(type1.get_subschema_id()); // avoid invalid cast
      }
      // query range extract check the calc type of %type.
      type.set_calc_meta(type1.get_calc_meta());
    }
    type.set_type(ObInt32Type);
    type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
    type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
  }
  type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_NO_RANGE_CHECK);

  return ret;
}

int ObExprIsBase::is_infinite_nan(const SqlValueType datum_type,
                              Datum *datum,
                              bool &ret_bool,
                              Ieee754 inf_or_nan)
{
  int ret = OB_SUCCESS;
  if (inf_or_nan == Ieee754::INFINITE_VALUE) {
    if (ObFloatType == datum_type) {
      ret_bool = (fabs(datum->get_float()) == INFINITY ? true: false);
    } else if (ObDoubleType == datum_type) {
      ret_bool = (fabs(datum->get_double()) == INFINITY ? true: false);
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("is_infinite: invalid column type", K(datum_type));
    }
  } else {
    if (ObFloatType == datum_type) {
      ret_bool = isnan(fabs(datum->get_float()));
    } else if (ObDoubleType == datum_type) {
      ret_bool = isnan(fabs(datum->get_double()));
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("is_infinite: invalid column type", K(datum_type));
    }
  }

  return ret;
}

int ObExprIsBase::cg_expr_internal(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr, const ObConstRawExpr *&const_param2) const
{
 	UNUSED(op_cg_ctx);
  int ret = OB_SUCCESS;
  const ObOpRawExpr *op_raw_expr = static_cast<const ObOpRawExpr*>(&raw_expr);
  const ObRawExpr *child = NULL;
  if (rt_expr.arg_cnt_ != 2) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(rt_expr.args_) || OB_ISNULL(rt_expr.args_[0])
            || OB_ISNULL(rt_expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(child = op_raw_expr->get_param_expr(1))
             || OB_UNLIKELY(!child->is_const_raw_expr())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const_param2 = static_cast<const ObConstRawExpr *>(child);
  }
  return ret;
}

#define EVAL_FUNC(type)                         \
  if (is_not && is_true) {                      \
    eval_func = ObExprIsNot::type##_is_not_true;  \
  } else if (is_not && !is_true) {              \
    eval_func = ObExprIsNot::type##_is_not_false; \
  } else if (!is_not && is_true) {              \
    eval_func = ObExprIs::type##_is_true;         \
  } else if (!is_not && !is_true) {             \
    eval_func = ObExprIs::type##_is_false;        \
  }

int ObExprIsBase::cg_result_type_class(SqlValueType type, ObExpr::EvalFunc &eval_func, bool is_not,
                                        bool is_true) const {
  int ret = OB_SUCCESS;
  switch (type) {
    case ObTinyIntType:
    case ObSmallIntType:
    case ObMediumIntType:
    case ObInt32Type:
    case ObIntType:
    case ObUTinyIntType:
    case ObUSmallIntType:
    case ObUMediumIntType:
    case ObUInt32Type:
    case ObUInt64Type:
    case ObBitType: {
        EVAL_FUNC(int);
        break;
    }
    case ObFloatType:
    case ObUFloatType:{
        EVAL_FUNC(float);
        break;
    }
    case ObDoubleType:
    case ObUDoubleType: {
        EVAL_FUNC(double);
        break;
    }
    case ObDecimalIntType: {
      EVAL_FUNC(decimal_int);
      break;
    }
    case ObJsonType: {
      EVAL_FUNC(json);
      break;
    }
    case ObMaxType: {
        ret = OB_ERR_UNEXPECTED;
        break;
    }
    default: {
        EVAL_FUNC(number);
        break;
    }
  }
  return ret;
}

int ObExprIs::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  const ObConstRawExpr *param2 = NULL;
  const ObConstRawExpr *param3 = NULL;
  SqlValueType param1_type = ObMaxType;
  if (OB_FAIL(cg_expr_internal(op_cg_ctx, raw_expr, rt_expr, param2))) {
  } else if (OB_ISNULL(param2)) {
    ret = OB_ERR_UNEXPECTED;
  } else if(FALSE_IT(param1_type = rt_expr.args_[0]->datum_meta_.type_)) {
  } else if (param2->get_value_datum().is_null()) {  // c1 is null
    rt_expr.eval_func_ = ObExprIs::calc_is_null;
  } else if ((!param2->get_value_datum().is_null() && ob_is_int_tc(param2->get_value_meta().get_type()) && param2->get_value_datum().get_int() != 0)) {
    if (OB_FAIL(cg_result_type_class(param1_type, rt_expr.eval_func_, false, true))) {
    }
  } else if ((!param2->get_value_datum().is_null() && ob_is_int_tc(param2->get_value_meta().get_type()) && param2->get_value_datum().get_int() == 0)) {
    if (OB_FAIL(cg_result_type_class(param1_type, rt_expr.eval_func_, false, false))) {
    }
  } else if (ObDoubleType == param2->get_value_meta().get_type()) {
    if (isnan(param2->get_value_datum().get_double())) {
      rt_expr.eval_func_ = ObExprIs::calc_is_nan;
    } else {
      rt_expr.eval_func_ = ObExprIs::calc_is_infinite;
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObExprIsNot::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  const ObConstRawExpr *param2 = NULL;
  const ObConstRawExpr *param3 = NULL;
  SqlValueType param1_type = ObMaxType;
  if (OB_FAIL(cg_expr_internal(op_cg_ctx, raw_expr, rt_expr, param2))) {
  } else if (OB_ISNULL(param2)) {
    ret = OB_ERR_UNEXPECTED;
  } else if(FALSE_IT(param1_type = rt_expr.args_[0]->datum_meta_.type_)) {
  } else if (param2->get_value_datum().is_null()) {  // c1 is null
    rt_expr.eval_func_ = ObExprIsNot::calc_is_not_null;
    rt_expr.eval_batch_func_ = ObExprIsNot::calc_batch_is_not_null;
  } else if ((!param2->get_value_datum().is_null() && ob_is_int_tc(param2->get_value_meta().get_type()) && param2->get_value_datum().get_int() != 0)) {
    if (OB_FAIL(cg_result_type_class(param1_type, rt_expr.eval_func_, true, true))) {
    }
  } else if ((!param2->get_value_datum().is_null() && ob_is_int_tc(param2->get_value_meta().get_type()) && param2->get_value_datum().get_int() == 0)) {
    if (OB_FAIL(cg_result_type_class(param1_type, rt_expr.eval_func_, true, false))) {
    }
  } else if (ObDoubleType == param2->get_value_meta().get_type()) {
    if (isnan(param2->get_value_datum().get_double())) {
      rt_expr.eval_func_ = ObExprIsNot::calc_is_not_nan;
    } else {
      rt_expr.eval_func_ = ObExprIsNot::calc_is_not_infinite;
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObExprIs::calc_is_null(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum)
{
  int ret = OB_SUCCESS;
  Datum *param1 = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, param1))) {
  } else {
    bool ret_bool = param1->is_null();
    expr_datum.set_int32(static_cast<int32_t>(ret_bool));
  }
  return ret;
}

int ObExprIs::calc_is_infinite(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum)
{
  int ret = OB_SUCCESS;
  Datum *param1 = NULL;
  bool ret_bool = false;
  if (OB_FAIL(expr.args_[0]->eval(ctx, param1))) {
  } else if (param1->is_null()) {
    expr_datum.set_null();
  } else {
    if (OB_FAIL(is_infinite_nan(expr.args_[0]->datum_meta_.type_, param1,
                                ret_bool, Ieee754::INFINITE_VALUE))) {
    } else {
      expr_datum.set_int32(static_cast<int32_t>(ret_bool));
    }
  }
  return ret;
}

int ObExprIs::calc_is_nan(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum)
{
  int ret = OB_SUCCESS;
  Datum *param1 = NULL;
  bool ret_bool = false;
  if (OB_FAIL(expr.args_[0]->eval(ctx, param1))) {
  } else if (param1->is_null()) {
    expr_datum.set_null();
  } else {
    if (OB_FAIL(is_infinite_nan(expr.args_[0]->datum_meta_.type_, param1,
                                ret_bool, Ieee754::NAN_VALUE))) {
    } else {
      expr_datum.set_int32(static_cast<int32_t>(ret_bool));
    }
  }
  return ret;
}

int ObExprIsNot::calc_is_not_null(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum)
{
  int ret = OB_SUCCESS;
  Datum *param1 = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, param1))) {
  } else {
    bool ret_bool = !param1->is_null();
    expr_datum.set_int32(static_cast<int32_t>(ret_bool));
  }
  return ret;
}

int ObExprIsNot::calc_batch_is_not_null(const ObExpr &expr, ObEvalCtx &ctx,
                                        const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  Datum *results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < batch_size; i++) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      Datum &arg = expr.args_[0]->locate_expr_datum(ctx, i);
      bool v = !arg.is_null();
      results[i].set_int32(static_cast<int32_t>(v));
      eval_flags.set(i);
    }
  }
  return ret;
}

int ObExprIsNot::calc_is_not_infinite(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum)
{
  int ret = OB_SUCCESS;
  Datum *param1 = NULL;
  bool ret_bool = false;
  if (OB_FAIL(expr.args_[0]->eval(ctx, param1))) {
  } else if (param1->is_null()) {
    expr_datum.set_null();
  } else {
    if (OB_FAIL(is_infinite_nan(expr.args_[0]->datum_meta_.type_, param1,
                                ret_bool, Ieee754::INFINITE_VALUE))) {
    } else {
      expr_datum.set_int32(static_cast<int32_t>(!ret_bool));
    }
  }
  return ret;
}

int ObExprIsNot::calc_is_not_nan(const ObExpr &expr, ObEvalCtx &ctx, Datum &expr_datum)
{
  int ret = OB_SUCCESS;
  Datum *param1 = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, param1))) {
  } else if (param1->is_null()) {
    expr_datum.set_null();
  } else {
    bool ret_bool = false;
    if (OB_FAIL(is_infinite_nan(expr.args_[0]->datum_meta_.type_, param1,
                                ret_bool, Ieee754::NAN_VALUE))) {
    } else {
      expr_datum.set_int32(static_cast<int32_t>(!ret_bool));
    }
  }
  return ret;
}

int ObExprInnerIsTrue::calc_result_type2(ObExprResType &type,
                                         ObExprResType &type1,
                                         ObExprResType &type2,
                                         ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObRawExpr *raw_expr = get_raw_expr();
  if (OB_ISNULL(raw_expr) || OB_UNLIKELY(raw_expr->get_param_count() != 2)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(type1.is_ext())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected type", K(type1));
  } else {
    type.set_ext();
    type.set_precision(DEFAULT_PRECISION_FOR_TEMPORAL);
    type.set_scale(SCALE_UNKNOWN_YET);
    type.set_result_flag(NOT_NULL_FLAG);
    type2.set_calc_type(type2.get_type());
    if (ob_is_numeric_type(type1.get_type())) {
      type1.set_calc_meta(type1.get_sql_type_meta());
      type1.set_calc_accuracy(type1.get_accuracy());
    } else if (ob_is_json(type1.get_type())) {
      type1.set_calc_meta(type1.get_sql_type_meta());
      type1.set_calc_accuracy(type1.get_accuracy());
    } else {
      type1.set_calc_type(ObNumberType);
      const ObAccuracy &calc_acc = ObAccuracy::DDL_DEFAULT_ACCURACY[ObNumberType];
      type1.set_calc_accuracy(calc_acc);
    }
  }
  type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_NO_RANGE_CHECK);

  return ret;
}

int ObExprInnerIsTrue::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  const ObConstRawExpr *param2 = NULL;
  SqlValueType param1_type = ObMaxType;
  if (rt_expr.arg_cnt_ != 2) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    OB_ASSERT(rt_expr.args_ != nullptr && rt_expr.args_[0] != nullptr && rt_expr.args_[1] != nullptr);
    if (OB_ISNULL(param2 = static_cast<const ObConstRawExpr *>(raw_expr.get_param_expr(1)))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_UNLIKELY(!param2->get_value_meta().is_int())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("const value is not int type", KPC(param2));
    } else {
      param1_type = rt_expr.args_[0]->datum_meta_.type_;
      bool is_start = param2->get_value_datum().get_int() > 0;
      switch (param1_type) {
      case ObTinyIntType:
      case ObSmallIntType:
      case ObMediumIntType:
      case ObInt32Type:
      case ObIntType:
      case ObUTinyIntType:
      case ObUSmallIntType:
      case ObUMediumIntType:
      case ObUInt32Type:
      case ObUInt64Type:
      case ObBitType: {
          rt_expr.eval_func_ = is_start ? ObExprInnerIsTrue::int_is_true_start
                                        : ObExprInnerIsTrue::int_is_true_end;
          break;
      }
      case ObFloatType:
      case ObUFloatType:{
          rt_expr.eval_func_ = is_start ? ObExprInnerIsTrue::float_is_true_start
                                        : ObExprInnerIsTrue::float_is_true_end;
          break;
      }
      case ObDoubleType:
      case ObUDoubleType: {
          rt_expr.eval_func_ = is_start ? ObExprInnerIsTrue::double_is_true_start
                                        : ObExprInnerIsTrue::double_is_true_end;
          break;
      }
      case ObDecimalIntType: {
          rt_expr.eval_func_ = is_start ? ObExprInnerIsTrue::decimal_int_is_true_start
                                        : ObExprInnerIsTrue::decimal_int_is_true_end;
        break;
      }
      case ObJsonType: {
          rt_expr.eval_func_ = is_start ? ObExprInnerIsTrue::json_is_true_start
                                        : ObExprInnerIsTrue::json_is_true_end;
        break;
      }
      case ObMaxType: {
          ret = OB_ERR_UNEXPECTED;
          break;
      }
      default: {
          rt_expr.eval_func_ = is_start ? ObExprInnerIsTrue::number_is_true_start
                                        : ObExprInnerIsTrue::number_is_true_end;
          break;
      }
    }
    }
  }
  return ret;
}

template <typename T>
int ObExprIsBase::is_zero(T number, const uint32_t len)
{
  UNUSED(len);
  return 0 == number;
}
template <>
int ObExprIsBase::is_zero<number::ObCompactNumber>(number::ObCompactNumber number,
                                                   const uint32_t len)
{
  UNUSED(len);
  return number.is_zero();
}

template <>
int ObExprIsBase::is_zero<const ObDecimalInt *>(const ObDecimalInt *decimal_int, const uint32_t len)
{
  const char *data = reinterpret_cast<const char *>(decimal_int);
  bool is_zero = true;
  for (int64_t i = 0; i < len; ++i) {
    if (data[i] != 0) {
      is_zero = false;
      break;
    }
  }
  return is_zero;
}

namespace
{
#define SCALAR_ZERO_READER(kind) \
int read_zero_##kind(const ObExpr &, const Datum &value, bool &zero) \
{ zero = ObExprIsBase::is_zero(value.get_##kind(), value.length()); return OB_SUCCESS; }
SCALAR_ZERO_READER(int)
SCALAR_ZERO_READER(float)
SCALAR_ZERO_READER(double)
SCALAR_ZERO_READER(number)
SCALAR_ZERO_READER(json)
#undef SCALAR_ZERO_READER

int read_zero_decimal_int(const ObExpr &expr, const Datum &value, bool &zero)
{
  alignas(8) char scratch[64];
  common::ObDatum coefficient;
  const int ret = expr.args_[0]->get_datum_copy_plan().get_decimal(value, coefficient, scratch, sizeof(scratch));
  if (ret == OB_SUCCESS) {
    zero = ObExprIsBase::is_zero(coefficient.get_decimal_int(), coefficient.get_int_bytes());
  }
  return ret;
}
} // namespace

#define NUMERIC_CALC_FUNC(type, func_name, bool_is_not, is_true, str_is_not)                \
  int func_name::type##_##str_is_not##_##is_true(const ObExpr &expr, ObEvalCtx &ctx,        \
                                                Datum &expr_datum)                    \
  {                                                           \
    int ret = OB_SUCCESS;                                     \
    Datum *param1 = NULL;                                   \
    bool ret_bool = false;                                    \
    if (OB_FAIL(expr.args_[0]->eval(ctx, param1))) {          \
      LOG_WARN("eval first param failed", K(ret));            \
    } else if (param1->is_null()) {                           \
      ret_bool = bool_is_not;                                 \
    } else {                                                  \
      bool zero = false;                                      \
      if (OB_SUCC(ret = read_zero_##type(expr, *param1, zero))) { \
        ret_bool = bool_is_not == is_true ? zero : !zero;       \
      }                                                       \
    }                                                         \
    if (OB_SUCC(ret)) {                                       \
      expr_datum.set_int32(static_cast<int32_t>(ret_bool));   \
    }                                                         \
    return ret;                                               \
  }

NUMERIC_CALC_FUNC(int, ObExprIsNot, true, true, is_not)
NUMERIC_CALC_FUNC(float, ObExprIsNot, true, true, is_not)
NUMERIC_CALC_FUNC(double, ObExprIsNot, true, true, is_not)
NUMERIC_CALC_FUNC(number, ObExprIsNot, true, true, is_not)
NUMERIC_CALC_FUNC(decimal_int, ObExprIsNot, true, true, is_not)
NUMERIC_CALC_FUNC(json, ObExprIsNot, true, true, is_not)


NUMERIC_CALC_FUNC(int, ObExprIsNot, true, false, is_not)
NUMERIC_CALC_FUNC(float, ObExprIsNot, true, false, is_not)
NUMERIC_CALC_FUNC(double, ObExprIsNot, true, false, is_not)
NUMERIC_CALC_FUNC(number, ObExprIsNot, true, false, is_not)
NUMERIC_CALC_FUNC(decimal_int, ObExprIsNot, true, false, is_not)
NUMERIC_CALC_FUNC(json, ObExprIsNot, true, false, is_not)

NUMERIC_CALC_FUNC(int, ObExprIs, false, true, is)
NUMERIC_CALC_FUNC(float, ObExprIs, false, true, is)
NUMERIC_CALC_FUNC(double, ObExprIs, false, true, is)
NUMERIC_CALC_FUNC(number, ObExprIs, false, true, is)
NUMERIC_CALC_FUNC(decimal_int, ObExprIs, false, true, is)
NUMERIC_CALC_FUNC(json, ObExprIs, false, true, is)

NUMERIC_CALC_FUNC(int, ObExprIs, false, false, is)
NUMERIC_CALC_FUNC(float, ObExprIs, false, false, is)
NUMERIC_CALC_FUNC(double, ObExprIs, false, false, is)
NUMERIC_CALC_FUNC(number, ObExprIs, false, false, is)
NUMERIC_CALC_FUNC(decimal_int, ObExprIs, false, false, is)
NUMERIC_CALC_FUNC(json, ObExprIs, false, false, is)

#define INNER_NUMERIC_CALC_FUNC(type, is_start, str_pos)                          \
  int ObExprInnerIsTrue::type##_##is_true##_##str_pos(const ObExpr &expr, ObEvalCtx &ctx,     \
                                                      Datum &expr_datum)        \
  {                                                                               \
    int ret = OB_SUCCESS;                                                         \
    Datum *param1 = NULL;                                                       \
    bool ret_bool = false;                                                        \
    if (OB_FAIL(expr.args_[0]->eval(ctx, param1))) {                              \
      LOG_WARN("eval first param failed", K(ret));                                \
    } else if (param1->is_null()) {                                               \
      ret_bool = false;                                                           \
    } else {                                                                      \
      bool zero = false;                                                         \
      if (OB_SUCC(ret = read_zero_##type(expr, *param1, zero))) { ret_bool = !zero; } \
    }                                                                             \
    if (OB_SUCC(ret)) {                                                           \
      if ((ret_bool && is_start) || (!ret_bool && !is_start)) {                   \
        expr_datum.set_min();                                                   \
      } else {                                                                    \
        expr_datum.set_max();                                                   \
      }                                                                           \
    }                                                                             \
    return ret;                                                                   \
  }

INNER_NUMERIC_CALC_FUNC(int, true, start)
INNER_NUMERIC_CALC_FUNC(float, true, start)
INNER_NUMERIC_CALC_FUNC(double, true, start)
INNER_NUMERIC_CALC_FUNC(number, true, start)
INNER_NUMERIC_CALC_FUNC(decimal_int, true, start)
INNER_NUMERIC_CALC_FUNC(json, true, start)

INNER_NUMERIC_CALC_FUNC(int, false, end)
INNER_NUMERIC_CALC_FUNC(float, false, end)
INNER_NUMERIC_CALC_FUNC(double, false, end)
INNER_NUMERIC_CALC_FUNC(number, false, end)
INNER_NUMERIC_CALC_FUNC(decimal_int, false, end)
INNER_NUMERIC_CALC_FUNC(json, false, end)

}
}
