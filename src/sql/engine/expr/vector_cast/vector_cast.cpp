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

#include "vector_cast.h"
#include "sql/engine/vector/ob_vector_define.h"

#define USING_LOG_PREFIX SQL

namespace oceanbase
{
using namespace common;
namespace sql
{

static bool is_vec_format_valid_for_tc(const VecValueTypeClass tc, const VectorFormat fmt)
{
  switch (fmt) {
  case VEC_UNIFORM:
  case VEC_UNIFORM_CONST:
    return is_uniform_vec(tc);
  case VEC_FIXED:
    return is_fixed_length_vec(tc);
  case VEC_DISCRETE:
    return is_discrete_vec(tc);
  case VEC_CONTINUOUS:
    return is_continuous_vec(tc);
  default:
    return false;
  }
}

int _eval_arg_vec_cast(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                                   const EvalBound &bound)
{
#define SET_NULLS(out_vec_type)                                                                    \
  for (int i = bound.start(); i < bound.end(); i++) {                                              \
    if (eval_flags.at(i) || skip.at(i)) {                                                          \
      continue;                                                                                    \
    } else {                                                                                       \
      static_cast<out_vec_type *>(output_vector)->set_null(i);                                     \
      eval_flags.set(i);                                                                           \
    }                                                                                              \
  }
  int ret = OB_SUCCESS;
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  VectorFormat out_fmt = expr.get_format(ctx);
  ObIVector *output_vector = expr.get_vector(ctx);
  if (out_fmt == VEC_UNIFORM) {
    SET_NULLS(ObUniformFormat<false>);
  } else if (out_fmt == VEC_UNIFORM_CONST) {
    SET_NULLS(ObUniformFormat<true>);
  } else {
    SET_NULLS(ObBitmapNullVectorBase);
  }
  return ret;
#undef SET_NULLS
}

int _eval_arg_vec_copy_cast(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                            const EvalBound &bound, const VecValueTypeClass in_tc,
                            const VecValueTypeClass out_tc)
{
  int ret = OB_SUCCESS;
  ObIVector *input_vector = nullptr;
  ObIVector *output_vector = nullptr;
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  const char *src = nullptr;
  ObLength src_len = 0;
  if (OB_FAIL(expr.args_[0]->eval_vector(ctx, skip, bound))) {
    LOG_WARN("eval vector failed", K(ret));
  } else if (OB_UNLIKELY(!is_vec_format_valid_for_tc(in_tc, expr.args_[0]->get_format(ctx)))
             || OB_UNLIKELY(!is_vec_format_valid_for_tc(out_tc, expr.get_format(ctx)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("choose format failed", K(ret), K(in_tc), K(out_tc),
             K(expr.args_[0]->get_format(ctx)), K(expr.get_format(ctx)));
  } else if (OB_ISNULL(input_vector = expr.args_[0]->get_vector(ctx))
             || OB_ISNULL(output_vector = expr.get_vector(ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null vector", K(ret), K(input_vector), K(output_vector));
  } else if (OB_LIKELY(bound.get_all_rows_active() && eval_flags.accumulate_bit_cnt(bound) == 0)) {
    if (!input_vector->has_null()) {
      for (int i = bound.start(); i < bound.end(); i++) {
        input_vector->get_payload(i, src, src_len);
        output_vector->set_payload_shallow(i, src, src_len);
      }
    } else {
      for (int i = bound.start(); i < bound.end(); i++) {
        if (input_vector->is_null(i)) {
          output_vector->set_null(i);
        } else {
          input_vector->get_payload(i, src, src_len);
          output_vector->set_payload_shallow(i, src, src_len);
        }
      }
    }
    eval_flags.set_all(bound.start(), bound.end());
  } else {
    for (int i = bound.start(); i < bound.end(); i++) {
      if (eval_flags.at(i) || skip.at(i)) {
        continue;
      } else if (input_vector->is_null(i)) {
        output_vector->set_null(i);
      } else {
        input_vector->get_payload(i, src, src_len);
        output_vector->set_payload_shallow(i, src, src_len);
      }
      eval_flags.set(i);
    }
  }
  return ret;
}

extern int __init_all_vec_cast_funcs();

// 0 for explicit cast, 1 for implicit cast
ObExpr::EvalVectorFunc VECTOR_CAST_FUNCS[MAX_VEC_TC][MAX_VEC_TC][2] = {};
ObExpr::EvalVectorFunc VECTOR_EVAL_ARG_CAST_FUNCS[MAX_VEC_TC][MAX_VEC_TC][2] = {};

void __init_vec_cast_default_funcs()
{
  for (int in_tc = 0; in_tc < MAX_VEC_TC; ++in_tc) {
    for (int out_tc = 0; out_tc < MAX_VEC_TC; ++out_tc) {
      VECTOR_CAST_FUNCS[in_tc][out_tc][IMPLICIT_CAST_FLAG] = expr_default_eval_vector_func;
      VECTOR_CAST_FUNCS[in_tc][out_tc][EXPLICIT_CAST_FLAG] = expr_default_eval_vector_func;
    }
  }
}

static int init_vector_cast_ret = __init_all_vec_cast_funcs();

extern int cast_not_expected(const sql::ObExpr &, sql::ObEvalCtx &, sql::ObDatum &);
extern int cast_not_support(const sql::ObExpr &, sql::ObEvalCtx &, sql::ObDatum &);
extern int cast_inconsistent_types(const sql::ObExpr &, sql::ObEvalCtx &, sql::ObDatum &);
extern int cast_inconsistent_types_json(const sql::ObExpr &, sql::ObEvalCtx &, sql::ObDatum &);
extern int cast_to_udt_not_support(const sql::ObExpr &, sql::ObEvalCtx &, sql::ObDatum &);
extern int unknown_other(const sql::ObExpr &, sql::ObEvalCtx &, sql::ObDatum &);

ObExpr::EvalVectorFunc VectorCasterUtil::get_vector_cast(const VecValueTypeClass in_tc,
                                                         const VecValueTypeClass out_tc,
                                                         const bool is_eval_arg_cast,
                                                         ObExpr::EvalFunc row_cast_fn,
                                                         const ObCastMode cast_mode)
{
  ObExpr::EvalVectorFunc ret_func = nullptr;
  ObExpr::EvalFunc temp_func = nullptr;
  if (is_eval_arg_cast) {
    ret_func = CM_IS_EXPLICIT_CAST(cast_mode) ? VECTOR_EVAL_ARG_CAST_FUNCS[in_tc][out_tc][EXPLICIT_CAST_FLAG] :
                                                VECTOR_EVAL_ARG_CAST_FUNCS[in_tc][out_tc][IMPLICIT_CAST_FLAG];
  } else if (row_cast_fn == (temp_func = cast_not_expected)
             || row_cast_fn == cast_not_support
             || row_cast_fn == cast_inconsistent_types
             || row_cast_fn == cast_inconsistent_types_json
             || row_cast_fn == cast_to_udt_not_support
             || row_cast_fn == unknown_other) {
    // if rt_expr.eval_func_ is set to any of functions listing above,
    // casting routine must fail, we use row casting function to report err_code by set eval_vector_func_ to be nullptr
    // do nothing
  } else {
    ret_func = VECTOR_CAST_FUNCS[in_tc][out_tc][CM_IS_IMPLICIT_CAST(cast_mode)];
  }
  LOG_DEBUG("choose vector casting funcs", K(in_tc), K(out_tc), K(is_eval_arg_cast), K(cast_mode));
  return ret_func;
}

} // end sql
} // end oceanbase
