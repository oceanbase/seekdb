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

#include "sql/engine/expr/ob_expr_oracle_to_char.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_number_format_models.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

const int64_t MAX_NUMBER_BUFFER_SIZE = 40;
const int64_t MAX_DATETIME_BUFFER_SIZE = 100;
const int64_t MAX_TO_CHAR_BUFFER_SIZE = 256;
const int64_t MAX_INTERVAL_BUFFER_SIZE = 32;
const int64_t MAX_CLOB_BUFFER_SIZE = 4000;

#define DISPATCH_VECTOR_ARG_FORMAT(func_name)                         \
  switch (left_format) {                                                       \
  case VEC_FIXED: {                                                            \
    ret = func_name<ObFixedLengthBase, ObVectorBase>(VECTOR_EVAL_FUNC_ARG_LIST);    \
    break;                                                                     \
  }                                                                            \
  case VEC_DISCRETE: {                                                         \
    ret = func_name<ObDiscreteFormat, ObVectorBase>(VECTOR_EVAL_FUNC_ARG_LIST);     \
    break;                                                                     \
  }                                                                            \
  case VEC_CONTINUOUS: {                                                       \
    ret = func_name<ObContinuousFormat, ObVectorBase>(VECTOR_EVAL_FUNC_ARG_LIST);   \
    break;                                                                     \
  }                                                                            \
  case VEC_UNIFORM: {                                                          \
    ret =                                                                      \
        func_name<ObUniformFormat<false>, ObVectorBase>(VECTOR_EVAL_FUNC_ARG_LIST); \
    break;                                                                     \
  }                                                                            \
  case VEC_UNIFORM_CONST: {                                                    \
    ret =                                                                      \
        func_name<ObUniformFormat<true>, ObVectorBase>(VECTOR_EVAL_FUNC_ARG_LIST);  \
    break;                                                                     \
  }                                                                            \
  default: {                                                                   \
    ret = func_name<ObVectorBase, ObVectorBase>(VECTOR_EVAL_FUNC_ARG_LIST);         \
  }                                                                            \
  }

ObExprToChar::ObExprToChar(ObIAllocator &alloc)
    : ObExprToCharCommon(alloc, T_FUN_SYS_TO_CHAR, N_TO_CHAR, MORE_THAN_ZERO, VALID_FOR_GENERATED_COL)
{
}

ObExprToChar::~ObExprToChar()
{
}


int ObExprToChar::calc_result_typeN(ObExprResType &type,
                                          ObExprResType *type_array,
                                          int64_t params_count,
                                          ObExprTypeCtx &type_ctx) const
{
  //https://docs.oracle.com/cd/B19306_01/server.102/b14200/functions180.htm
  int ret = OB_SUCCESS;
  CK(NULL != type_ctx.get_session());
  ObSessionNLSParams nls_param = type_ctx.get_session()->get_session_nls_params();
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(NULL == type_array || params_count < 1 || params_count > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument.", K(ret), K(type_array), K(params_count));
  } else if (OB_ISNULL(type_ctx.get_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else {
    const ObObjTypeClass type_class = type_array[0].get_type_class();
    switch (type_class) {
      case ObNullTC: {
        type.set_null();
        break;
      }
      case ObFloatTC:
      case ObDoubleTC:
      case ObStringTC:
      case ObTextTC:
      case ObIntTC:
      case ObUIntTC:
      case ObNumberTC:
      case ObDecimalIntTC: {
        if (1 == params_count) {
          if (type_array[0].is_varchar_or_char()) {
            type.set_type_simple(type_array[0].get_type());
            type.set_length_semantics(type_array[0].get_length_semantics());
          } else {
            type.set_varchar();
            type.set_length_semantics(nls_param.nls_length_semantics_);
          }
          type.set_collation_level(CS_LEVEL_IMPLICIT);
          type.set_collation_type(nls_param.nls_collation_);
          if (ob_is_numeric_tc(type_class)) {
            type.set_length(MAX_NUMBER_BUFFER_SIZE);
          } else if (ObStringTC == type_class) {
            type.set_length(type_array[0].get_accuracy().get_length());
          } else if (ObTextTC == type_class) {
            type.set_length(MAX_CLOB_BUFFER_SIZE);
          } else {
            type.set_length(MAX_TO_CHAR_BUFFER_SIZE);
          }
        } else {
          const ObObj &fmt_obj = type_array[1].get_param();
          ObNFMToChar nfm;
          int32_t length = 0;
          type.set_varchar();
          type.set_collation_level(CS_LEVEL_IMPLICIT);
          type.set_collation_type(nls_param.nls_collation_);
          type.set_length_semantics(nls_param.nls_length_semantics_);
          if (fmt_obj.is_null()) {
            length = OB_MAX_MYSQL_VARCHAR_LENGTH;
          } else if (OB_FAIL(nfm.calc_result_length(fmt_obj, length))) {
            // invalid format won't cause failure because the expr may not be executed
            LOG_WARN("calc reuslt length failed", K(fmt_obj), K(ret));
            ret = OB_SUCCESS;
            if (type_array[0].is_character_type() && 0 == type_array[0].get_length()) {
              length = 0;
            } else {
              length = OB_MAX_MYSQL_VARCHAR_LENGTH;
            }
          }
          OX (type.set_length(length));
        }
        break;
      }
      case ObDateTC:
      case ObMySQLDateTC:
      case ObTimeTC:
      case ObYearTC:
      case ObDateTimeTC:
      case ObMySQLDateTimeTC:
      case ObOTimestampTC:
      case ObIntervalTC: {
        type.set_varchar();
        type.set_length_semantics(nls_param.nls_length_semantics_);
        type.set_collation_level(CS_LEVEL_IMPLICIT);
        type.set_default_collation_type();
        type.set_length(MAX_TO_CHAR_BUFFER_SIZE);
        break;
      }
      default: {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        LOG_WARN("input type not supported", K(ret), K(type_class));
        break;
      }
    }
  }
  if (type_array[0].is_string_type()) {
    type_array[0].set_calc_meta(type);
    type_array[0].set_calc_length(type.get_length());
    type_array[0].set_calc_length_semantics(type.get_length_semantics());
  }
  if (OB_SUCC(ret) && params_count >= 2) {
    const ObObjTypeClass tc = type_array[0].get_type_class();
    if (ObStringTC == tc || ObTextTC == tc || ObIntTC == tc || ObUIntTC == tc || ObDecimalIntTC == tc) {
      type_array[0].set_calc_type(ObNumberType);
      type_array[0].set_calc_scale(NUMBER_SCALE_UNKNOWN_YET);
    }
    if (type_array[0].is_oracle_temporal_type()) {
      type_array[1].set_calc_type(ObVarcharType);
      type_array[1].set_calc_collation_type(nls_param.nls_collation_);
    } else if (!type_array[1].is_varchar_or_char()) {
      type_array[1].set_calc_type_default_varchar();
    }
  }
  if (OB_SUCC(ret) && params_count == 3) {
    type_array[2].set_calc_type_default_varchar();
  }
  return ret;
}
// Used for to_char, to_nchar, translate using expression parameter length derivation when parameter is string/raw tc
// Before calling, need to set the result type, collation_type, and length_semantics

// '-0' => '-', '0' => ''


/**
 * @brief ObExprToChar::format_number
 * Format number_str according to format_str and output to result_buf
 */

int ObExprToCharCommon::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                const ObRawExpr &raw_expr,
                                ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (!(rt_expr.arg_cnt_ >= 1 && rt_expr.arg_cnt_ <= 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("mysql to_char only support 1 or 2 args", K(ret));
  } else {
    rt_expr.eval_func_ = &ObExprToCharCommon::eval_to_char;
    if (1 == rt_expr.arg_cnt_ || 
        ( 2 == rt_expr.arg_cnt_ && rt_expr.args_[1]->is_const_expr())) {
      rt_expr.eval_vector_func_ = &ObExprToCharCommon::eval_to_char_vector;
    }
  }
  return ret;
}

int ObExprToCharCommon::eval_to_char(const ObExpr &expr,
                                            ObEvalCtx &ctx,
                                            ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  const ObObjTypeClass input_tc = ob_obj_type_class(expr.args_[0]->datum_meta_.type_);
  ObDatum *input = NULL;
  ObDatum *fmt_datum = NULL;
  ObDatum *nlsparam_datum = NULL;
  ObString fmt;
  ObString nlsparam;
  if (OB_FAIL(expr.eval_param_value(ctx, input, fmt_datum, nlsparam_datum))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (input->is_null()
             || (NULL != fmt_datum && fmt_datum->is_null())
             || (NULL != nlsparam_datum && nlsparam_datum->is_null())) {
    expr_datum.set_null();
  } else if (1 == expr.arg_cnt_ && (ObStringTC == input_tc || ObTextTC == input_tc)) {
    expr_datum.set_datum(*input);
  } else {
    ObEvalCtx::TempAllocGuard alloc_guard(ctx);
    ObIAllocator &alloc = alloc_guard.get_allocator();
    // convert the fmt && nlsparam to utf8 first.
    if (NULL != fmt_datum) {
      fmt = fmt_datum->get_string();
    }

    ObString res;
    if (OB_SUCC(ret)) {
      switch (input_tc) {
        case ObDateTC:
        case ObMySQLDateTC:
        case ObTimeTC:
        case ObYearTC:
        case ObDateTimeTC:
        case ObMySQLDateTimeTC:
        case ObOTimestampTC: {
          OZ(datetime_to_char(expr, ctx, alloc, *input, fmt, nlsparam, res));
          break;
        }
        case ObIntTC: // to support PLS_INTERGER type
        case ObFloatTC:
        case ObDoubleTC:
        case ObNumberTC:
        case ObDecimalIntTC: {
          if (OB_FAIL(is_valid_to_char_number(expr))) {
            LOG_WARN("fail to check num format", K(ret));
          } else if (OB_FAIL(number_to_char(expr, ctx, alloc, *input, fmt, nlsparam, res))) {
            LOG_WARN("number to char failed", K(ret));
          }
          break;
        }
        default: {
          ret = OB_ERR_INVALID_TYPE_FOR_OP;
          LOG_WARN("unsupported to_char", K(ret), K(input_tc));
        }
      }
    }

    if (OB_SUCC(ret)) {
      const bool is_ascii = (ObDateTimeTC == input_tc || ObOTimestampTC == input_tc) ? false : true;
      ObCollationType src_coll_type = (ObDateTimeTC == input_tc || ObOTimestampTC == input_tc)
                                            ? ctx.exec_ctx_.get_my_session()->get_nls_collation()
                                            : CS_TYPE_UTF8MB4_BIN;
      if (is_mysql_mode()) {
        src_coll_type = (ObDateTimeTC == input_tc) ? ObCharset::get_default_collation(
                                           ObCharset::get_default_charset())
                                     : CS_TYPE_UTF8MB4_BIN;
      }
      if (OB_FAIL(ObExprUtil::set_expr_ascii_result(expr, ctx, expr_datum, res, is_ascii,
                                                    src_coll_type))) {
        LOG_WARN("set expr ascii result failed", K(ret));
      }
    }
  }
  return ret;
}

// for static engine batch
int ObExprToCharCommon::eval_oracle_to_char_batch(
    const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size)
{
  LOG_DEBUG("eval to_char in batch mode", K(batch_size));
  int ret = OB_SUCCESS;
  ObDatum *results = expr.locate_batch_datums(ctx);

  if (OB_ISNULL(results)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr results frame is not init", K(ret));
  } else {
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    ObDatum *fmt_datum = NULL;
    ObDatum *nlsparam_datum = NULL;
    bool is_result_all_null = false;
    // if the second arg or the third arg is null, all the results are null
    if (2 == expr.arg_cnt_ || 3 == expr.arg_cnt_) {
      if (OB_FAIL(expr.args_[1]->eval(ctx, fmt_datum))) {
        LOG_WARN("eval fmt_datum failed", K(ret));
      } else if (fmt_datum->is_null()) {
        is_result_all_null = true;
      } else if (3 == expr.arg_cnt_) {
        if (OB_FAIL(expr.args_[2]->eval(ctx, nlsparam_datum))) {
          LOG_WARN("eval nlsparam_datum failed", K(ret));
        } else if (nlsparam_datum->is_null()) {
          is_result_all_null = true;
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (is_result_all_null) {
      for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
        if (skip.at(j) || eval_flags.at(j)) {
          continue;
        } else {
          results[j].set_null();
          eval_flags.set(j);
        }
      }
    } else if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
      LOG_WARN("failed to eval batch result args0", K(ret));
    } else {
      ObDatum *datum_array = expr.args_[0]->locate_batch_datums(ctx);
      ObString fmt;
      ObString nlsparam;
      ObEvalCtx::TempAllocGuard alloc_guard(ctx);
      ObIAllocator &alloc = alloc_guard.get_allocator();
      // convert the nlsparam to utf8 first.
      if (NULL != fmt_datum) {
        fmt = fmt_datum->get_string();
      }
      if (OB_SUCC(ret) && NULL != nlsparam_datum) {
        OZ(ObExprUtil::convert_string_collation(nlsparam_datum->get_string(),
                                                expr.args_[2]->datum_meta_.cs_type_,
                                                nlsparam, CS_TYPE_UTF8MB4_BIN, alloc));
      }
      if (OB_SUCC(ret)) {
        const ObObjTypeClass input_tc = ob_obj_type_class(expr.args_[0]->datum_meta_.type_);
        if (1 == expr.arg_cnt_ && (ObStringTC == input_tc || ObTextTC == input_tc)) {
          for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
            if (skip.at(j) || eval_flags.at(j)) {
              continue;
            } else if (datum_array[j].is_null()) {
              results[j].set_null();
              eval_flags.set(j);
            } else {
              results[j].set_datum(datum_array[j]);
              eval_flags.set(j);
            }
          }
        } else {
          ObString res;
          for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
            if (skip.at(j) || eval_flags.at(j)) {
              continue;
            } else if (datum_array[j].is_null()) {
              results[j].set_null();
              eval_flags.set(j);
            } else {
              switch (input_tc) {
                case ObDateTimeTC:
                case ObOTimestampTC: {
                  OZ(datetime_to_char(expr, ctx, alloc, datum_array[j], fmt, nlsparam, res));
                  break;
                }
                case ObIntTC: // to support PLS_INTERGER type
                case ObFloatTC:
                case ObDoubleTC:
                case ObNumberTC:
                case ObDecimalIntTC: {
                  if (NULL != nlsparam_datum) {
                    nlsparam = nlsparam_datum->get_string();
                  }
                  if (OB_FAIL(is_valid_to_char_number(expr))) {
                    LOG_WARN("fail to check num format", K(ret));
                  } else if (OB_FAIL(number_to_char(
                                     expr, ctx, alloc, datum_array[j], fmt, nlsparam, res))) {
                    // need to avoid calling ObNFMBase::parse_fmt in number_to_char more than once
                    LOG_WARN("number to char failed", K(ret));
                  }
                  break;
                }
                default: {
                  ret = OB_ERR_INVALID_TYPE_FOR_OP;
                  LOG_WARN("unsupported to_char", K(ret), K(input_tc));
                }
              }
              if (OB_SUCC(ret)) {
                const bool is_ascii = (ObDateTimeTC == input_tc || ObOTimestampTC == input_tc) ? false : true;
                ObCollationType src_coll_type = (ObDateTimeTC == input_tc || ObOTimestampTC == input_tc)
                                                      ? ctx.exec_ctx_.get_my_session()->get_nls_collation()
                                                      : CS_TYPE_UTF8MB4_BIN;
                if (OB_FAIL(ObExprUtil::set_expr_ascii_result(expr, ctx, results[j], res, j,
                                                              is_ascii, src_coll_type))) {
                  LOG_WARN("set expr ascii result failed", K(ret));
                } else {
                  eval_flags.set(j);
                }
              }
            }
          }
        }
      }
    }
  }

  return ret;
}

int ObExprToCharCommon::eval_to_char_vector(VECTOR_EVAL_FUNC_ARG_DECL) {
  LOG_DEBUG("eval to_char in vector mode", K(bound.range_size()));
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.args_[0]->eval_vector(ctx, skip, bound))) {
    LOG_WARN("failed to eval vector param values", K(ret));
  } else {
    VectorFormat res_format = expr.get_format(ctx);
    VectorFormat left_format = expr.args_[0]->get_format(ctx);
    DISPATCH_VECTOR_ARG_FORMAT(inner_eval_to_char_vector);
  }
  return ret;
}

template <typename LeftVec, typename ResVec>
int ObExprToCharCommon::inner_eval_to_char_vector(VECTOR_EVAL_FUNC_ARG_DECL) {
  int ret = OB_SUCCESS;
  ResVec *res_vec = static_cast<ResVec *>(expr.get_vector(ctx));
  LeftVec *input_vec = static_cast<LeftVec *>(expr.args_[0]->get_vector(ctx));
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  bool is_result_all_null = false;

  ObString fmt_args;
  ObString nlsparam;
  if (2 == expr.arg_cnt_ || 3 == expr.arg_cnt_) {
    int64_t const_skip = 0;
    const ObBitVector *param_skip = to_bit_vector(&const_skip);
    if (OB_FAIL(expr.args_[1]->eval_vector(ctx, *param_skip, EvalBound(1)))) {
      LOG_WARN("eval fmt failed", K(ret));
    } else if (expr.args_[1]->get_vector(ctx)->is_null(0)) {
      is_result_all_null = true;
    } else {
      fmt_args = expr.args_[1]->get_vector(ctx)->get_string(0);
      if (3 == expr.arg_cnt_) {
        if (OB_FAIL(expr.args_[2]->eval_vector(ctx, *param_skip, EvalBound(1)))) {
          LOG_WARN("eval nlsparam failed", K(ret));
        } else if (expr.args_[2]->get_vector(ctx)->is_null(0)) {
          is_result_all_null = true;
        } else {
          nlsparam = expr.args_[2]->get_vector(ctx)->get_string(0);
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_result_all_null) {
    for (int64_t idx = bound.start(); idx < bound.end(); ++idx) {
      if (skip.at(idx) || eval_flags.at(idx)) {
        continue;
      } else {
        res_vec->set_null(idx);
        eval_flags.set(idx);
      }
    }
  } else {
    ObString fmt;
    fmt = fmt_args;
    ObEvalCtx::TempAllocGuard alloc_guard(ctx);
    ObIAllocator &alloc = alloc_guard.get_allocator();
    const ObObjTypeClass input_tc =
        ob_obj_type_class(expr.args_[0]->datum_meta_.type_);
    if (1 == expr.arg_cnt_ &&
        (ObStringTC == input_tc || ObTextTC == input_tc)) {
      for (int64_t idx = bound.start(); idx < bound.end(); ++idx) {
        if (skip.at(idx) || eval_flags.at(idx)) {
          continue;
        }
        if (input_vec->is_null(idx)) {
          res_vec->set_null(idx);
        } else {
          res_vec->set_string(idx, input_vec->get_string(idx));
        }
        eval_flags.set(idx);
      }
    } else {
      ObString res;
      const char *in_ptr = NULL;
      ObLength in_len = 0;
      bool is_null;
      for (int idx = bound.start(); OB_SUCC(ret) && idx < bound.end(); ++idx) {
        if (skip.at(idx) || eval_flags.at(idx)) {
          continue;
        } else {
          input_vec->get_payload(idx, is_null, in_ptr, in_len);
          if (is_null) {
            res_vec->set_null(idx);
            eval_flags.set(idx);
            continue;
          }
          switch (input_tc) {
          case ObDateTC:
          case ObMySQLDateTC:
          case ObTimeTC:
          case ObYearTC:
          case ObDateTimeTC:
          case ObMySQLDateTimeTC:
          case ObOTimestampTC: {
            OZ(datetime_to_char(expr, ctx, alloc, in_ptr, in_len, fmt, nlsparam, res));
            break;
          }
          case ObIntTC: // to support PLS_INTERGER type
          case ObUIntTC:
          case ObFloatTC:
          case ObDoubleTC:
          case ObNumberTC:
          case ObDecimalIntTC: {
            if (OB_FAIL(is_valid_to_char_number(expr))) {
              LOG_WARN("fail to check num format", K(ret));
            } else if (OB_FAIL(number_to_char(expr, ctx, alloc, in_ptr,
                                              in_len, fmt, nlsparam, res))) {
              // need to avoid calling ObNFMBase::parse_fmt in number_to_char
              // more than once
              LOG_WARN("number to char failed", K(ret));
            }
            break;
          }
          default: {
            ret = OB_ERR_INVALID_TYPE_FOR_OP;
            LOG_WARN("unsupported to_char", K(ret), K(input_tc));
          }
          }

          if (OB_SUCC(ret)) {
            ObTextStringDatumResult output_result(expr.datum_meta_.type_,
                                                  &expr, &ctx, res_vec, idx);
            const bool is_ascii = (ObDateTimeTC == input_tc || ObOTimestampTC == input_tc) ? false : true;
            ObCollationType src_coll_type = (ObDateTimeTC == input_tc || ObOTimestampTC == input_tc)
                                                  ? ctx.exec_ctx_.get_my_session()->get_nls_collation()
                                                  : CS_TYPE_UTF8MB4_BIN;
            if (is_mysql_mode()) {
              src_coll_type = (ObDateTimeTC == input_tc) ? ObCharset::get_default_collation(
                                                 ObCharset::get_default_charset())
                                           : CS_TYPE_UTF8MB4_BIN;
            }
            if (OB_FAIL(ObExprUtil::set_expr_asscii_result(expr, ctx, output_result,
                                                 res, idx, is_ascii,
                                                 src_coll_type))) {
              LOG_WARN("set expr ascii result failed", K(ret));
            } else {
              eval_flags.set(idx);
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObExprToCharCommon::is_valid_to_char_number(const ObExpr &expr)
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 2)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument.", K(ret), K(expr.arg_cnt_));
  } else if (expr.args_[0]->obj_meta_.is_numeric_type()) {
    is_valid = true;
  } else if (ObStringTC == expr.args_[0]->obj_meta_.get_type_class()
             && expr.arg_cnt_ >= 2) {
    is_valid = (ObNullTC == expr.args_[1]->obj_meta_.get_type_class()
        || ObStringTC == expr.args_[1]->obj_meta_.get_type_class());
  }
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(!is_valid)) {
      ret = OB_ERR_INVALID_NUMBER_FORMAT_MODEL;
      LOG_WARN("failed to check is valid to char number", K(ret));
    }
  }
  return ret;
}

int ObExprToCharCommon::convert_timelike_to_str(
    const ObExpr &expr, ObEvalCtx &ctx, ObIAllocator &alloc,
    const ObDatum &input, const ObObjType input_type, ObString &res) {
  int ret = OB_SUCCESS;
  ObScale in_scale;
  char *result_buf = NULL;
  int64_t pos = 0;
  int64_t result_buf_len = MAX_DATETIME_BUFFER_SIZE;
  if (OB_ISNULL(result_buf =
                    static_cast<char *>(alloc.alloc(result_buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate buff", K(ret), K(result_buf_len));
  } else {
    switch (ob_obj_type_class(input_type)) {
    case common::ObTimeTC: {
      in_scale = expr.args_[0]->datum_meta_.scale_;
      ret = ObTimeConverter::time_to_str(input.get_time(), in_scale, result_buf,
                                         result_buf_len, pos);
      break;
    }
    case common::ObYearTC: {
      ret = ObTimeConverter::year_to_str(input.get_year(), result_buf,
                                         result_buf_len, pos);
      break;
    }
    case ObDateTC: {
      ret = ObTimeConverter::date_to_str(input.get_date(), result_buf,
                                         result_buf_len, pos);
      break;
    }
    case ObMySQLDateTC: {
      ret = ObTimeConverter::mdate_to_str(input.get_mysql_date(), result_buf,
                                         result_buf_len, pos);
      break;
    }
    default: {
      ret = OB_INVALID_DATE_VALUE;
      LOG_WARN("input value is invalid", K(ret));
    }
    }
  }
  res = ObString(pos, result_buf);
  return ret;
}

int ObExprToCharCommon::convert_to_ob_time(ObEvalCtx &ctx,
                                           const ObDatum &input,
                                           const ObObjType input_type,
                                           const ObTimeZoneInfo *tz_info,
                                           ObTime &ob_time)
{
  int ret = OB_SUCCESS;
  switch (ob_obj_type_class(input_type)) {
    case ObTimeTC: {
      ret = ObTimeConverter::time_to_ob_time(input.get_time(), ob_time);
      break;
    }
    case ObYearTC: {
      ob_time.parts_[DT_YEAR] = input.get_year();
      break;
    }
    case ObDateTC: {
      ret = ObTimeConverter::date_to_ob_time(input.get_date(), ob_time);
      break;
    }
    case ObMySQLDateTC: {
      ret = ObTimeConverter::mdate_to_ob_time<true>(input.get_mysql_date(), ob_time);
      break;
    }
    case ObDateTimeTC: {
      ret = ObTimeConverter::datetime_to_ob_time(
          input.get_datetime(), input_type == ObTimestampType ? tz_info : NULL,
          ob_time);
      break;
    }
    case ObMySQLDateTimeTC: {
      ret = ObTimeConverter::mdatetime_to_ob_time<true>(input.get_mysql_datetime(), ob_time);
      break;
    }
    default: {
      ret = OB_INVALID_DATE_VALUE;
      LOG_WARN("input value is invalid", K(ret));
    }
  }
  return ret;
}

int ObExprToCharCommon::datetime_to_char(const ObExpr &expr, ObEvalCtx &ctx,
                                       ObIAllocator &alloc,
                                       const char *&input_ptr,
                                       uint32_t input_len, const ObString &fmt,
                                       const common::ObString &nlsparam,
                                       ObString &res) {
  return datetime_to_char(expr, ctx, alloc,
                          ObDatum(input_ptr, input_len, false),
                           fmt, nlsparam, res);
}

int ObExprToCharCommon::datetime_to_char(const ObExpr &expr,
                                         ObEvalCtx &ctx,
                                         ObIAllocator &alloc,
                                         const ObDatum &input,
                                         const ObString &fmt,
                                         const ObString &nlsparam,
                                         ObString &res)
{
  int ret = OB_SUCCESS;
  ObString format_str;
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  CK(NULL != session);
  ObSolidifiedVarsGetter helper(expr, ctx, session);
  const ObDatumMeta &input_meta = expr.args_[0]->datum_meta_;

  //param2: format
  if (OB_SUCC(ret)) {
    if (!fmt.empty()) {
      format_str = fmt;
    } else {
        format_str = "";
    }
  }

  ObTime ob_time;
  ObScale scale = input_meta.scale_;
  const ObTimeZoneInfo* tz_info = NULL;

  //determine type, get calc ob_time from input_value
  if (OB_SUCC(ret)) {
    if (OB_FAIL(helper.get_time_zone_info(tz_info))) {
      LOG_WARN("faild to get local timezone info", K(ret));
    } else if (OB_FAIL(convert_to_ob_time(ctx, input, input_meta.type_, tz_info, ob_time))) {
      LOG_WARN("fail to convert to ob time", K(ret));
    }
  }

  //print result
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(format_str.empty())) {
      res.reset();
      // handle case of mysql mode when input has no format string
      if (is_mysql_mode()) {
        if (ob_is_datetime_or_mysql_datetime_tc(input_meta.type_)) {
          const ObTimeZoneInfo *tz_info_use =
              (ObTimestampType == input_meta.type_) ? tz_info : NULL;
          char *result_buf = NULL;
          int64_t pos = 0;
          int64_t result_buf_len = MAX_DATETIME_BUFFER_SIZE;
          ObString nls_format;
          if (OB_ISNULL(result_buf = static_cast<char *>(alloc.alloc(result_buf_len)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate buff", K(ret), K(result_buf_len));
          } else if (ObMySQLDateTimeType == input_meta.type_ &&
                     OB_FAIL(ObTimeConverter::mdatetime_to_str(
                         input.get_mysql_datetime(), tz_info_use, nls_format,
                         scale, result_buf, result_buf_len, pos))) {
            LOG_WARN("failed to convert datetime to string", K(ret),
                     K(input.get_mysql_datetime()), KP(tz_info_use), K(nls_format),
                     K(scale), KP(result_buf), K(pos));
          } else if (ObDateTimeTC == ob_obj_type_class(input_meta.type_) &&
                     OB_FAIL(ObTimeConverter::datetime_to_str(
                         input.get_datetime(), tz_info_use, nls_format, scale,
                         result_buf, result_buf_len, pos))) {
            LOG_WARN("failed to convert datetime to string", K(ret),
                     K(input.get_datetime()), KP(tz_info_use), K(nls_format),
                     K(scale), KP(result_buf), K(pos));
          } else {
            res = ObString(pos, result_buf);
          }
        } else {
          if (OB_FAIL(convert_timelike_to_str(expr, ctx, alloc, input,
                                              input_meta.type_, res))) {
            LOG_WARN("fail to convert to string", K(ret), K(format_str));
          }
        }
      }
    } else {
      char *result_buf = NULL;
      int64_t pos = 0;
      int64_t result_buf_len = MAX_DATETIME_BUFFER_SIZE;
      if (OB_ISNULL(result_buf = static_cast<char *>(alloc.alloc(result_buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate buff", K(ret), K(result_buf_len));
      } else if (expr.arg_cnt_ > 1 && !!(expr.args_[1]->is_static_const_)) {
        auto rt_ctx_id = static_cast<uint64_t>(expr.expr_ctx_id_);
        ObExprDFMConvertCtx *dfm_convert_ctx = NULL;
        if (NULL == (dfm_convert_ctx = static_cast<ObExprDFMConvertCtx *>
                     (ctx.exec_ctx_.get_expr_op_ctx(rt_ctx_id)))) {
          if (OB_FAIL(ctx.exec_ctx_.create_expr_op_ctx(rt_ctx_id, dfm_convert_ctx))) {
            LOG_WARN("failed to create operator ctx", K(ret));
          } else if (OB_FAIL(dfm_convert_ctx->parse_format(format_str, expr.datum_meta_.type_,
                                                           false, ctx.exec_ctx_.get_allocator()))) {
            LOG_WARN("fail to parse format", K(ret), K(format_str));
          }
          LOG_DEBUG("new dfm convert ctx", K(ret), KPC(dfm_convert_ctx));
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(ObTimeConverter::ob_time_to_str_by_dfm_elems(ob_time, scale,
                                                                   dfm_convert_ctx->get_dfm_elems(),
                                                                   format_str, result_buf,
                                                                   result_buf_len, pos))) {
            LOG_WARN("failed to convert to string", K(ret), K(format_str));
          }
        }
      } else {
        if (OB_FAIL(ObTimeConverter::ob_time_to_str_oracle_dfm(
                  ob_time, scale, format_str, result_buf, result_buf_len, pos))) {
          LOG_WARN("failed to convert to varchar2", K(ret), K(format_str));
        }
      }
      if (OB_SUCC(ret)) {
        res = ObString(pos, result_buf);
      }
    }
  }

  LOG_DEBUG("expr to char function finished",
            K(ret), K(ob_time), K(input), K(format_str), K(nlsparam), K(res));

  return ret;
}

int ObExprToCharCommon::number_to_char(const ObExpr &expr, ObEvalCtx &ctx,
                                     common::ObIAllocator &alloc,
                                     const char *&input_ptr, uint32_t input_len,
                                     common::ObString &fmt_str,
                                     const common::ObString &nlsparam,
                                     common::ObString &res) {
  return number_to_char(expr, ctx, alloc, ObDatum(input_ptr, input_len, false),
                        fmt_str, nlsparam, res);
}

int ObExprToCharCommon::number_to_char(const ObExpr &expr,
                                       ObEvalCtx &ctx,
                                       common::ObIAllocator &alloc,
                                       const ObDatum &input,
                                       common::ObString &fmt_str,
                                       const common::ObString &nlsparam,
                                       common::ObString &res)
{
  int ret = OB_SUCCESS;
  ObString number_raw;
  int scale = -1;
  const ObObjMeta &obj_meta = expr.args_[0]->obj_meta_;
  if (1 == expr.arg_cnt_) {
    if (OB_FAIL(process_number_sci_value(expr, alloc, input, scale, res))) {
      LOG_WARN("fail to get number string", K(ret));
    }
  } else {
    if (OB_SUCC(ret)) {
      ObNFMToChar nfm;
      if (OB_FAIL(nfm.convert_num_to_fmt_str(obj_meta, expr.args_[0]->datum_meta_, input, alloc,
                  fmt_str.ptr(), fmt_str.length(), ctx, res))) {
        LOG_WARN("fail to convert num to fmt str", K(ret), K(fmt_str));
      }
    }
  }
  return ret;
}

int ObExprToCharCommon::process_number_sci_value(
    const ObExpr &expr, common::ObIAllocator &alloc, const ObDatum &input,
    const int scale, common::ObString &res)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  int64_t str_len = 0;
  const bool is_float = expr.args_[0]->obj_meta_.is_float() ||
                        expr.args_[0]->obj_meta_.is_ufloat();
  const bool is_double = expr.args_[0]->obj_meta_.is_double() ||
                         expr.args_[0]->obj_meta_.is_udouble();
  const bool is_decimal_int = expr.args_[0]->obj_meta_.is_decimal_int();
  const int64_t alloc_size = ((is_float || is_double)
                              ? MAX_NUMBER_BUFFER_SIZE
                              : MAX_TO_CHAR_BUFFER_SIZE);
  if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(alloc_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory for number string failed", K(ret));
  } else {
    if (is_double) {
      double val = input.get_double();
      if (isnan(fabs(val))){
        str_len = strlen("Nan");
        strncpy(buf, "Nan", str_len);
      } else if (fabs(val) == INFINITY) {
        str_len = strlen("Inf");
        strncpy(buf, "Inf", str_len);
      } else {
        str_len = ob_gcvt_opt(val, OB_GCVT_ARG_DOUBLE,
            static_cast<int32_t>(alloc_size), buf, NULL, TRUE);
      }
    } else if (is_float) {
      float val = input.get_float();
      if (isnan(fabs(val))){
        str_len = strlen("Nan");
        strncpy(buf, "Nan", str_len);
      } else if (fabs(val) == INFINITY) {
        str_len = strlen("Inf");
        strncpy(buf, "Inf", str_len); 
      } else {
        str_len = ob_gcvt_opt(val, OB_GCVT_ARG_FLOAT,
            static_cast<int32_t>(alloc_size), buf, NULL, TRUE);
      }
    } else if (is_decimal_int) {
      ObScale in_scale = expr.args_[0]->datum_meta_.scale_;
      if (OB_FAIL(wide::to_string(input.get_decimal_int(), input.get_int_bytes(),
          static_cast<int16_t>(in_scale), buf, static_cast<int32_t>(alloc_size), str_len, true))) {
        LOG_WARN("to_string failed", K(ret), K(input.get_int_bytes()), K(in_scale));
      }
    } else {
      number::ObNumber number_value;
      if (expr.args_[0]->obj_meta_.is_integer_type()) {
        if (OB_FAIL(number_value.from(input.get_int(), alloc))) {
          LOG_WARN("fail to int_number", K(ret));
        }
      } else {
        number_value.assign(input.get_number().desc_.desc_,
                            const_cast<uint32_t *>(&(input.get_number().digits_[0])));
      }
      if (OB_FAIL(ret)) {
      } else {
        if (OB_FAIL(number_value.format(buf, alloc_size, str_len,
                                            static_cast<int16_t>(scale)))) {
          LOG_WARN("fail to format number to string", K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      res.assign_ptr(buf, static_cast<int32_t>(str_len));
      LOG_DEBUG("process_number_sci_value", K(input), K(res));
    }
  }
  return ret;
}


int ObExprOracleToChar::eval_oracle_to_char(const ObExpr &expr,
                                                    ObEvalCtx &ctx,
                                                    ObDatum &expr_datum)
{
  return ObExprToCharCommon::eval_to_char(expr, ctx, expr_datum);
}

DEF_SET_LOCAL_SESSION_VARS(ObExprToCharCommon, raw_expr) {
  int ret = OB_SUCCESS;
  SET_LOCAL_SYSVAR_CAPACITY(4);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_NLS_DATE_FORMAT);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_NLS_TIMESTAMP_FORMAT);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_NLS_TIMESTAMP_TZ_FORMAT);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_TIME_ZONE);
  return ret;
}


} // end of sql
} // end of oceanbase
