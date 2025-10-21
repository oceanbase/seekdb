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

#include "sql/engine/expr/ob_expr_minus.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"
#include "sql/engine/expr/ob_batch_eval_util.h"
#include "sql/engine/expr/ob_rt_datum_arith.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"

namespace oceanbase
{
using namespace common;
using namespace common::number;
using namespace oceanbase::lib;

namespace sql
{

ObExprMinus::ObExprMinus(ObIAllocator &alloc, ObExprOperatorType type)
  : ObArithExprOperator(alloc,
                        type,
                        N_MINUS,
                        2,
                        NOT_ROW_DIMENSION,
                        ObExprResultTypeUtil::get_minus_result_type,
                        ObExprResultTypeUtil::get_minus_calc_type,
                        minus_funcs_)
{
  param_lazy_eval_ = false;
}

int ObExprMinus::calc_result_type2(ObExprResType &type,
                                   ObExprResType &type1,
                                   ObExprResType &type2,
                                   ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  static const int64_t CARRY_OFFSET = 1;
  ObScale scale = SCALE_UNKNOWN_YET;
  ObPrecision precision = PRECISION_UNKNOWN_YET;
  const ObSQLSessionInfo *session = nullptr;
  const bool is_all_decint_args =
    ob_is_decimal_int(type1.get_type()) && ob_is_decimal_int(type2.get_type());
  if (OB_ISNULL(session = type_ctx.get_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get mysession", K(ret));
  } else if (OB_FAIL(ObArithExprOperator::calc_result_type2(type, type1, type2, type_ctx))) {
    LOG_WARN("fail to calc result type", K(ret), K(type), K(type1), K(type2));
  } else if (type.is_decimal_int() && (type1.is_null() || type2.is_null())) {
    type.set_precision(MAX(type1.get_precision(), type2.get_precision()));
    type.set_scale(MAX(type1.get_scale(), type2.get_scale()));
  } else if (type.is_collection_sql_type()) {
    if (type1.is_collection_sql_type() && type2.is_collection_sql_type()) {
      ObSQLSessionInfo *session = const_cast<ObSQLSessionInfo *>(type_ctx.get_session());
      ObExecContext *exec_ctx = OB_ISNULL(session) ? NULL : session->get_cur_exec_ctx();
      ObCollectionTypeBase *coll_type1 = NULL;
      ObCollectionTypeBase *coll_type2 = NULL;
      ObExprResType coll_calc_type = type;
      if (OB_ISNULL(exec_ctx)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("exec ctx is null", K(ret));
      } else if (OB_FAIL(ObArrayExprUtils::get_coll_type_by_subschema_id(exec_ctx, type1.get_subschema_id(), coll_type1))) {
        LOG_WARN("failed to get array type by subschema id", K(ret), K(type1.get_subschema_id()));
      } else if (coll_type1->type_id_ != ObNestedType::OB_ARRAY_TYPE && coll_type1->type_id_ != ObNestedType::OB_VECTOR_TYPE) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        LOG_WARN("invalid collection type", K(ret), K(coll_type1->type_id_));
      } else if (OB_FAIL(ObArrayExprUtils::get_coll_type_by_subschema_id(exec_ctx, type2.get_subschema_id(), coll_type2))) {
        LOG_WARN("failed to get array type by subschema id", K(ret), K(type2.get_subschema_id()));
      } else if (coll_type2->type_id_ != ObNestedType::OB_ARRAY_TYPE && coll_type2->type_id_ != ObNestedType::OB_VECTOR_TYPE) {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        LOG_WARN("invalid collection type", K(ret), K(coll_type2->type_id_));
      } else if (OB_FAIL(ObExprResultTypeUtil::get_array_calc_type(exec_ctx, type1, type2, coll_calc_type))) {
        LOG_WARN("failed to check array compatibilty", K(ret));
      } else {
        type1.set_calc_meta(coll_calc_type);
        type2.set_calc_meta(coll_calc_type);
        type.set_collection(coll_calc_type.get_subschema_id());
      }
    } else {
      // only support vector/array/varchar - vector/array/varchar now // array and varchar need cast to array(float)
      uint16_t res_subschema_id = UINT16_MAX;
      if (OB_FAIL(ObArrayExprUtils::calc_cast_type2(type_, type1, type2, type_ctx, res_subschema_id))) {
        LOG_WARN("failed to calc cast type", K(ret), K(type1));
      } else if (UINT16_MAX == res_subschema_id) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected result subschema_id", K(ret));
      } else {
        type.set_collection(res_subschema_id);
      }
    }
  } else if (OB_UNLIKELY(SCALE_UNKNOWN_YET == type1.get_scale() ||
                         SCALE_UNKNOWN_YET == type2.get_scale())) {
    type.set_scale(NUMBER_SCALE_UNKNOWN_YET);
    type.set_precision(PRECISION_UNKNOWN_YET);
  } else if (type1.get_type_class() == ObIntervalTC
             || type2.get_type_class() == ObIntervalTC) {
    type.set_scale(ObAccuracy::MAX_ACCURACY2[ORACLE_MODE][type.get_type()].get_scale());
    type.set_precision(ObAccuracy::MAX_ACCURACY2[ORACLE_MODE][type.get_type()].get_precision());
  } else {
    ObScale scale1 = static_cast<ObScale>(MAX(type1.get_scale(), 0));
    ObScale scale2 = static_cast<ObScale>(MAX(type2.get_scale(), 0));
    scale = MAX(scale1, scale2);
    if (lib::is_mysql_mode() && type.is_double()) {
      precision = ObMySQLUtil::float_length(scale);
    } else if (type.has_result_flag(DECIMAL_INT_ADJUST_FLAG)) {
      precision = MAX(type1.get_precision(), type2.get_precision());
    } else {
      int64_t inter_part_length1 = type1.get_precision() - type1.get_scale();
      int64_t inter_part_length2 = type2.get_precision() - type2.get_scale();
      precision = static_cast<ObPrecision>(MAX(inter_part_length1, inter_part_length2)
                                          + CARRY_OFFSET + scale);
      precision = MIN(OB_MAX_DECIMAL_POSSIBLE_PRECISION, precision);
    }
    type.set_scale(scale);

    if (OB_UNLIKELY(PRECISION_UNKNOWN_YET == type1.get_precision()) ||
        OB_UNLIKELY(PRECISION_UNKNOWN_YET == type2.get_precision())) {
      type.set_precision(PRECISION_UNKNOWN_YET);
    } else {
      type.set_precision(precision);
    }
    if (lib::is_mysql_mode() && is_no_unsigned_subtraction(session->get_sql_mode())) {
      ObObjType convert_type = type.get_type();
      convert_unsigned_type_to_signed(convert_type);
      type.set_type(convert_type);
    }
    if (is_all_decint_args || type.is_decimal_int()) {
      if (OB_UNLIKELY(PRECISION_UNKNOWN_YET == type.get_precision() ||
                      SCALE_UNKNOWN_YET == type.get_scale())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected decimal int precision and scale", K(ret), K(type));
      } else {
        if (ObRawExprUtils::decimal_int_need_cast(type1.get_accuracy(), type.get_accuracy()) ||
              ObRawExprUtils::decimal_int_need_cast(type2.get_accuracy(), type.get_accuracy())) {
          type.set_result_flag(DECIMAL_INT_ADJUST_FLAG);
        }
        type1.set_calc_accuracy(type.get_accuracy());
        type2.set_calc_accuracy(type.get_accuracy());
      }
      LOG_DEBUG("calc_result_type2", K(type.get_accuracy()), K(type1.get_accuracy()),
                                     K(type2.get_accuracy()));
    }
    if ((ob_is_double_tc(type.get_type()) || ob_is_float_tc(type.get_type())) && type.get_scale() > 0) {
      // if result is fixed double/float, calc type's of params should also be fixed double/float
      if (ob_is_double_tc(type1.get_calc_type()) || ob_is_float_tc(type1.get_calc_type())) {
        type1.set_calc_scale(type.get_scale());
      }
      if (ob_is_double_tc(type2.get_calc_type()) || ob_is_float_tc(type2.get_calc_type())) {
        type2.set_calc_scale(type.get_scale());
      }
    }
    LOG_DEBUG("calc_result_type2", K(scale), K(type1), K(type2), K(type), K(precision));
  }
  return ret;
}



ObArithFunc ObExprMinus::minus_funcs_[ObMaxTC] =
{
  NULL,
  ObExprMinus::minus_int,
  ObExprMinus::minus_uint,
  NULL,
  ObExprMinus::minus_double,
  ObExprMinus::minus_number,
  ObExprMinus::minus_datetime,//datetime
  NULL,//date
  NULL,//time
  NULL,//year
  NULL,//string
  NULL,//extend
  NULL,//unknown
  NULL,//text
  NULL,//bit
  NULL,//enumset
  NULL,//enumsetInner
};

ObArithFunc ObExprMinus::agg_minus_funcs_[ObMaxTC] =
{
  NULL,
  ObExprMinus::minus_int,
  ObExprMinus::minus_uint,
  NULL,
  ObExprMinus::minus_double_no_overflow,
  ObExprMinus::minus_number,
  ObExprMinus::minus_datetime,//datetime
  NULL,//date
  NULL,//time
  NULL,//year
  NULL,//string
  NULL,//extend
  NULL,//unknown
  NULL,//text
  NULL,//bit
  NULL,//enumset
  NULL,//enumsetInner
};

int ObExprMinus::minus_int(ObObj &res,
                           const ObObj &left,
                           const ObObj &right,
                           ObIAllocator *allocator,
                           ObScale scale)
{
  int ret = OB_SUCCESS;
  int64_t left_i = left.get_int();
  int64_t right_i = right.get_int();
  char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
  int64_t pos = 0;
  if (left.get_type_class() == right.get_type_class()) {
    res.set_int(left_i - right_i);
    if (OB_UNLIKELY(is_int_int_out_of_range(left_i, right_i, res.get_int()))) {
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld - %ld)'",
                      left_i,
                      right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT", expr_str);
    }
  } else if (OB_UNLIKELY(ObUIntTC != right.get_type_class())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid types", K(ret), K(left), K(right));
  } else {
    res.set_uint64(left_i - right_i);
    if (OB_UNLIKELY(is_int_uint_out_of_range(left_i, right_i, res.get_uint64()))) {
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld - %lu)'",
                      left_i,
                      right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
  }
  UNUSED(allocator);
  UNUSED(scale);
  return ret;
}

int ObExprMinus::minus_uint(ObObj &res,
                            const ObObj &left,
                            const ObObj &right,
                            ObIAllocator *allocator,
                            ObScale scale)
{
  int ret = OB_SUCCESS;
  uint64_t left_i = left.get_uint64();
  uint64_t right_i = right.get_uint64();
  res.set_uint64(left_i - right_i);
  char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
  int64_t pos = 0;
  if (left.get_type_class() == right.get_type_class()) {
    if (OB_UNLIKELY(is_uint_uint_out_of_range(left_i, right_i, res.get_uint64()))) {
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%lu - %lu)'",
                      left_i,
                      right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
  } else if (OB_UNLIKELY(ObIntTC != right.get_type_class())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid types", K(ret), K(left), K(right));
  } else {
    if (OB_UNLIKELY(is_uint_int_out_of_range(right_i, left_i, res.get_uint64()))) {
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%lu - %ld)'",
                      left_i,
                      right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
  }
  UNUSED(allocator);
  UNUSED(scale);
  return ret;
}

int ObExprMinus::minus_double(ObObj &res,
                              const ObObj &left,
                              const ObObj &right,
                              ObIAllocator *allocator,
                              ObScale scale)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(left.get_type_class() != right.get_type_class())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid types", K(ret), K(left), K(right));
  } else {
    double left_d = left.get_double();
    double right_d = right.get_double();
    res.set_double(left_d - right_d);
    if (OB_UNLIKELY(is_double_out_of_range(res.get_double()))) {
      ret = OB_OPERATE_OVERFLOW;
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%e - %e)'",
                      left_d,
                      right_d);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DOUBLE", expr_str);
      LOG_WARN("double out of range", K(res), K(left), K(right), K(res));
      res.set_null();
    }
    LOG_DEBUG("succ to minus double", K(res), K(left), K(right));
  }
  UNUSED(allocator);
  UNUSED(scale);
  return ret;
}

int ObExprMinus::minus_double_no_overflow(ObObj &res,
                                          const ObObj &left,
                                          const ObObj &right,
                                          ObIAllocator *,
                                          ObScale)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(left.get_type_class() != right.get_type_class())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid types", K(ret), K(left), K(right));
  } else {
    double left_d = left.get_double();
    double right_d = right.get_double();
    res.set_double(left_d - right_d);
  }
  return ret;
}

int ObExprMinus::minus_number(ObObj &res,
                              const ObObj &left,
                              const ObObj &right,
                              ObIAllocator *allocator,
                              ObScale scale)
{
  int ret = OB_SUCCESS;
  number::ObNumber res_nmb;
  if (OB_UNLIKELY(NULL == allocator)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("allocator is null", K(ret));
  } else if (OB_FAIL(left.get_number().sub_v3(right.get_number(), res_nmb, *allocator))) {
    LOG_WARN("failed to sub numbers", K(ret), K(left), K(right));
  } else {
    if (ObUNumberType == res.get_type()) {
      res.set_unumber(res_nmb);
    } else {
      res.set_number(res_nmb);
    }
  }
  UNUSED(scale);
  return ret;
}

int ObExprMinus::minus_datetime(ObObj &res, const ObObj &left, const ObObj &right,
    ObIAllocator *allocator, ObScale scale)
{
  int ret = OB_SUCCESS;
  const int64_t left_i = left.get_datetime();
  const int64_t right_i = right.get_datetime();
  ObTime ob_time;
  if (OB_LIKELY(left.get_type_class() == right.get_type_class())) {
    int64_t round_value = left_i - right_i;
    ObTimeConverter::round_datetime(OB_MAX_DATE_PRECISION, round_value);
    res.set_datetime(round_value);
    res.set_scale(OB_MAX_DATE_PRECISION);
    if (OB_UNLIKELY(res.get_datetime() > DATETIME_MAX_VAL || res.get_datetime() < DATETIME_MIN_VAL)
        || (OB_FAIL(ObTimeConverter::datetime_to_ob_time(res.get_datetime(), NULL, ob_time)))
        || (OB_FAIL(ObTimeConverter::validate_oracle_date(ob_time)))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      int64_t pos = 0;
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld - %ld)'", left_i, right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DATE", expr_str);
    }
  }
  LOG_DEBUG("minus datetime", K(left), K(right), K(ob_time), K(res));
  UNUSED(allocator);
  UNUSED(scale);
  return ret;
}

int ObExprMinus::cg_expr(ObExprCGCtx &op_cg_ctx,
                         const ObRawExpr &raw_expr,
                         ObExpr &rt_expr) const
{
#define SET_MINUS_FUNC_PTR(v) \
  rt_expr.eval_func_ = ObExprMinus::v; \
  rt_expr.eval_batch_func_ = ObExprMinus::v##_batch;

  int ret = OB_SUCCESS;
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  if (rt_expr.arg_cnt_ != 2 || OB_ISNULL(rt_expr.args_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("count of children is not 2 or children is null", K(ret), K(rt_expr.arg_cnt_),
                                                            K(rt_expr.args_));
  } else if (OB_ISNULL(rt_expr.args_[0]) || OB_ISNULL(rt_expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("child is null", K(ret), K(rt_expr.args_[0]), K(rt_expr.args_[1]));
  } else {
    rt_expr.eval_func_ = NULL;
    rt_expr.may_not_need_raw_check_ = false;
    const ObObjType left_type = rt_expr.args_[0]->datum_meta_.type_;
    const ObObjType right_type = rt_expr.args_[1]->datum_meta_.type_;
    const ObObjType result_type = rt_expr.datum_meta_.type_;
    const ObObjTypeClass left_tc = ob_obj_type_class(left_type);
    const ObObjTypeClass right_tc = ob_obj_type_class(right_type);

    switch (result_type) {
      case ObIntType:
        rt_expr.may_not_need_raw_check_ = true;
        SET_MINUS_FUNC_PTR(minus_int_int);
        rt_expr.eval_vector_func_ = minus_int_int_vector;
        break;
      case ObUInt64Type:
        if (ObIntTC == left_tc && ObUIntTC == right_tc) {
          SET_MINUS_FUNC_PTR(minus_int_uint);
          rt_expr.eval_vector_func_ = minus_int_uint_vector;
        } else if (ObUIntTC == left_tc && ObIntTC == right_tc) {
          SET_MINUS_FUNC_PTR(minus_uint_int);
          rt_expr.eval_vector_func_ = minus_uint_int_vector;
        } else if (ObUIntTC == left_tc && ObUIntTC == right_tc) {
          SET_MINUS_FUNC_PTR(minus_uint_uint);
          rt_expr.eval_vector_func_ = minus_uint_uint_vector;
        }
        break;
      case ObDateTimeType:
        if (ObDateTimeType == left_type && ObNumberType == right_type) {
          SET_MINUS_FUNC_PTR(minus_datetime_number);
        } else {
          SET_MINUS_FUNC_PTR(minus_datetime_datetime);
        }
        break;
      case ObFloatType:
        SET_MINUS_FUNC_PTR(minus_float_float);
        rt_expr.eval_vector_func_ = minus_float_float_vector;
        break;
      case ObDoubleType:
        SET_MINUS_FUNC_PTR(minus_double_double);
        rt_expr.eval_vector_func_ = minus_double_double_vector;
        break;
      case ObUNumberType:
      case ObNumberType:
        if (ObDateTimeType == left_type) {
          SET_MINUS_FUNC_PTR(minus_datetime_datetime_oracle);
        } else if (ob_is_decimal_int(left_type) && ob_is_decimal_int(right_type)) {
          switch (get_decimalint_type(rt_expr.args_[0]->datum_meta_.precision_)) {
            case DECIMAL_INT_32:
              SET_MINUS_FUNC_PTR(minus_decimalint32_oracle);
              rt_expr.eval_vector_func_ = minus_decimalint32_oracle_vector;
              break;
            case DECIMAL_INT_64:
              SET_MINUS_FUNC_PTR(minus_decimalint64_oracle);
              rt_expr.eval_vector_func_ = minus_decimalint64_oracle_vector;
              break;
            case DECIMAL_INT_128:
              SET_MINUS_FUNC_PTR(minus_decimalint128_oracle);
              rt_expr.eval_vector_func_ = minus_decimalint128_oracle_vector;
              break;
            default:
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected precision", K(ret), K(rt_expr.datum_meta_));
              break;
          }
        } else {
          SET_MINUS_FUNC_PTR(minus_number_number);
          rt_expr.eval_vector_func_ = minus_number_number_vector;
        }
        break;
      case ObDecimalIntType:
        switch (get_decimalint_type(rt_expr.datum_meta_.precision_)) {
          case DECIMAL_INT_32:
            SET_MINUS_FUNC_PTR(minus_decimalint32);
            rt_expr.eval_vector_func_ = minus_decimalint32_vector;
            break;
          case DECIMAL_INT_64:
            SET_MINUS_FUNC_PTR(minus_decimalint64);
            rt_expr.eval_vector_func_ = minus_decimalint64_vector;
            break;
          case DECIMAL_INT_128:
            SET_MINUS_FUNC_PTR(minus_decimalint128);
            rt_expr.eval_vector_func_ = minus_decimalint128_vector;
            break;
          case DECIMAL_INT_256:
            SET_MINUS_FUNC_PTR(minus_decimalint256);
            rt_expr.eval_vector_func_ = minus_decimalint256_vector;
            break;
          case DECIMAL_INT_512:
            if (rt_expr.datum_meta_.precision_ < OB_MAX_DECIMAL_POSSIBLE_PRECISION) {
              SET_MINUS_FUNC_PTR(minus_decimalint512);
              rt_expr.eval_vector_func_ = minus_decimalint512_vector;
            } else {
              SET_MINUS_FUNC_PTR(minus_decimalint512_with_check);
              rt_expr.eval_vector_func_ = minus_decimalint512_with_check_vector;
            }
            break;
          default:
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected precision", K(ret), K(rt_expr.datum_meta_));
            break;
        }
        break;
        case ObCollectionSQLType: {
          ObExecContext *exec_ctx = op_cg_ctx.session_->get_cur_exec_ctx();
          const uint16_t sub_id = rt_expr.obj_meta_.get_subschema_id();
          ObObjType elem_type;
          uint32_t unused;
          bool is_vec = false;
          if (OB_FAIL(ObArrayExprUtils::get_array_element_type(exec_ctx, sub_id, elem_type, unused, is_vec))) {
            LOG_WARN("failed to get collection elem type", K(ret), K(sub_id));
          } else if (elem_type == ObTinyIntType) {
            SET_MINUS_FUNC_PTR(minus_collection_collection_int8_t);
            rt_expr.eval_vector_func_ = minus_collection_collection_int8_t_vector;
          } else if (elem_type == ObSmallIntType) {
            SET_MINUS_FUNC_PTR(minus_collection_collection_int16_t);
            rt_expr.eval_vector_func_ = minus_collection_collection_int16_t_vector;
          } else if (elem_type == ObInt32Type) {
            SET_MINUS_FUNC_PTR(minus_collection_collection_int32_t);
            rt_expr.eval_vector_func_ = minus_collection_collection_int32_t_vector;
          } else if (elem_type == ObIntType) {
            SET_MINUS_FUNC_PTR(minus_collection_collection_int64_t);
            rt_expr.eval_vector_func_ = minus_collection_collection_int64_t_vector;
          } else if (elem_type == ObFloatType) {
            SET_MINUS_FUNC_PTR(minus_collection_collection_float);
            rt_expr.eval_vector_func_ = minus_collection_collection_float_vector;
          } else if (elem_type == ObDoubleType) {
            SET_MINUS_FUNC_PTR(minus_collection_collection_double);
            rt_expr.eval_vector_func_ = minus_collection_collection_double_vector;
          } else if (elem_type == ObUInt64Type) {
            SET_MINUS_FUNC_PTR(minus_collection_collection_uint64_t);
            rt_expr.eval_vector_func_ = minus_collection_collection_uint64_t_vector;
          } else {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("invalid element type for array operation", K(ret), K(elem_type));
          }
        }
        break;
      default:
        break;
    }
    if (OB_ISNULL(rt_expr.eval_func_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unexpected params type.", K(ret), K(left_type), K(right_type), K(result_type));
    }
  }
  return ret;
#undef SET_MINUS_FUNC_PTR
}

struct ObIntIntBatchMinusRaw : public ObArithOpRawType<int64_t, int64_t, int64_t>
{
  static void raw_op(int64_t &res, const int64_t l, const int64_t r)
  {
    res = l - r;
  }

  static int raw_check(const int64_t res, const int64_t l, const int64_t r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMinus::is_int_int_out_of_range(l, r, res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld - %ld)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT", expr_str);
    }
    return ret;
  }
};

//calc_type is IntTC  left and right has same TC
int ObExprMinus::minus_int_int(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObIntIntBatchMinusRaw>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_int_int_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObIntIntBatchMinusRaw>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_int_int_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObIntIntBatchMinusRaw>>(VECTOR_EVAL_FUNC_ARG_LIST);
}


struct ObIntUIntBatchMinusRaw : public ObArithOpRawType<uint64_t, int64_t, uint64_t>
{
  static void raw_op(uint64_t &res, const int64_t l, const uint64_t r)
  {
    res = l - r;
  }

  static int raw_check(const uint64_t res, const int64_t l, const uint64_t r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMinus::is_int_uint_out_of_range(l, r, res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld - %lu)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
    return ret;
  }
};

// calc_type/left_type is IntTC, right is ObUIntTC, only mysql mode
int ObExprMinus::minus_int_uint(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObIntUIntBatchMinusRaw>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_int_uint_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObIntUIntBatchMinusRaw>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_int_uint_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObIntUIntBatchMinusRaw>>(
    VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObUIntUIntBatchMinusRaw : public ObArithOpRawType<uint64_t, uint64_t, uint64_t>
{
  static void raw_op(uint64_t &res, const uint64_t l, const uint64_t r)
  {
    res = l - r;
  }

  static int raw_check(const uint64_t res, const uint64_t l, const uint64_t r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMinus::is_uint_uint_out_of_range(l, r, res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%lu - %lu)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
    return ret;
  }
};

//calc_type is UIntTC  left and right has same TC
int ObExprMinus::minus_uint_uint(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObUIntUIntBatchMinusRaw>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_uint_uint_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObUIntUIntBatchMinusRaw>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_uint_uint_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObUIntUIntBatchMinusRaw>>(
    VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObUIntIntBatchMinusRaw : public ObArithOpRawType<uint64_t, uint64_t, int64_t>
{
  static void raw_op(uint64_t &res, const uint64_t l, const int64_t r)
  {
    res = l - r;
  }

  static int raw_check(const uint64_t res, const uint64_t l, const int64_t r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMinus::is_uint_int_out_of_range(r, l, res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%lu - %ld)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
    return ret;
  }
};

// calc_type/left_tpee is UIntTC , right is intTC. only mysql mode
int ObExprMinus::minus_uint_int(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObUIntIntBatchMinusRaw>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_uint_int_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObUIntIntBatchMinusRaw>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_uint_int_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObUIntIntBatchMinusRaw>>(
    VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObFloatBatchMinusRawNoCheck : public ObArithOpRawType<float, float, float>
{
  static void raw_op(float &res, const float l, const float r)
  {
    res = l - r;
  }

};

struct ObFloatBatchMinusRawWithCheck: public ObFloatBatchMinusRawNoCheck
{
  static int raw_check(const float res, const float l, const float r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMinus::is_float_out_of_range(res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%e - %e)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "FLOAT", expr_str);
      LOG_WARN("float out of range", K(l), K(r), K(res));
    }
    return ret;
  }
};

//calc type is floatTC, left and right has same TC, only oracle mode
int ObExprMinus::minus_float_float(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObFloatBatchMinusRawWithCheck>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_float_float_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObFloatBatchMinusRawWithCheck>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_float_float_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObFloatBatchMinusRawWithCheck>>(
             VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObDoubleBatchMinusRawNoCheck : public ObArithOpRawType<double, double, double>
{
  static void raw_op(double &res, const double l, const double r)
  {
    res = l - r;
  }

  static int raw_check(const double , const double , const double)
  {
    return OB_SUCCESS;
  }
};

struct ObDoubleBatchMinusRawWithCheck: public ObDoubleBatchMinusRawNoCheck
{
  static int raw_check(const double res, const double l, const double r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMinus::is_double_out_of_range(res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%e - %e)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DOUBLE", expr_str);
      LOG_WARN("double out of range", K(l), K(r), K(res));
    }
    return ret;
  }
};

//calc type is doubleTC, left and right has same TC
int ObExprMinus::minus_double_double(EVAL_FUNC_ARG_DECL)
{
  return T_OP_AGG_MINUS == expr.type_
      ? def_arith_eval_func<ObArithOpWrap<ObDoubleBatchMinusRawNoCheck>>(EVAL_FUNC_ARG_LIST)
      : def_arith_eval_func<ObArithOpWrap<ObDoubleBatchMinusRawWithCheck>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_double_double_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return T_OP_AGG_MINUS == expr.type_
      ? def_batch_arith_op<ObArithOpWrap<ObDoubleBatchMinusRawNoCheck>>(BATCH_EVAL_FUNC_ARG_LIST)
      : def_batch_arith_op<ObArithOpWrap<ObDoubleBatchMinusRawWithCheck>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_double_double_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return T_OP_AGG_MINUS == expr.type_ ?
           def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDoubleBatchMinusRawNoCheck>>(
             VECTOR_EVAL_FUNC_ARG_LIST) :
           def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDoubleBatchMinusRawWithCheck>>(
             VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObNumberMinusFunc
{
  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r) const
  {
    int ret = OB_SUCCESS;
    char local_buff[ObNumber::MAX_BYTE_LEN];
    ObDataBuffer local_alloc(local_buff, ObNumber::MAX_BYTE_LEN);
    number::ObNumber l_num(l.get_number());
    number::ObNumber r_num(r.get_number());
    number::ObNumber res_num;
    if (OB_FAIL(l_num.sub_v3(r_num, res_num, local_alloc))) {
      LOG_WARN("minus num failed", K(ret), K(l_num), K(r_num));
    } else {
      res.set_number(res_num);
    }
    return ret;
  }
};

//calc type TC is ObNumberTC
int ObExprMinus::minus_number_number(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObNumberMinusFunc>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_number_number_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  LOG_DEBUG("minus_number_number_batch begin");
  int ret = OB_SUCCESS;
  ObDatumVector l_datums;
  ObDatumVector r_datums;
  const ObExpr &left = *expr.args_[0];
  const ObExpr &right = *expr.args_[1];

  if (OB_FAIL(binary_operand_batch_eval(expr, ctx, skip, size, false))) {
    LOG_WARN("number minus batch evaluation failure", K(ret));
  } else {
    l_datums = left.locate_expr_datumvector(ctx);
    r_datums = right.locate_expr_datumvector(ctx);
  }

  if (OB_SUCC(ret)) {
    char local_buff[ObNumber::MAX_BYTE_LEN];
    ObDataBuffer local_alloc(local_buff, ObNumber::MAX_BYTE_LEN);
    ObDatumVector results = expr.locate_expr_datumvector(ctx);
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);

    for (auto i = 0; OB_SUCC(ret) && i < size; i++) {
      if (eval_flags.at(i) || skip.at(i)) {
        continue;
      }
      if (l_datums.at(i)->is_null() || r_datums.at(i)->is_null()) {
        results.at(i)->set_null();
        eval_flags.set(i);
        continue;
      }
      ObNumber l_num(l_datums.at(i)->get_number());
      ObNumber r_num(r_datums.at(i)->get_number());
      uint32_t *res_digits = const_cast<uint32_t *> (results.at(i)->get_number_digits());
      ObNumber::Desc &desc_buf = const_cast<ObNumber::Desc &> (results.at(i)->get_number_desc());
      // Notice that, space of desc_buf is allocated in frame but without memset operation, which causes random memory content.
      // And the reserved in storage layer should be 0, thus you must replacement new here to avoid checksum error, etc.
      ObNumber::Desc *res_desc = new (&desc_buf) ObNumberDesc();
      // speedup detection
      if (ObNumber::try_fast_minus(l_num, r_num, res_digits, *res_desc)) {
        results.at(i)->set_pack(sizeof(number::ObCompactNumber) +
                                res_desc->len_ * sizeof(*res_digits));
        eval_flags.set(i);
        // LOG_INFO("mul speedup done", K(l_num.format()), K(r_num.format()));
      } else {
        // normal path: no speedup
        ObNumber res_num;
        if (OB_FAIL(l_num.sub_v3(r_num, res_num, local_alloc))) {
          LOG_WARN("mul num failed", K(ret), K(l_num), K(r_num));
        } else {
          results.at(i)->set_number(res_num);
          eval_flags.set(i);
        }
        local_alloc.free();
      }
    }
  }
  LOG_DEBUG("minus_number_number_batch done");
  return ret;
}

struct NmbTryFastMinusOp
{
  OB_INLINE bool operator()(ObNumber &l_num, ObNumber &r_num, uint32_t *res_digit,
                            ObNumberDesc &res_desc)
  {
    return ObNumber::try_fast_minus(l_num, r_num, res_digit, res_desc);
  }
  OB_INLINE int operator()(const ObNumber &left, const ObNumber &right, ObNumber &value,
                           ObIAllocator &allocator)
  {
    return ObNumber::sub_v3(left, right, value, allocator);
  }
};

int ObExprMinus::minus_number_number_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  NmbTryFastMinusOp op;
  return def_number_vector_arith_op(VECTOR_EVAL_FUNC_ARG_LIST, op);
}

struct ObDatetimeNumberMinusFunc
{
  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r) const
  {
    int ret = OB_SUCCESS;
    number::ObNumber right_nmb(r.get_number());
    const int64_t left_i = l.get_datetime();
    int64_t int_part = 0;
    int64_t dec_part = 0;
    if (!right_nmb.is_int_parts_valid_int64(int_part,dec_part)) {
      ret = OB_INVALID_DATE_FORMAT;
      LOG_WARN("invalid date format", K(ret), K(right_nmb));
    } else {
      const int64_t right_i = static_cast<int64_t>(int_part * USECS_PER_DAY)
          + (right_nmb.is_negative() ? -1  : 1 )
          * static_cast<int64_t>(static_cast<double>(dec_part)
                                 / NSECS_PER_SEC * static_cast<double>(USECS_PER_DAY));
      int64_t round_value = left_i - right_i;
      ObTimeConverter::round_datetime(OB_MAX_DATE_PRECISION, round_value);
      res.set_datetime(round_value);
      ObTime ob_time;
      if (OB_UNLIKELY(res.get_datetime() > DATETIME_MAX_VAL
                      || res.get_datetime() < DATETIME_MIN_VAL)
          || (OB_FAIL(ObTimeConverter::datetime_to_ob_time(res.get_datetime(), NULL, ob_time)))
          || (OB_FAIL(ObTimeConverter::validate_oracle_date(ob_time)))) {
        char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
        int64_t pos = 0;
        ret = OB_OPERATE_OVERFLOW;
        pos = 0;
        databuff_printf(expr_str,
                        OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                        pos,
                        "'(%ld - %ld)'", left_i, right_i);
        LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DATE", expr_str);
      }
    }
    return ret;
  }
};

int ObExprMinus::minus_datetime_number(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObDatetimeNumberMinusFunc>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_datetime_number_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op_by_datum_func<ObDatetimeNumberMinusFunc>(
      BATCH_EVAL_FUNC_ARG_LIST);
}

struct ObDatetimeDatetimeOralceMinusFunc
{
  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r) const
  {
    int ret = OB_SUCCESS;
    const int64_t LOCAL_BUF_SIZE = ObNumber::MAX_CALC_BYTE_LEN * 5;
    char local_buf[LOCAL_BUF_SIZE];
    ObDataBuffer local_alloc(local_buf, LOCAL_BUF_SIZE);
    ObNumber left_datetime;
    ObNumber right_datetime;
    ObNumber usecs_per_day;
    ObNumber sub_datetime;
    ObNumber sub_date;
    if (OB_FAIL(left_datetime.from(l.get_datetime(), local_alloc))) {
      LOG_WARN("convert int64 to number failed", K(ret), K(l.get_datetime()));
    } else if (OB_FAIL(right_datetime.from(r.get_datetime(), local_alloc))) {
      LOG_WARN("convert int64 to number failed", K(ret), K(r.get_datetime()));
    } else if (OB_FAIL(usecs_per_day.from(USECS_PER_DAY, local_alloc))) {
      LOG_WARN("convert int64 to number failed", K(ret));
    } else if (OB_FAIL(left_datetime.sub_v3(right_datetime, sub_datetime, local_alloc))) {
      LOG_WARN("sub failed", K(ret), K(left_datetime), K(right_datetime));
    } else if (OB_FAIL(sub_datetime.div_v3(usecs_per_day, sub_date, local_alloc))) {
      LOG_WARN("calc left date number failed", K(ret));
    } else {
      res.set_number(sub_date);
    }
    return ret;
  }
};

//left and right are both ObDateTimeTC. calc_type is ObNumberType. only oracle mode.
int ObExprMinus::minus_datetime_datetime_oracle(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObDatetimeDatetimeOralceMinusFunc>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_datetime_datetime_oracle_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op_by_datum_func<ObDatetimeDatetimeOralceMinusFunc>(
      BATCH_EVAL_FUNC_ARG_LIST);
}

struct ObDatetimeDatetimeMinusFunc
{
  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r) const
  {
    int ret = OB_SUCCESS;
    const int64_t left_i = l.get_datetime();
    const int64_t right_i = r.get_datetime();
    ObTime ob_time;
    int64_t round_value = left_i - right_i;
    ObTimeConverter::round_datetime(OB_MAX_DATE_PRECISION, round_value);
    res.set_datetime(round_value);
    if (OB_UNLIKELY(res.get_datetime() > DATETIME_MAX_VAL
        || res.get_datetime() < DATETIME_MIN_VAL)
        || (OB_FAIL(ObTimeConverter::datetime_to_ob_time(res.get_datetime(), NULL, ob_time)))
        || (OB_FAIL(ObTimeConverter::validate_oracle_date(ob_time)))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      int64_t pos = 0;
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld - %ld)'", left_i, right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DATE", expr_str);
    }
    return ret;
  }
};

//calc type is datetimeTC. cast left and right to calc_type.
int ObExprMinus::minus_datetime_datetime(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObDatetimeDatetimeMinusFunc>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_datetime_datetime_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op_by_datum_func<ObDatetimeDatetimeMinusFunc>(
      BATCH_EVAL_FUNC_ARG_LIST);
}

template<typename T>
struct ObDecimalIntBatchMinusRaw : public ObArithOpRawType<T, T, T>
{
  static void raw_op(T &res, const T &l, const T &r)
  {
    res = l - r;
  }

  static int raw_check(const T &res, const T &l, const T &r)
  {
    return OB_SUCCESS;
  }
};

struct ObDecimalIntBatchMinusRawWithCheck : public ObDecimalIntBatchMinusRaw<int512_t>
{
  static int raw_check(const int512_t &res, const int512_t &l, const int512_t &r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(res <= wide::ObDecimalIntConstValue::MYSQL_DEC_INT_MIN
                    || res >= wide::ObDecimalIntConstValue::MYSQL_DEC_INT_MAX)) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str, OB_MAX_TWO_OPERATOR_EXPR_LENGTH, pos, "");
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DECIMAL", expr_str);
      LOG_WARN("decimal int out of range", K(ret));
    }
    return ret;
  }
};

#define DECINC_MINUS_EVAL_FUNC_DECL(TYPE) \
int ObExprMinus::minus_decimal##TYPE(EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_arith_eval_func<ObArithOpWrap<ObDecimalIntBatchMinusRaw<TYPE##_t>>>(EVAL_FUNC_ARG_LIST); \
}                                            \
int ObExprMinus::minus_decimal##TYPE##_batch(BATCH_EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_batch_arith_op<ObArithOpWrap<ObDecimalIntBatchMinusRaw<TYPE##_t>>>(BATCH_EVAL_FUNC_ARG_LIST); \
}                                            \
int ObExprMinus::minus_decimal##TYPE##_vector(VECTOR_EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDecimalIntBatchMinusRaw<TYPE##_t>>>(VECTOR_EVAL_FUNC_ARG_LIST); \
}


DECINC_MINUS_EVAL_FUNC_DECL(int32)
DECINC_MINUS_EVAL_FUNC_DECL(int64)
DECINC_MINUS_EVAL_FUNC_DECL(int128)
DECINC_MINUS_EVAL_FUNC_DECL(int256)
DECINC_MINUS_EVAL_FUNC_DECL(int512)

int ObExprMinus::minus_decimalint512_with_check(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObDecimalIntBatchMinusRawWithCheck>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_decimalint512_with_check_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObDecimalIntBatchMinusRawWithCheck>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMinus::minus_decimalint512_with_check_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDecimalIntBatchMinusRawWithCheck>>(
    VECTOR_EVAL_FUNC_ARG_LIST);
}

#undef DECINC_MINUS_EVAL_FUNC_DECL

template<typename T>
struct ObDecimalOracleMinusFunc
{
  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r, const int64_t scale,
                 ObNumStackOnceAlloc &alloc) const
  {
    int ret = OB_SUCCESS;
    const T res_int = *reinterpret_cast<const T *>(l.ptr_) - *reinterpret_cast<const T *>(r.ptr_);
    number::ObNumber res_num;
    if (OB_FAIL(wide::to_number(res_int, scale, alloc, res_num))) {
      LOG_WARN("fail to cast decima int to number", K(ret), K(scale));
    } else {
      res.set_number(res_num);
      alloc.free();  // for batch function reuse alloc
    }
    return ret;
  }
};

template<typename T>
struct ObDecimalOracleVectorMinusFunc
{
  template <typename ResVector, typename LeftVector, typename RightVector>
  int operator()(ResVector &res_vec, const LeftVector &l_vec, const RightVector &r_vec,
                 const int64_t idx, const int64_t scale, ObNumStackOnceAlloc &alloc) const
  {
    int ret = OB_SUCCESS;
    const T res_int = *reinterpret_cast<const T *>(l_vec.get_payload(idx))
                      - *reinterpret_cast<const T *>(r_vec.get_payload(idx));
    number::ObNumber res_num;
    if (OB_FAIL(wide::to_number(res_int, scale, alloc, res_num))) {
      LOG_WARN("fail to cast decima int to number", K(ret), K(scale));
    } else {
      res_vec.set_number(idx, res_num);
      alloc.free();  // for batch function reuse alloc
    }
    return ret;
  }
};

#define DECINC_MINUS_EVAL_FUNC_ORA_DECL(TYPE) \
int ObExprMinus::minus_decimal##TYPE##_oracle(EVAL_FUNC_ARG_DECL)      \
{                                            \
  ObNumStackOnceAlloc tmp_alloc;                                \
  const int64_t scale = expr.args_[0]->datum_meta_.scale_;      \
  return def_arith_eval_func<ObDecimalOracleMinusFunc<TYPE##_t>>(EVAL_FUNC_ARG_LIST, scale, tmp_alloc); \
}                                            \
int ObExprMinus::minus_decimal##TYPE##_oracle_batch(BATCH_EVAL_FUNC_ARG_DECL)      \
{                                            \
  ObNumStackOnceAlloc tmp_alloc;                                \
  const int64_t scale = expr.args_[0]->datum_meta_.scale_;      \
  return def_batch_arith_op_by_datum_func<ObDecimalOracleMinusFunc<TYPE##_t>>(BATCH_EVAL_FUNC_ARG_LIST, scale, tmp_alloc); \
}                                            \
int ObExprMinus::minus_decimal##TYPE##_oracle_vector(VECTOR_EVAL_FUNC_ARG_DECL)      \
{                                            \
  ObNumStackOnceAlloc tmp_alloc;                                \
  const int64_t scale = expr.args_[0]->datum_meta_.scale_;      \
  return def_fixed_len_vector_arith_op_func<ObDecimalOracleVectorMinusFunc<TYPE##_t>,\
                                            ObArithTypedBase<TYPE##_t, TYPE##_t, TYPE##_t>>(VECTOR_EVAL_FUNC_ARG_LIST, scale, tmp_alloc); \
}

DECINC_MINUS_EVAL_FUNC_ORA_DECL(int32)
DECINC_MINUS_EVAL_FUNC_ORA_DECL(int64)
DECINC_MINUS_EVAL_FUNC_ORA_DECL(int128)


#undef DECINC_MINUS_EVAL_FUNC_ORA_DECL

template<typename T>
struct ObArrayMinusFunc : public ObNestedArithOpBaseFunc
{
  int operator()(ObIArrayType &res, const ObIArrayType &l, const ObIArrayType &r) const
  {
    int ret = OB_SUCCESS;

    if (l.get_format() != r.get_format() || res.get_format() != r.get_format()) {
      ret = OB_ERR_ARRAY_TYPE_MISMATCH;
      LOG_WARN("nested type is mismatch", K(ret), K(l.get_format()), K(r.get_format()), K(res.get_format()));
    } else if (l.size() != r.size()) {
      ret = OB_ERR_ARRAY_TYPE_MISMATCH;
      LOG_WARN("nested size is mismatch", K(ret), K(l.size()), K(r.size()));
    } else if (l.get_format() != ArrayFormat::Vector && MEMCMP(l.get_nullbitmap(), r.get_nullbitmap(), sizeof(uint8_t) * l.size())) {
      ret = OB_ERR_ARRAY_TYPE_MISMATCH;
      LOG_WARN("nested nullbitmap is mismatch", K(ret));
    } else if (l.get_format() == ArrayFormat::Nested_Array) {
      // compare array dimension
      const ObArrayNested &left = static_cast<const ObArrayNested&>(l);
      const ObArrayNested &right = static_cast<const ObArrayNested&>(r);
      ObArrayNested &nest_res = static_cast<ObArrayNested&>(res);
      if (MEMCMP(left.get_nullbitmap(), right.get_nullbitmap(), sizeof(uint8_t) * left.size()) != 0) {
        ret = OB_ERR_ARRAY_TYPE_MISMATCH;
        LOG_WARN("nested nullbitmap is mismatch", K(ret));
      } else if (MEMCMP(left.get_offsets(), right.get_offsets(), sizeof(uint32_t) * left.size()) != 0) {
        ret = OB_ERR_ARRAY_TYPE_MISMATCH;
        LOG_WARN("nested offsets is mismatch", K(ret));
      } else if (OB_FAIL(res.set_null_bitmaps(left.get_nullbitmap(), left.size()))) {
        LOG_WARN("nested nullbitmap copy failed", K(ret));
      } else if (OB_FAIL(res.set_offsets(left.get_offsets(), left.size()))) {
        LOG_WARN("nested offset copy failed", K(ret));
      } else if (OB_FAIL(operator()(*nest_res.get_child_array(), *left.get_child_array(), *right.get_child_array()))) {
        LOG_WARN("nested child array add failed", K(ret));
      }
    } else if (l.get_format() != ArrayFormat::Fixed_Size && l.get_format() != ArrayFormat::Vector) {
      ret = OB_ERR_ARRAY_TYPE_MISMATCH;
      LOG_WARN("invaid array type", K(ret), K(l.get_format()));
    } else {
      T *res_data = NULL;
      if (OB_FAIL(l.get_format() != ArrayFormat::Vector && res.set_null_bitmaps(l.get_nullbitmap(), l.size()))) {
        LOG_WARN("array nullbitmap copy failed", K(ret));
      } else if (OB_FAIL(static_cast<ObArrayBase<T> &>(res).get_reserved_data(l.size(), res_data))) {
        LOG_WARN("array get resered data failed", K(ret));
      } else {
        T *left_data = reinterpret_cast<T *>(l.get_data());
        T *right_data = reinterpret_cast<T *>(r.get_data());
        for (int64_t i = 0; i < l.size() && OB_SUCC(ret); ++i) {
          res_data[i] = left_data[i] - right_data[i];
          if (OB_FAIL(ObArrayExprUtils::raw_check_minus(res_data[i], left_data[i], right_data[i]))) {
            LOG_WARN("array minus check failed", K(ret));
          }
        }
      }
    }
    return ret;
  }
};

#define COLLECTION_MINUS_EVAL_FUNC_DECL(TYPE) \
int ObExprMinus::minus_collection_collection_##TYPE(EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_arith_eval_func<ObNestedArithOpWrap<ObArrayMinusFunc<TYPE>>>(EVAL_FUNC_ARG_LIST, expr, ctx); \
}                                            \
int ObExprMinus::minus_collection_collection_##TYPE##_batch(BATCH_EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_batch_arith_op_by_datum_func<ObNestedArithOpWrap<ObArrayMinusFunc<TYPE>>>(BATCH_EVAL_FUNC_ARG_LIST, expr, ctx); \
}                                             \
int ObExprMinus::minus_collection_collection_##TYPE##_vector(VECTOR_EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_nested_vector_arith_op_func<ObNestedVectorArithOpFunc<ObArrayMinusFunc<TYPE>>>(VECTOR_EVAL_FUNC_ARG_LIST, expr, ctx);  \
}

COLLECTION_MINUS_EVAL_FUNC_DECL(int8_t)
COLLECTION_MINUS_EVAL_FUNC_DECL(int16_t)
COLLECTION_MINUS_EVAL_FUNC_DECL(int32_t)
COLLECTION_MINUS_EVAL_FUNC_DECL(int64_t)
COLLECTION_MINUS_EVAL_FUNC_DECL(float)
COLLECTION_MINUS_EVAL_FUNC_DECL(double)
COLLECTION_MINUS_EVAL_FUNC_DECL(uint64_t)

}
}
