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

#include "sql/engine/expr/ob_expr_length.h"

#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprLength::ObExprLength(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_LENGTH, N_LENGTH, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprLength::~ObExprLength()
{
}

int ObExprLength::calc_result_type1(ObExprResType &type, ObExprResType &text,
                                    ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = type_ctx.get_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else {
    type.set_int();
    type.set_scale(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].scale_);
    type.set_precision(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].precision_);
    if (ob_is_text_tc(text.get_type())) {
      // no need to do cast, save memory
    } else {
      text.set_calc_type(common::ObVarcharType);
    }
    OX(ObExprOperator::calc_result_flag1(type, text));
  }
  return ret;
}

int ObExprLength::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  if (rt_expr.arg_cnt_ != 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("length expr should have one param", K(ret), K(rt_expr.arg_cnt_));
  } else if (OB_ISNULL(rt_expr.args_) || OB_ISNULL(rt_expr.args_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of length expr is null", K(ret), K(rt_expr.args_));
  } else {
    ObObjType text_type = rt_expr.args_[0]->datum_meta_.type_;
    ObObjTypeClass type_class = ob_obj_type_class(text_type);

    if (!ob_is_castable_type_class(type_class)) {
      rt_expr.eval_func_ = ObExprLength::calc_null;
    } else {
      rt_expr.eval_func_ = ObExprLength::calc_mysql_mode;
      rt_expr.eval_vector_func_ = ObExprLength::calc_mysql_length_vector;
    }
  }
  return ret;
}

int ObExprLength::calc_null(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  UNUSED(expr);
  UNUSED(ctx);
  expr_datum.set_null();
  return OB_SUCCESS;
}
int ObExprLength::calc_mysql_mode(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *text_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, text_datum))) {
    LOG_WARN("eval param value failed", K(ret));
  } else if (text_datum->is_null()) {
    expr_datum.set_null();
  } else if (!is_lob_storage(expr.args_[0]->datum_meta_.type_)) {
    expr_datum.set_int(static_cast<int64_t>(text_datum->len_));
  } else { // text tc only
    ObLobLocatorV2 locator(text_datum->get_string(), expr.args_[0]->obj_meta_.has_lob_header());
    int64_t lob_data_byte_len = 0;
    if (OB_FAIL(locator.get_lob_data_byte_len(lob_data_byte_len))) {
      LOG_WARN("get lob data byte length failed", K(ret), K(locator));
    } else {
      expr_datum.set_int(static_cast<int64_t>(lob_data_byte_len));
    }
  }
  return ret;
}

template <typename ArgVec, typename ResVec>
int ObExprLength::calc_mysql_length_vector_dispatch(const ObExpr &expr,
                                      ObEvalCtx &ctx,
                                      const ObBitVector &skip,
                                      const EvalBound &bound) {
  int ret = OB_SUCCESS;
  ArgVec *arg_vec = reinterpret_cast<ArgVec *>(expr.args_[0]->get_vector(ctx));
  ResVec *res_vec = reinterpret_cast<ResVec *>(expr.get_vector(ctx));
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  for (int64_t idx = bound.start(); OB_SUCC(ret) && idx < bound.end(); ++idx) {
    if (skip.at(idx) || eval_flags.at(idx)) {
      continue;
    } else if (arg_vec->is_null(idx)) {
      res_vec->set_null(idx);
    } else {
      const char *ptr = NULL;
      ObLength len = 0;
      arg_vec->get_payload(idx, ptr, len);
      int64_t res_length = 0;
      if (!is_lob_storage(expr.args_[0]->datum_meta_.type_)) {
        res_length = len;
      } else {
        const ObLobCommon *lob = reinterpret_cast<const ObLobCommon *>(ptr);
        if (len != 0 && !lob->is_mem_loc_ && lob->in_row_) {
          res_length = lob->get_byte_size(len);
        } else {
          const ObMemLobCommon *memlob = reinterpret_cast<const ObMemLobCommon *>(ptr);
          if (len != 0 && memlob->has_inrow_data_ && memlob->has_extern_ == 0) {
            if (memlob->is_simple_) {
              res_length = len - sizeof(ObMemLobCommon);
            } else {
              const ObLobCommon *disklob = reinterpret_cast<const ObLobCommon *>(memlob->data_);
              res_length = disklob->get_byte_size(len - sizeof(ObMemLobCommon));
            }
          } else {
            ObLobLocatorV2 locator(ObString(len, ptr), expr.args_[0]->obj_meta_.has_lob_header());
            if (OB_FAIL(locator.get_lob_data_byte_len(res_length))) {
              LOG_WARN("get lob data byte length failed", K(ret), K(locator));
            }
          }
        }
      }
      res_vec->set_int(idx, res_length);
    }
    eval_flags.set(idx);
  } // for end
  return ret;
}

int ObExprLength::calc_mysql_length_vector(const ObExpr &expr,
                                           ObEvalCtx &ctx,
                                           const ObBitVector &skip,
                                           const EvalBound &bound)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.args_[0]->eval_vector(ctx, skip, bound))) {
    LOG_WARN("fail to eval sin param", K(ret));
  } else {
    VectorFormat arg_format = expr.args_[0]->get_format(ctx);
    VectorFormat res_format = expr.get_format(ctx);
    if (VEC_DISCRETE == arg_format && VEC_FIXED == res_format) {
      ret = calc_mysql_length_vector_dispatch<ObDiscreteFormat, ObFixedLengthFormat<int64_t>>(
        VECTOR_EVAL_FUNC_ARG_LIST);
    } else if (VEC_UNIFORM == arg_format && VEC_FIXED == res_format) {
      ret = calc_mysql_length_vector_dispatch<UniformFormat, ObFixedLengthFormat<int64_t>>(
        VECTOR_EVAL_FUNC_ARG_LIST);
    } else if (VEC_CONTINUOUS == arg_format && VEC_FIXED == res_format) {
      ret = calc_mysql_length_vector_dispatch<ObContinuousFormat, ObFixedLengthFormat<int64_t>>(
        VECTOR_EVAL_FUNC_ARG_LIST);
    } else if (VEC_DISCRETE == arg_format && VEC_UNIFORM == res_format) {
      ret = calc_mysql_length_vector_dispatch<ObDiscreteFormat, UniformFormat>(
        VECTOR_EVAL_FUNC_ARG_LIST);
    } else if (VEC_UNIFORM == arg_format && VEC_UNIFORM == res_format) {
      ret = calc_mysql_length_vector_dispatch<UniformFormat, UniformFormat>(
        VECTOR_EVAL_FUNC_ARG_LIST);
    } else if (VEC_CONTINUOUS == arg_format && VEC_UNIFORM == res_format) {
      ret = calc_mysql_length_vector_dispatch<ObContinuousFormat, UniformFormat>(
        VECTOR_EVAL_FUNC_ARG_LIST);
    } else {
      ret = calc_mysql_length_vector_dispatch<ObVectorBase, ObVectorBase>(
        VECTOR_EVAL_FUNC_ARG_LIST);
    }
  }
  return ret;
}

}
}
