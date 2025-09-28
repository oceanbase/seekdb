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

#include "sql/engine/expr/ob_expr_mul.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"
#include "sql/engine/expr/ob_batch_eval_util.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
using namespace common;
using namespace common::number;

ObExprMul::ObExprMul(ObIAllocator &alloc, ObExprOperatorType type)
  : ObArithExprOperator(alloc,
                        type,
                        N_MUL,
                        2,
                        NOT_ROW_DIMENSION,
                        ObExprResultTypeUtil::get_mul_result_type,
                        ObExprResultTypeUtil::get_mul_calc_type,
                        mul_funcs_)
{
  param_lazy_eval_ = false;
}

int ObExprMul::calc_result_type2(ObExprResType &type,
                                 ObExprResType &type1,
                                 ObExprResType &type2,
                                 ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  const bool is_all_decint_args =
    ob_is_decimal_int(type1.get_type()) && ob_is_decimal_int(type2.get_type());
  if (OB_FAIL(ObArithExprOperator::calc_result_type2(type, type1, type2, type_ctx))) {
  } else if (type.is_collection_sql_type()) {
    // only support vector/array/varchar * vector/array/varchar now // array and varchar need cast to array(float)
    uint16_t res_subschema_id = UINT16_MAX;
    if (OB_FAIL(ObArrayExprUtils::calc_cast_type2(type_, type1, type2, type_ctx, res_subschema_id))) {
      LOG_WARN("failed to calc cast type", K(ret), K(type1));
    } else if (UINT16_MAX == res_subschema_id) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected result subschema_id", K(ret));
    } else {
      type.set_collection(res_subschema_id);
    }
  } else if (type.is_decimal_int() && (type1.is_null() || type2.is_null())) {
    type.set_precision(MAX(type1.get_precision(), type2.get_precision()));
    type.set_scale(MAX(type1.get_scale(), type2.get_scale()));
  } else {
    ObScale scale1 = static_cast<ObScale>(MAX(type1.get_scale(), 0));
    ObScale scale2 = static_cast<ObScale>(MAX(type2.get_scale(), 0));
    if (SCALE_UNKNOWN_YET == type1.get_scale() || SCALE_UNKNOWN_YET == type2.get_scale()) {
      type.set_scale(SCALE_UNKNOWN_YET);
    } else {
      if (lib::is_mysql_mode() && type.is_double()) {
        type.set_scale(MAX(scale1, scale2));
      } else {
        type.set_scale(MIN(static_cast<ObScale>(scale1 + scale2), OB_MAX_DECIMAL_SCALE));
      }
    }
    ObPrecision precision1 = static_cast<ObPrecision>(MAX(type1.get_precision(), 0));
    ObPrecision precision2 = static_cast<ObPrecision>(MAX(type2.get_precision(), 0));
    if (OB_UNLIKELY(PRECISION_UNKNOWN_YET == type1.get_precision()) ||
        OB_UNLIKELY(PRECISION_UNKNOWN_YET == type2.get_precision())) {
      type.set_precision(PRECISION_UNKNOWN_YET);
    } else {
      // estimated precision
      if (lib::is_mysql_mode() && type.is_double()) {
        type.set_precision(ObMySQLUtil::float_length(type.get_scale()));
      } else if (type.has_result_flag(DECIMAL_INT_ADJUST_FLAG)) {
        ObPrecision precision = MAX(precision1, precision2);
        type.set_precision(precision);
      } else {
        ObPrecision precision = static_cast<ObPrecision>(precision1 + precision2);
        type.set_precision(precision);
      }

      if (is_all_decint_args || type.is_decimal_int()) {
        if (OB_UNLIKELY(PRECISION_UNKNOWN_YET == type.get_precision() ||
                        SCALE_UNKNOWN_YET == type.get_scale())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected decimal int precision and scale", K(ret), K(type));
        } else if ((type1.get_scale() + type2.get_scale() <= OB_MAX_DECIMAL_SCALE)
              && (type1.get_precision() + type2.get_precision() <= MAX_PRECISION_DECIMAL_INT_256)) {
          // use specialized functions without additional casts
          type1.set_calc_accuracy(type1.get_accuracy());
          type2.set_calc_accuracy(type2.get_accuracy());
        } else {
          ObAccuracy l_dst_acc(type.get_precision(), type1.get_scale());
          if (ObRawExprUtils::decimal_int_need_cast(type1.get_accuracy(), l_dst_acc)) {
            type.set_result_flag(DECIMAL_INT_ADJUST_FLAG);
            type1.set_calc_accuracy(l_dst_acc);
          }
          ObAccuracy r_dst_acc(type.get_precision(), type2.get_scale());
          if (ObRawExprUtils::decimal_int_need_cast(type2.get_accuracy(), r_dst_acc)) {
            type.set_result_flag(DECIMAL_INT_ADJUST_FLAG);
            type2.set_calc_accuracy(r_dst_acc);
          }
        }
        LOG_DEBUG("calc_result_type2", K(type.get_accuracy()), K(type1.get_accuracy()),
                                       K(type2.get_accuracy()));
      }
    }
    LOG_DEBUG("calc_result_type2 muluply", K(type.get_accuracy()), K(type1.get_accuracy()),
                K(type2.get_accuracy()), K(type1.get_calc_accuracy()),
                K(type2.get_calc_accuracy()));
  }
  return ret;
}



ObArithFunc ObExprMul::mul_funcs_[ObMaxTC] =
{
  NULL,
  ObExprMul::mul_int,
  ObExprMul::mul_uint,
  NULL,
  ObExprMul::mul_double,
  ObExprMul::mul_number,
  NULL,//datetime
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
  NULL,//otimestamp
  NULL,//raw
  NULL,//interval
};

ObArithFunc ObExprMul::agg_mul_funcs_[ObMaxTC] =
{
  NULL,
  ObExprMul::mul_int,
  ObExprMul::mul_uint,
  NULL,
  ObExprMul::mul_double_no_overflow,
  ObExprMul::mul_number,
  NULL,//datetime
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
  NULL,//otimestamp
  NULL,//raw
  NULL,//interval
};

int ObExprMul::mul_int(ObObj &res,
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
    if (OB_UNLIKELY(is_multi_overflow64(left_i, right_i))) {
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld * %ld)'",
                      left_i,
                      right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT", expr_str);
    }
    res.set_int(left_i * right_i);
  } else if (OB_UNLIKELY(ObUIntTC != right.get_type_class())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid types", K(ret), K(left), K(right));
  } else {
    if (OB_UNLIKELY(is_int_uint_mul_out_of_range(left_i, right_i))) {
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld * %lu)'",
                      left_i,
                      right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
    res.set_uint64(left_i * right_i);
  }
  UNUSED(allocator);
  UNUSED(scale);
  return ret;
}

int ObExprMul::mul_uint(ObObj &res,
                        const ObObj &left,
                        const ObObj &right,
                        ObIAllocator *allocator,
                        ObScale scale)
{
  int ret = OB_SUCCESS;
  uint64_t left_i = left.get_uint64();
  uint64_t right_i = right.get_uint64();
  char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
  int64_t pos = 0;
  if (left.get_type_class() == right.get_type_class()) {
    if (OB_UNLIKELY(is_uint_uint_mul_out_of_range(left_i, right_i))) {
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%lu * %lu)'",
                      left_i,
                      right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);

    }
    res.set_uint64(left_i * right_i);
  } else if (OB_UNLIKELY(ObIntTC != right.get_type_class())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid types", K(ret), K(left), K(right));
  } else {
    if (OB_UNLIKELY(is_int_uint_mul_out_of_range(right_i, left_i))) {
      ret = OB_OPERATE_OVERFLOW;
      pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%lu * %ld)'",
                      left_i,
                      right_i);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
    res.set_uint64(left_i * right_i);
  }
  UNUSED(allocator);
  UNUSED(scale);
  return ret;
}

int ObExprMul::mul_double(ObObj &res,
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
    res.set_double(left_d * right_d);
    if (OB_UNLIKELY(is_double_out_of_range(res.get_double()))) {
      ret = OB_OPERATE_OVERFLOW;
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%e * %e)'",
                      left_d,
                      right_d);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DOUBLE", expr_str);
      LOG_WARN("double out of range", K(ret), K(left), K(right), K(res));
      res.set_null();
    }
    LOG_DEBUG("succ to mul double", K(left), K(right));
  }
  UNUSED(allocator);
  UNUSED(scale);
  return ret;
}

int ObExprMul::mul_double_no_overflow(ObObj &res,
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
    res.set_double(left_d * right_d);
    LOG_DEBUG("succ to mul double", K(left), K(right));
  }
  return ret;
}

int ObExprMul::mul_number(ObObj &res,
                          const ObObj &left,
                          const ObObj &right,
                          ObIAllocator *allocator,
                          ObScale scale)
{
  int ret = OB_SUCCESS;
  number::ObNumber res_nmb;
  if (OB_UNLIKELY(NULL == allocator)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("allocator is null", K(ret));
  } else if (OB_FAIL(left.get_number().mul_v3(right.get_number(), res_nmb, *allocator))) {
    LOG_WARN("failed to mul numbers", K(ret), K(left), K(right));
  } else {
    ObObjType res_type = res.get_type();
    if (ObUNumberType == res_type) {
      res.set_unumber(res_nmb);
    } else {
      res.set_number(res_nmb);
    }
  }
  UNUSED(scale);
  return ret;
}

struct ObIntIntBatchMulRaw : public ObArithOpRawType<int64_t, int64_t, int64_t>
{
  static void raw_op(int64_t &res, const int64_t l, const int64_t r)
  {
    res = l * r;
  }

  static int raw_check(const int64_t, const int64_t l, const int64_t r)
  {
    int ret = OB_SUCCESS;
    long long res;
    if (OB_UNLIKELY(ObExprMul::is_mul_out_of_range(l, r, res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld * %ld)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT", expr_str);
    }
    return ret;
  }
};

int ObExprMul::mul_int_int(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObIntIntBatchMulRaw>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_int_int_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObIntIntBatchMulRaw>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_int_int_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObIntIntBatchMulRaw>>(VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObIntUIntBatchMulRaw : public ObArithOpRawType<uint64_t, int64_t, uint64_t>
{
  static void raw_op(uint64_t &res, const int64_t l, const uint64_t r)
  {
    res = l * r;
  }

  static int raw_check(const uint64_t, const int64_t l, const uint64_t r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMul::is_int_uint_mul_out_of_range(l, r))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%ld * %lu)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
    return ret;
  }
};

int ObExprMul::mul_int_uint(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObIntUIntBatchMulRaw>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_int_uint_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObIntUIntBatchMulRaw>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_int_uint_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObIntUIntBatchMulRaw>>(VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObUIntIntBatchMulRaw : public ObArithOpRawType<uint64_t, uint64_t, int64_t>
{
  static void raw_op(uint64_t &res, const uint64_t l, const int64_t r)
  {
    res = l * r;
  }

  static int raw_check(const uint64_t, const uint64_t l, const int64_t r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMul::is_int_uint_mul_out_of_range(r, l))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%lu * %ld)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
    return ret;
  }
};

int ObExprMul::mul_uint_int(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObUIntIntBatchMulRaw>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_uint_int_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObUIntIntBatchMulRaw>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_uint_int_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObUIntIntBatchMulRaw>>(VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObUIntUIntBatchMulRaw : public ObArithOpRawType<uint64_t, uint64_t, uint64_t>
{
  static void raw_op(uint64_t &res, const uint64_t l, const uint64_t r)
  {
    res = l * r;
  }

  static int raw_check(const uint64_t, const uint64_t l, const uint64_t r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMul::is_uint_uint_mul_out_of_range(l, r))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%lu * %lu)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BIGINT UNSIGNED", expr_str);
    }
    return ret;
  }
};

int ObExprMul::mul_uint_uint(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObUIntUIntBatchMulRaw>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_uint_uint_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObUIntUIntBatchMulRaw>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_uint_uint_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObUIntUIntBatchMulRaw>>(VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObFloatBatchMulRawNoCheck : public ObArithOpRawType<float, float, float>
{
  static void raw_op(float &res, const float l, const float r)
  {
    res = l * r;
  }

};

struct ObFloatBatchMulRawWithCheck: public ObFloatBatchMulRawNoCheck
{
  static int raw_check(const float res, const float l, const float r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMul::is_float_out_of_range(res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%e * %e)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "FLOAT", expr_str);
      LOG_WARN("float out of range", K(l), K(r), K(res));
    }
    return ret;
  }
};

int ObExprMul::mul_float(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObFloatBatchMulRawWithCheck>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_float_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObFloatBatchMulRawWithCheck>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_float_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObFloatBatchMulRawWithCheck>>(
             VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObDoubleBatchMulRawNoCheck : public ObArithOpRawType<double, double, double>
{
  static void raw_op(double &res, const double l, const double r)
  {
    res = l * r;
  }

  static int raw_check(const double , const double , const double)
  {
    return OB_SUCCESS;
  }
};

struct ObDoubleBatchMulRawWithCheck: public ObDoubleBatchMulRawNoCheck
{
  static int raw_check(const double res, const double l, const double r)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(ObExprMul::is_double_out_of_range(res))) {
      char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
      ret = OB_OPERATE_OVERFLOW;
      int64_t pos = 0;
      databuff_printf(expr_str,
                      OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                      pos,
                      "'(%e * %e)'", l, r);
      LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DOUBLE", expr_str);
      LOG_WARN("double out of range", K(l), K(r), K(res));
    }
    return ret;
  }
};

int ObExprMul::mul_double(EVAL_FUNC_ARG_DECL)
{
  return T_OP_AGG_MUL == expr.type_
      ? def_arith_eval_func<ObArithOpWrap<ObDoubleBatchMulRawNoCheck>>(EVAL_FUNC_ARG_LIST)
      : def_arith_eval_func<ObArithOpWrap<ObDoubleBatchMulRawWithCheck>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_double_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return T_OP_AGG_MUL == expr.type_
      ? def_batch_arith_op<ObArithOpWrap<ObDoubleBatchMulRawNoCheck>>(BATCH_EVAL_FUNC_ARG_LIST)
      : def_batch_arith_op<ObArithOpWrap<ObDoubleBatchMulRawWithCheck>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_double_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return T_OP_AGG_MUL == expr.type_ ?
           def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDoubleBatchMulRawNoCheck>>(
             VECTOR_EVAL_FUNC_ARG_LIST) :
           def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDoubleBatchMulRawWithCheck>>(
             VECTOR_EVAL_FUNC_ARG_LIST);
}

struct ObNumberMulFunc
{
  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r) const
  {
    int ret = OB_SUCCESS;
    char local_buff[ObNumber::MAX_BYTE_LEN];
    ObDataBuffer local_alloc(local_buff, ObNumber::MAX_BYTE_LEN);
    number::ObNumber l_num(l.get_number());
    number::ObNumber r_num(r.get_number());
    number::ObNumber res_num;
    if (OB_FAIL(l_num.mul_v3(r_num, res_num, local_alloc))) {
      LOG_WARN("mul num failed", K(ret), K(l_num), K(r_num));
    } else {
      res.set_number(res_num);
    }
    return ret;
  }
};

int ObExprMul::mul_number(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObNumberMulFunc>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_number_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  LOG_DEBUG("mul_number_batch begin");
  int ret = OB_SUCCESS;
  ObDatumVector l_datums;
  ObDatumVector r_datums;
  const ObExpr &left = *expr.args_[0];
  const ObExpr &right = *expr.args_[1];

  if (OB_FAIL(binary_operand_batch_eval(expr, ctx, skip, size, false))) {
    LOG_WARN("number multiply batch evaluation failure", K(ret));
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
      ObNumber res_num;
      ObNumber l_num(l_datums.at(i)->get_number());
      ObNumber r_num(r_datums.at(i)->get_number());
      uint32_t *res_digits = const_cast<uint32_t *> (results.at(i)->get_number_digits());
      ObNumber::Desc &desc_buf = const_cast<ObNumber::Desc &> (results.at(i)->get_number_desc());
      // Notice that, space of desc_buf is allocated in frame but without memset operation, which causes random memory content.
      // And the reserved in storage layer should be 0, thus you must replacement new here to avoid checksum error, etc.
      ObNumber::Desc *res_desc = new (&desc_buf) ObNumber::Desc();
      // speedup detection
      if (ObNumber::try_fast_mul(l_num, r_num, res_digits, *res_desc)) {
        results.at(i)->set_pack(sizeof(number::ObCompactNumber) +
                                res_desc->len_ * sizeof(*res_digits));
        eval_flags.set(i);
        // LOG_DEBUG("mul speedup", K(l_num.format()),
        // K(r_num.format()), K(res_num.format()));
      } else {
        // normal path: no speedup
        if (OB_FAIL(l_num.mul_v3(r_num, res_num, local_alloc))) {
          LOG_WARN("mul num failed", K(ret), K(l_num), K(r_num));
        } else {
          results.at(i)->set_number(res_num);
          eval_flags.set(i);
        }
        local_alloc.free();
      }
    }
  }
  LOG_DEBUG("mul_number_batch done");
  return ret;

}

struct NmbTryFastMultiplyOp
{
  OB_INLINE bool operator()(ObNumber &l_num, ObNumber &r_num, uint32_t *res_digit,
                            ObNumberDesc &res_desc)
  {
    return ObNumber::try_fast_mul(l_num, r_num, res_digit, res_desc);
  }

  OB_INLINE int operator()(const ObNumber &left, const ObNumber &right, ObNumber &value,
                           ObIAllocator &allocator)
  {
    return ObNumber::mul_v3(left, right, value, allocator);
  }
};

int ObExprMul::mul_number_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  NmbTryFastMultiplyOp op;
  return def_number_vector_arith_op(VECTOR_EVAL_FUNC_ARG_LIST, op);
}

template<typename Res, typename Left, typename Righ>
struct ObDecimalIntBatchMulRaw : public ObArithOpRawType<Res, Left, Righ>
{
  static void raw_op(Res &res, const Left &l, const Righ &r)
  {
    res = l;
    res = res * r;
  }

  static int raw_check(const Res &res, const Left &l, const Righ &r)
  {
    return OB_SUCCESS;
  }
};

template<typename Res, typename Left, typename Righ>
struct ObDecimalIntBatchMulResTypeNotLagerRaw : public ObArithOpRawType<Res, Left, Righ>
{
  static void raw_op(Res &res, const Left &l, const Righ &r)
  {
    res = l * r;
  }

  static int raw_check(const Res &res, const Left &l, const Righ &r)
  {
    return OB_SUCCESS;
  }
};

template<typename T>
struct ObDecimalIntBatchMulRawWithRound : public ObDecimalIntBatchMulResTypeNotLagerRaw<T, T, T>
{
  static void raw_op(T &res, const T &l, const T &r, const T &sf)
  {
    res = l * r;
    bool is_neg = res < 0 ? true : false;
    if (is_neg) {
      res = -res;
    }
    const T mod = res % sf;
    res = res / sf;
    if (mod >= (sf >> 1)) {
      res = res + 1;
    }
    if (is_neg) {
      res = -res;
    }
  }
};

struct ObDecimalIntBatchMulRawWithCheck
  : public ObDecimalIntBatchMulResTypeNotLagerRaw<int512_t, int512_t, int512_t>
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

struct ObDecimalIntBatchMulRawWithRoundCheck : public ObDecimalIntBatchMulRawWithRound<int512_t>
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

struct ObDecint128TO256: public ObArithOpRawType<int256_t, int128_t, int128_t>
{
  inline static void raw_op(int256_t &res, const int128_t &l, const int128_t &r)
  {
    uint64_t tmp_res[sizeof(int128_t)] = {0};
    if (OB_OPERATE_OVERFLOW == l.multiply<wide::CheckOverFlow>(r, *reinterpret_cast<int128_t *>(tmp_res))) {
      res = l;
      res = res * r;
    } else {
      res = *reinterpret_cast<int128_t *>(tmp_res);
    }
  }

  inline static int raw_check(const int256_t &res, const int128_t &l, const int128_t &r)
  {
    return OB_SUCCESS;
  }
};

#define DECINC_MUL_EVAL_FUNC_DECL(RES, L, R) \
int ObExprMul::mul_decimal##RES##_##L##_##R(EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_arith_eval_func<ObArithOpWrap<ObDecimalIntBatchMulRaw<RES##_t, L##_t, R##_t>>>(EVAL_FUNC_ARG_LIST); \
}                                            \
int ObExprMul::mul_decimal##RES##_##L##_##R##_batch(BATCH_EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_batch_arith_op<ObArithOpWrap<ObDecimalIntBatchMulRaw<RES##_t, L##_t, R##_t>>>(BATCH_EVAL_FUNC_ARG_LIST); \
}                                            \
int ObExprMul::mul_decimal##RES##_##L##_##R##_vector(VECTOR_EVAL_FUNC_ARG_DECL)      \
{                                            \
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDecimalIntBatchMulRaw<RES##_t, L##_t, R##_t>>>(VECTOR_EVAL_FUNC_ARG_LIST); \
}

// use bignum overflow checking instead promoting calculation type
int ObExprMul::mul_decimalint256_int128_int128(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObDecimalIntBatchMulRaw<int256_t, int128_t, int128_t>>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_decimalint256_int128_int128_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObDecimalIntBatchMulRaw<int256_t, int128_t, int128_t>>>(BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_decimalint256_int128_int128_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDecint128TO256>>(VECTOR_EVAL_FUNC_ARG_LIST);
}

DECINC_MUL_EVAL_FUNC_DECL(int64, int32, int32)
DECINC_MUL_EVAL_FUNC_DECL(int128, int32, int64)
DECINC_MUL_EVAL_FUNC_DECL(int128, int64, int32)
DECINC_MUL_EVAL_FUNC_DECL(int128, int64, int64)
DECINC_MUL_EVAL_FUNC_DECL(int256, int32, int128)
DECINC_MUL_EVAL_FUNC_DECL(int256, int128, int32)
DECINC_MUL_EVAL_FUNC_DECL(int256, int64, int128)
DECINC_MUL_EVAL_FUNC_DECL(int256, int128, int64)
// DECINC_MUL_EVAL_FUNC_DECL(int256, int128, int128)


#undef DECINC_MUL_EVAL_FUNC_DECL

#define DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(RES, L, R)                                    \
  int ObExprMul::mul_decimal##RES##_##L##_##R(EVAL_FUNC_ARG_DECL)                                  \
  {                                                                                                \
    return def_arith_eval_func<                                                                    \
      ObArithOpWrap<ObDecimalIntBatchMulResTypeNotLagerRaw<RES##_t, L##_t, R##_t>>>(               \
      EVAL_FUNC_ARG_LIST);                                                                         \
  }                                                                                                \
  int ObExprMul::mul_decimal##RES##_##L##_##R##_batch(BATCH_EVAL_FUNC_ARG_DECL)                    \
  {                                                                                                \
    return def_batch_arith_op<                                                                     \
      ObArithOpWrap<ObDecimalIntBatchMulResTypeNotLagerRaw<RES##_t, L##_t, R##_t>>>(               \
      BATCH_EVAL_FUNC_ARG_LIST);                                                                   \
  }                                                                                                \
  int ObExprMul::mul_decimal##RES##_##L##_##R##_vector(VECTOR_EVAL_FUNC_ARG_DECL)                  \
  {                                                                                                \
    return def_fixed_len_vector_arith_op<                                                          \
      ObVectorArithOpWrap<ObDecimalIntBatchMulResTypeNotLagerRaw<RES##_t, L##_t, R##_t>>>(         \
      VECTOR_EVAL_FUNC_ARG_LIST);                                                                  \
  }
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int32, int32, int32)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int64, int32, int64)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int64, int64, int32)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int128, int32, int128)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int128, int128, int32)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int128, int64, int128)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int128, int128, int64)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int128, int128, int128)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int256, int32, int256)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int256, int256, int32)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int256, int256, int64)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int256, int64, int256)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int256, int128, int256)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int256, int256, int128)
DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL(int512, int512, int512)
#undef DECINC_RES_TYPE_NOT_LAGER_MUL_EVAL_FUNC_DECL

#define DECINC_MUL_ROUND_EVAL_FUNC_DECL(TYPE) \
int ObExprMul::mul_decimal##TYPE##_round(EVAL_FUNC_ARG_DECL)      \
{                                            \
  const int16_t scale = (expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_) \
                          - OB_MAX_DECIMAL_SCALE; \
  const TYPE##_t sf = get_scale_factor<TYPE##_t>(scale);                          \
  return def_arith_eval_func<ObArithOpWrap<ObDecimalIntBatchMulRawWithRound<TYPE##_t>>>(EVAL_FUNC_ARG_LIST, sf); \
}                                            \
int ObExprMul::mul_decimal##TYPE##_round_batch(BATCH_EVAL_FUNC_ARG_DECL)      \
{                                            \
  const int16_t scale = (expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_) \
                          - OB_MAX_DECIMAL_SCALE; \
  const TYPE##_t sf = get_scale_factor<TYPE##_t>(scale);                         \
  return def_batch_arith_op<ObArithOpWrap<ObDecimalIntBatchMulRawWithRound<TYPE##_t>>>(BATCH_EVAL_FUNC_ARG_LIST, sf); \
}                                            \
int ObExprMul::mul_decimal##TYPE##_round_vector(VECTOR_EVAL_FUNC_ARG_DECL)      \
{                                            \
  const int16_t scale = (expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_) \
                          - OB_MAX_DECIMAL_SCALE; \
  const TYPE##_t sf = get_scale_factor<TYPE##_t>(scale);                         \
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDecimalIntBatchMulRawWithRound<TYPE##_t>>>(VECTOR_EVAL_FUNC_ARG_LIST, sf); \
}

DECINC_MUL_ROUND_EVAL_FUNC_DECL(int64)
DECINC_MUL_ROUND_EVAL_FUNC_DECL(int128)
DECINC_MUL_ROUND_EVAL_FUNC_DECL(int256)
DECINC_MUL_ROUND_EVAL_FUNC_DECL(int512)

#undef DECINC_MUL_ROUND_EVAL_FUNC_DECL

int ObExprMul::mul_decimalint512_with_check(EVAL_FUNC_ARG_DECL)
{
  return def_arith_eval_func<ObArithOpWrap<ObDecimalIntBatchMulRawWithCheck>>(EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_decimalint512_with_check_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  return def_batch_arith_op<ObArithOpWrap<ObDecimalIntBatchMulRawWithCheck>>(
    BATCH_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_decimalint512_with_check_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDecimalIntBatchMulRawWithCheck>>(VECTOR_EVAL_FUNC_ARG_LIST);
}

int ObExprMul::mul_decimalint512_round_with_check(EVAL_FUNC_ARG_DECL)
{
  const int16_t scale = (expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_)
                           - OB_MAX_DECIMAL_SCALE;
  const int512_t sf = get_scale_factor<int512_t>(scale);
  return def_arith_eval_func<ObArithOpWrap<ObDecimalIntBatchMulRawWithRoundCheck>>(EVAL_FUNC_ARG_LIST, sf);
}

int ObExprMul::mul_decimalint512_round_with_check_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  const int16_t scale = (expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_)
                           - OB_MAX_DECIMAL_SCALE;
  const int512_t sf = get_scale_factor<int512_t>(scale);
  return def_batch_arith_op<ObArithOpWrap<ObDecimalIntBatchMulRawWithRoundCheck>>(BATCH_EVAL_FUNC_ARG_LIST, sf);
}

int ObExprMul::mul_decimalint512_round_with_check_vector(VECTOR_EVAL_FUNC_ARG_DECL)
{
  const int16_t scale = (expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_)
                           - OB_MAX_DECIMAL_SCALE;
  const int512_t sf = get_scale_factor<int512_t>(scale);
  return def_fixed_len_vector_arith_op<ObVectorArithOpWrap<ObDecimalIntBatchMulRawWithRoundCheck>>(
    VECTOR_EVAL_FUNC_ARG_LIST, sf);
}

template<typename Res, typename Left, typename Righ>
struct ObDecimalOracleMulFunc
{
  int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r, const int64_t scale,
                 ObNumStackOnceAlloc &alloc) const
  {
    int ret = OB_SUCCESS;
    Res res_int = *reinterpret_cast<const Left *>(l.ptr_);
    res_int = res_int * (*reinterpret_cast<const Righ *>(r.ptr_));
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

template<typename Res, typename Left, typename Righ>
struct ObDecimalOracleVectorMulFunc
{
  template <typename ResVector, typename LeftVector, typename RightVector>
  int operator()(ResVector &res_vec, const LeftVector &l_vec, const RightVector &r_vec,
                 const int64_t idx, const int64_t scale, ObNumStackOnceAlloc &alloc) const
  {
    int ret = OB_SUCCESS;
    Res res_int = *reinterpret_cast<const Left *>(l_vec.get_payload(idx));
    res_int = res_int * (*reinterpret_cast<const Righ *>(r_vec.get_payload(idx)));
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

#define DECINC_MUL_EVAL_FUNC_ORA_DECL(RES, L, R) \
int ObExprMul::mul_decimal##RES##_##L##_##R##_oracle(EVAL_FUNC_ARG_DECL)      \
{                                            \
  ObNumStackOnceAlloc tmp_alloc;                                \
  const int64_t scale = expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_;      \
  return def_arith_eval_func<ObDecimalOracleMulFunc<RES##_t, L##_t, R##_t>>(EVAL_FUNC_ARG_LIST, scale, tmp_alloc); \
}                                            \
int ObExprMul::mul_decimal##RES##_##L##_##R##_oracle_batch(BATCH_EVAL_FUNC_ARG_DECL)      \
{                                            \
  ObNumStackOnceAlloc tmp_alloc;                                \
  const int64_t scale = expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_;      \
  return def_batch_arith_op_by_datum_func<ObDecimalOracleMulFunc<RES##_t, L##_t, R##_t>>(BATCH_EVAL_FUNC_ARG_LIST, scale, tmp_alloc); \
}                                            \
int ObExprMul::mul_decimal##RES##_##L##_##R##_oracle_vector(VECTOR_EVAL_FUNC_ARG_DECL)      \
{                                            \
  ObNumStackOnceAlloc tmp_alloc;                                \
  const int64_t scale = expr.args_[0]->datum_meta_.scale_ + expr.args_[1]->datum_meta_.scale_;      \
  return def_fixed_len_vector_arith_op_func<ObDecimalOracleVectorMulFunc<RES##_t, L##_t, R##_t>,\
                                            ObArithTypedBase<L##_t, R##_t, RES##_t>>(VECTOR_EVAL_FUNC_ARG_LIST, scale, tmp_alloc); \
}

DECINC_MUL_EVAL_FUNC_ORA_DECL(int32, int32, int32)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int64, int32, int32)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int64, int32, int64)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int64, int64, int32)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int128, int32, int64)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int128, int64, int32)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int128, int32, int128)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int128, int128, int32)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int128, int64, int64)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int128, int64, int128)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int128, int128, int64)
DECINC_MUL_EVAL_FUNC_ORA_DECL(int128, int128, int128)

#undef DECINC_MUL_EVAL_FUNC_ORA_DECL

#define SET_MUL_FUNC_PTR(v) \
  rt_expr.eval_func_ = ObExprMul::v; \
  rt_expr.eval_batch_func_ = ObExprMul::v##_batch;

int ObExprMul::set_decimal_int_eval_func(ObExpr &rt_expr, const bool is_oracle)
{
  int ret = OB_SUCCESS;
#define DECINT_FUNC_VAL(res, l, r) (res << 6) | (l << 3) | r
#define DECINT_SWITCH_CASE_ORA(res, l, r) \
  case DECINT_FUNC_VAL(DECIMAL_INT_##res, DECIMAL_INT_##l, DECIMAL_INT_##r): \
    SET_MUL_FUNC_PTR(mul_decimalint##res##_int##l##_int##r##_oracle); \
    rt_expr.eval_vector_func_ = mul_decimalint##res##_int##l##_int##r##_oracle_vector; \
    break;
#define DECINT_SWITCH_CASE(res, l, r) \
  case DECINT_FUNC_VAL(DECIMAL_INT_##res, DECIMAL_INT_##l, DECIMAL_INT_##r): \
    SET_MUL_FUNC_PTR(mul_decimalint##res##_int##l##_int##r); \
    rt_expr.eval_vector_func_ = mul_decimalint##res##_int##l##_int##r##_vector; \
    break;

  const int16_t lp = rt_expr.args_[0]->datum_meta_.precision_;
  const int16_t rp = rt_expr.args_[1]->datum_meta_.precision_;
  const int16_t ls = rt_expr.args_[0]->datum_meta_.scale_;
  const int16_t rs = rt_expr.args_[1]->datum_meta_.scale_;
  const int16_t res_p = lp + rp;
  const int16_t res_s = ls + rs;
  const int16_t l_type = get_decimalint_type(lp);
  const int16_t r_type = get_decimalint_type(rp);
  const int16_t res_type = get_decimalint_type(res_p);
  if (is_oracle) { // oracle
    switch (DECINT_FUNC_VAL(res_type, l_type, r_type)) {
      DECINT_SWITCH_CASE_ORA(32, 32, 32)
      DECINT_SWITCH_CASE_ORA(64, 32, 32)
      DECINT_SWITCH_CASE_ORA(64, 32, 64)
      DECINT_SWITCH_CASE_ORA(64, 64, 32)
      DECINT_SWITCH_CASE_ORA(128, 32, 64)
      DECINT_SWITCH_CASE_ORA(128, 64, 32)
      DECINT_SWITCH_CASE_ORA(128, 32, 128)
      DECINT_SWITCH_CASE_ORA(128, 128, 32)
      DECINT_SWITCH_CASE_ORA(128, 64, 64)
      DECINT_SWITCH_CASE_ORA(128, 64, 128)
      DECINT_SWITCH_CASE_ORA(128, 128, 64)
      DECINT_SWITCH_CASE_ORA(128, 128, 128)
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected precision in oracle", K(ret), K(lp), K(ls), K(rp), K(rs));
        break;
    }
  } else if (res_s <= OB_MAX_DECIMAL_SCALE && res_p <= MAX_PRECISION_DECIMAL_INT_256) { // mysql
    switch (DECINT_FUNC_VAL(res_type, l_type, r_type)) {
      DECINT_SWITCH_CASE(32, 32, 32)
      DECINT_SWITCH_CASE(64, 32, 32)
      DECINT_SWITCH_CASE(64, 32, 64)
      DECINT_SWITCH_CASE(64, 64, 32)
      DECINT_SWITCH_CASE(128, 32, 64)
      DECINT_SWITCH_CASE(128, 64, 32)
      DECINT_SWITCH_CASE(128, 32, 128)
      DECINT_SWITCH_CASE(128, 128, 32)
      DECINT_SWITCH_CASE(128, 64, 64)
      DECINT_SWITCH_CASE(128, 64, 128)
      DECINT_SWITCH_CASE(128, 128, 64)
      DECINT_SWITCH_CASE(128, 128, 128)
      DECINT_SWITCH_CASE(256, 32, 128)
      DECINT_SWITCH_CASE(256, 128, 32)
      DECINT_SWITCH_CASE(256, 32, 256)
      DECINT_SWITCH_CASE(256, 256, 32)
      DECINT_SWITCH_CASE(256, 64, 128)
      DECINT_SWITCH_CASE(256, 128, 64)
      DECINT_SWITCH_CASE(256, 64, 256)
      DECINT_SWITCH_CASE(256, 256, 64)
      DECINT_SWITCH_CASE(256, 128, 128)
      DECINT_SWITCH_CASE(256, 128, 256)
      DECINT_SWITCH_CASE(256, 256, 128)
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected precision in mysql", K(ret), K(lp), K(ls), K(rp), K(rs));
        break;
    }
  } else { // mysql with round or overflow check
    switch (get_decimalint_type(rt_expr.datum_meta_.precision_)) {
      case DECIMAL_INT_64:
        SET_MUL_FUNC_PTR(mul_decimalint64_round);
        rt_expr.eval_vector_func_ = mul_decimalint64_round_vector;
        break;
      case DECIMAL_INT_128:
        SET_MUL_FUNC_PTR(mul_decimalint128_round);
        rt_expr.eval_vector_func_ = mul_decimalint128_round_vector;
        break;
      case DECIMAL_INT_256:
        SET_MUL_FUNC_PTR(mul_decimalint256_round);
        rt_expr.eval_vector_func_ = mul_decimalint256_round_vector;
        break;
      case DECIMAL_INT_512:
        if (rt_expr.datum_meta_.precision_ < OB_MAX_DECIMAL_POSSIBLE_PRECISION) {
          if (res_s <= OB_MAX_DECIMAL_SCALE) {
            SET_MUL_FUNC_PTR(mul_decimalint512_int512_int512);
            rt_expr.eval_vector_func_ = mul_decimalint512_int512_int512_vector;
          } else {
            SET_MUL_FUNC_PTR(mul_decimalint512_round);
            rt_expr.eval_vector_func_ = mul_decimalint512_round_vector;
          }
        } else {
          if (res_s <= OB_MAX_DECIMAL_SCALE) {
            SET_MUL_FUNC_PTR(mul_decimalint512_with_check);
            rt_expr.eval_vector_func_ = mul_decimalint512_with_check_vector;
          } else {
            SET_MUL_FUNC_PTR(mul_decimalint512_round_with_check);
            rt_expr.eval_vector_func_ = mul_decimalint512_round_with_check_vector;
          }
        }
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected precision", K(ret), K(rt_expr.datum_meta_));
        break;
    }
  }

#undef DECINT_SWITCH_CASE
#undef DECINT_SWITCH_CASE_ORA
#undef DECINT_FUNC_VAL
  return ret;
}

int ObExprMul::cg_expr(ObExprCGCtx &op_cg_ctx,
                       const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  OB_ASSERT(2 == rt_expr.arg_cnt_);
  OB_ASSERT(NULL != rt_expr.args_);
  OB_ASSERT(NULL != rt_expr.args_[0]);
  OB_ASSERT(NULL != rt_expr.args_[1]);
  const common::ObObjType left = rt_expr.args_[0]->datum_meta_.type_;
  const common::ObObjType right = rt_expr.args_[1]->datum_meta_.type_;
  const ObObjTypeClass left_tc = ob_obj_type_class(left);
  const ObObjTypeClass right_tc = ob_obj_type_class(right);

  rt_expr.inner_functions_ = NULL;
  rt_expr.may_not_need_raw_check_ = false;
  LOG_DEBUG("arrive here cg_expr", K(ret), K(rt_expr));
  switch (rt_expr.datum_meta_.type_) {
    case ObIntType: {
      SET_MUL_FUNC_PTR(mul_int_int);
      rt_expr.eval_vector_func_ = mul_int_int_vector;
      break;
    }
    case ObUInt64Type: {
      if (ObIntTC == left_tc) {
        if (ObUIntTC == right_tc) {
          SET_MUL_FUNC_PTR(mul_int_uint);
          rt_expr.eval_vector_func_ = mul_int_uint_vector;
        }
      } else if (ObUIntTC == left_tc) {
        if (ObIntTC == right_tc) {
          SET_MUL_FUNC_PTR(mul_uint_int);
          rt_expr.eval_vector_func_ = mul_uint_int_vector;
        } else if (ObUIntTC == right_tc) {
          SET_MUL_FUNC_PTR(mul_uint_uint);
          rt_expr.eval_vector_func_ = mul_uint_uint_vector;
        }
      }
      break;
    }
    case ObFloatType: {
      SET_MUL_FUNC_PTR(mul_float);
      rt_expr.eval_vector_func_ = mul_float_vector;
      break;
    }
    case ObDoubleType: {
      SET_MUL_FUNC_PTR(mul_double);
      rt_expr.eval_vector_func_ = mul_double_vector;
      break;
    }
    case ObUNumberType:
    case ObNumberType: {
      if (ob_is_decimal_int(left) && ob_is_decimal_int(right)) {
        set_decimal_int_eval_func(rt_expr, true /*is_oracle*/);
      } else {
        SET_MUL_FUNC_PTR(mul_number);
        rt_expr.eval_vector_func_ = mul_number_vector;
      }
      break;
    }
    case ObDecimalIntType: {
      set_decimal_int_eval_func(rt_expr, false /*is_oracle*/);
      break;
    }
    case ObCollectionSQLType: {
      SET_MUL_FUNC_PTR(mul_vec_vec);
      break;
    }
    default: {
      break;
    }
  }

  if (OB_ISNULL(rt_expr.eval_func_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected result type", K(ret), K(rt_expr.datum_meta_.type_), K(left), K(right));
  }
  return ret;
}
#undef SET_MUL_FUNC_PTR

int ObExprMul::mul_vec_vec(EVAL_FUNC_ARG_DECL)
{
  ObVectorArithFunc::ArithType op_type = ObVectorArithFunc::ArithType::MUL;
  return def_arith_eval_func<ObVectorVectorArithFunc>(EVAL_FUNC_ARG_LIST, expr, ctx, op_type);
}

int ObExprMul::mul_vec_vec_batch(BATCH_EVAL_FUNC_ARG_DECL)
{
  ObVectorArithFunc::ArithType op_type = ObVectorArithFunc::ArithType::MUL;
  return def_batch_arith_op_by_datum_func<ObVectorVectorArithFunc>(BATCH_EVAL_FUNC_ARG_LIST, expr, ctx, op_type);
}

}
}
