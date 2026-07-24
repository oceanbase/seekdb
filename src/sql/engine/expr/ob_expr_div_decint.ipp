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

#ifndef OCEANBASE_EXPR_DIV_DECINT_IPP_
#define OCEANBASE_EXPR_DIV_DECINT_IPP_
#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_div.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/expr/ob_batch_eval_util.h"

namespace oceanbase
{
namespace sql
{
using namespace common;
using namespace share;

template<typename LVal, typename RVal>
struct ObDecintMySQLDivDatumFunc
{
  OB_INLINE int operator()(ObDatum &res, const ObDatum &l, const ObDatum &r,
                           const bool is_err_div_by_zero, const ObScale round_up_scale,
                           const ObScale decint_res_scale) const
  {
    int ret = OB_SUCCESS;
    const LVal &numerator = *reinterpret_cast<const LVal *>(l.get_decimal_int());
    const RVal &denominator = *reinterpret_cast<const RVal *>(r.get_decimal_int());
    LVal quo;
    number::ObNumber res_nmb;
    ObNumStackOnceAlloc tmp_alloc;
    if (OB_UNLIKELY(denominator == 0)) {
      if (is_err_div_by_zero) {
        ret = OB_DIVISION_BY_ZERO;
      } else {
        res.set_null();
        LOG_DEBUG("divisor is equal to zero", K(l), K(r), K(ret));
      }
    } else {
      quo = numerator / denominator;
      ObScale round_scale = MIN(round_up_scale, decint_res_scale);
      if (OB_FAIL(wide::to_number(quo, decint_res_scale, tmp_alloc, res_nmb))) {
        LOG_WARN("wide::to_number failed", K(ret));
      } else if (round_scale < decint_res_scale && OB_FAIL(res_nmb.trunc(round_scale))) {
        LOG_WARN("truncate number failed", K(ret));
      } else {
        res.set_number(res_nmb);
      }
    }
    return ret;
  }
};


template<typename ltype, typename rtype>
int ObExprDiv::decint_div_mysql_fn(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  int64_t div_inc = 0;
  if (OB_FAIL(ctx.exec_ctx_.get_my_session()->get_div_precision_increment(div_inc))) {
    LOG_WARN("get_div_precision_increment failed", K(ret));
  } else {
    ObScale round_up_scale = decint_res_round_up_scale(expr, div_inc);
    ret = def_arith_eval_func<ObDecintMySQLDivDatumFunc<ltype, rtype>>(
      expr, ctx, expr_datum, expr.is_error_div_by_zero_, round_up_scale, expr.div_calc_scale_);
  }
  return ret;
}

template <typename ltype, typename rtype>
int ObExprDiv::decint_div_mysql_batch_fn(BATCH_EVAL_FUNC_ARG_DECL)
{
  int ret = OB_SUCCESS;
  int64_t div_inc = 0;
  if (OB_FAIL(binary_operand_batch_eval(expr, ctx, skip, size, false))) { // mysql mode
    LOG_WARN("eval operands failed", K(ret));
  } else if (OB_FAIL(ctx.exec_ctx_.get_my_session()->get_div_precision_increment(div_inc))) {
    LOG_WARN("get_div_precision_increment failed", K(ret));
  } else {
    ObDatumVector l_vec = expr.args_[0]->locate_expr_datumvector(ctx);
    ObDatumVector r_vec = expr.args_[1]->locate_expr_datumvector(ctx);
    ObDatum *res_datums = expr.locate_datums_for_update(ctx, size);
    ObDecintMySQLDivDatumFunc<ltype, rtype> div_fn;
    // ObScale res_scale = expr.datum_meta_.scale_;
    ObScale round_up_scale = decint_res_round_up_scale(expr, div_inc);
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    for (int i = 0; OB_SUCC(ret) && i < size; i++) {
      if (skip.at(i) || eval_flags.at(i)) {
      } else if (l_vec.at(i)->is_null() || r_vec.at(i)->is_null()) {
        res_datums[i].set_null();
        eval_flags.set(i);
      } else {
        ret = div_fn(res_datums[i], *l_vec.at(i), *r_vec.at(i), expr.is_error_div_by_zero_,
                     round_up_scale, expr.div_calc_scale_);
        if (OB_SUCC(ret)) {
          eval_flags.set(i);
        }
      }
    }
  }
  return ret;
}

ObScale ObExprDiv::decint_res_round_up_scale(const ObExpr &expr, int64_t div_inc)
{
  ObScale decint_res_scale = expr.div_calc_scale_;
  ObScale s2 = expr.args_[1]->datum_meta_.scale_;
  ObScale p2 = expr.args_[1]->datum_meta_.precision_;
  ObScale s1 = decint_res_scale - div_inc - extra_scale_for_decint_div - p2 + s2;
  ObScale res_scale = MAX(round_up_scale(s1) + round_up_scale(s2), round_up_scale(s1 + s2 + div_inc));
  return res_scale;
}

} // end sql
} // end oceanbase

#endif // OCEANBASE_EXPR_DIV_DECINT_IPP_

