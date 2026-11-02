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

#include "sql/engine/expr/ob_expr.h"
#include "vector_cast.h"
#include "share/ob_errno.h"
#include "share/datum/ob_datum_util.h"

#define USING_LOG_PREFIX SQL

namespace oceanbase
{
using namespace common;
namespace sql
{
OB_NOINLINE int _eval_arg_vec_cast(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                                   const EvalBound &bound);
OB_NOINLINE int _eval_arg_vec_copy_cast(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                                        const EvalBound &bound, const VecValueTypeClass in_tc,
                                        const VecValueTypeClass out_tc);

template <VecValueTypeClass in_tc, VecValueTypeClass out_tc, bool implicit>
struct EvalArgCasterImpl
{};

template <VecValueTypeClass in_tc, VecValueTypeClass out_tc>
struct EvalArgCasterImpl<in_tc, out_tc, IMPLICIT_CAST_FLAG>
{
  static int eval_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                         const EvalBound &bound)
  {
    return _eval_arg_vec_copy_cast(expr, ctx, skip, bound, in_tc, out_tc);
  }
};

template<VecValueTypeClass out_tc>
struct EvalArgCasterImpl<VEC_TC_NULL, out_tc, IMPLICIT_CAST_FLAG>
{
  static int eval_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                         const EvalBound &bound)
  {
    return _eval_arg_vec_cast(expr, ctx, skip, bound);
  }
};

template<VecValueTypeClass in_tc, VecValueTypeClass out_tc>
struct EvalArgCasterImpl<in_tc, out_tc, EXPLICIT_CAST_FLAG>
{
  static int eval_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                         const EvalBound &bound)
  {
    int ret =
      EvalArgCasterImpl<in_tc, out_tc, IMPLICIT_CAST_FLAG>::eval_vector(expr, ctx, skip, bound);
    if (OB_FAIL(ret)) {
      LOG_WARN("implicit eval arg for casting failed", K(ret));
    } else {
      VectorFormat out_fmt = expr.get_format(ctx);
      int warning = OB_SUCCESS;
      switch (out_fmt) {
      case common::VEC_UNIFORM: {
        if constexpr (is_uniform_vec(out_tc)) {
          ret = BatchValueRangeChecker<out_tc, ObUniformFormat<false>>::check(
            expr, ctx, bound, skip, warning);
        } else {
          ret = DummyChecker::check(expr, ctx, bound, skip, warning);
        }
        break;
      }
      case common::VEC_UNIFORM_CONST: {
        if constexpr (is_uniform_vec(out_tc)) {
          ret = BatchValueRangeChecker<out_tc, ObUniformFormat<true>>::check(
            expr, ctx, bound, skip, warning);
        } else {
          ret = DummyChecker::check(expr, ctx, bound, skip, warning);
        }
        break;
      }
      case common::VEC_FIXED: {
        if constexpr (is_fixed_length_vec(out_tc)) {
          ret = BatchValueRangeChecker<out_tc, ObFixedLengthFormat<RTCType<out_tc>>>::check(
            expr, ctx, bound, skip, warning);
        } else {
          ret = DummyChecker::check(expr, ctx, bound, skip, warning);
        }
        break;
      }
      case common::VEC_DISCRETE: {
        if constexpr (is_discrete_vec(out_tc)) {
          ret = BatchValueRangeChecker<out_tc, ObDiscreteFormat>::check(
            expr, ctx, bound, skip, warning);
        } else {
          ret = DummyChecker::check(expr, ctx, bound, skip, warning);
        }
        break;
      }
      case common::VEC_CONTINUOUS: {
        if constexpr (is_continuous_vec(out_tc)) {
          ret = BatchValueRangeChecker<out_tc, ObContinuousFormat>::check(
            expr, ctx, bound, skip, warning);
        } else {
          ret = DummyChecker::check(expr, ctx, bound, skip, warning);
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid format", K(out_fmt));
        break;
      }
      }
      if (OB_FAIL(ret)) {
        LOG_WARN("accuracy check failed", K(in_tc), K(out_tc), K(out_fmt), K(warning), K(ret));
      }
    }
    return ret;
  }
private:
  struct DummyChecker
  {
    inline static int check(const ObExpr &expr, ObEvalCtx &ctx, const EvalBound &bound,
                            const ObBitVector &skip, int &warning)
    {
      return OB_SUCCESS;
    }
  };
};

template<VecValueTypeClass out_tc>
struct EvalArgCasterImpl<VEC_TC_NULL, out_tc, EXPLICIT_CAST_FLAG>
{
  inline static int eval_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                         const EvalBound &bound)
  {
    return EvalArgCasterImpl<VEC_TC_NULL, out_tc, IMPLICIT_CAST_FLAG>::eval_vector(expr, ctx, skip,
                                                                                   bound);
  }
};

extern int expr_default_eval_vector_func(const ObExpr &, ObEvalCtx &, const ObBitVector &,
                                         const EvalBound &);

extern ObExpr::EvalVectorFunc VECTOR_CAST_FUNCS[MAX_VEC_TC][MAX_VEC_TC][2];
extern ObExpr::EvalVectorFunc VECTOR_EVAL_ARG_CAST_FUNCS[MAX_VEC_TC][MAX_VEC_TC][2];

template<int N, int M, bool defined = true>
struct VectorCastFuncInit
{
  static void init_array()
  {
    constexpr VecValueTypeClass in_tc = static_cast<VecValueTypeClass>(N);
    constexpr VecValueTypeClass out_tc = static_cast<VecValueTypeClass>(M);
    // if eval_vector is not defined, func ptr must set to `expr_default_eval_vector_func` for upgrading compatiblity.
    VECTOR_CAST_FUNCS[N][M][IMPLICIT_CAST_FLAG] =
      VectorCaster<in_tc, out_tc, IMPLICIT_CAST_FLAG>::defined_ ?
        VectorCaster<in_tc, out_tc, IMPLICIT_CAST_FLAG>::eval_vector :
        expr_default_eval_vector_func;
    VECTOR_CAST_FUNCS[N][M][EXPLICIT_CAST_FLAG] =
      VectorCaster<in_tc, out_tc, IMPLICIT_CAST_FLAG>::defined_ ?
        VectorCaster<in_tc, out_tc, EXPLICIT_CAST_FLAG>::eval_vector :
        expr_default_eval_vector_func;
    // eval arg funcs
    // VECTOR_EVAL_ARG_CAST_FUNCS[N][M][IMPLICIT_CAST_FLAG] =
    //   EvalArgCasterImpl<in_tc, out_tc, IMPLICIT_CAST_FLAG>::eval_vector;
    // accuracy checking will happend after explicit eval arg funcs,
    // if accuracy checker not defined, use `eval_default_eval_vector_func` otherwise
    // VECTOR_EVAL_ARG_CAST_FUNCS[N][M][EXPLICIT_CAST_FLAG] =
    //   ValueRangeChecker<out_tc, ObVectorBase>::defined_ ?
    //     EvalArgCasterImpl<in_tc, out_tc, EXPLICIT_CAST_FLAG>::eval_vector :
    //     expr_default_eval_vector_func;
  }
};

template<int N, int M>
struct VectorCastFuncInit<N, M, false>
{
  static void init_array() {}
};

template<int N, int M, bool defined = true>
struct EvalArgVecCasterFuncInit
{
  static void init_array()
  {
    constexpr VecValueTypeClass in_tc = static_cast<VecValueTypeClass>(N);
    constexpr VecValueTypeClass out_tc = static_cast<VecValueTypeClass>(M);
    VECTOR_EVAL_ARG_CAST_FUNCS[N][M][IMPLICIT_CAST_FLAG] =
      EvalArgCasterImpl<in_tc, out_tc, IMPLICIT_CAST_FLAG>::eval_vector;
    VECTOR_EVAL_ARG_CAST_FUNCS[N][M][EXPLICIT_CAST_FLAG] =
      EvalArgCasterImpl<in_tc, out_tc, EXPLICIT_CAST_FLAG>::eval_vector;
  }
};

template<int N, int M>
struct EvalArgVecCasterFuncInit<N, M, false>
{
  static void init_array()
  {
    constexpr VecValueTypeClass in_tc = static_cast<VecValueTypeClass>(N);
    constexpr VecValueTypeClass out_tc = static_cast<VecValueTypeClass>(M);
    VECTOR_EVAL_ARG_CAST_FUNCS[N][M][IMPLICIT_CAST_FLAG] =
      EvalArgCasterImpl<in_tc, out_tc, IMPLICIT_CAST_FLAG>::eval_vector;
    VECTOR_EVAL_ARG_CAST_FUNCS[N][M][EXPLICIT_CAST_FLAG] = expr_default_eval_vector_func;
  }
};

template<int N, int M>
using VectorCastIniter = VectorCastFuncInit<N, M,
                                            VectorCaster<static_cast<VecValueTypeClass>(N),
                                            static_cast<VecValueTypeClass>(M), IMPLICIT_CAST_FLAG>::defined_>;

template<int N, int M>
using EvalArgVecCasterIniter = EvalArgVecCasterFuncInit<N, M,
                                                        BatchValueRangeChecker<static_cast<VecValueTypeClass>(M),
                                                                          ObVectorBase>::defined_>;

} // end sql
} // end oceanbase
