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
#include "sql/engine/expr/ob_expr_not.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
}
}


ObExprNot::ObExprNot(ObIAllocator &alloc)
    : ObLogicalExprOperator(alloc, T_OP_NOT, N_NOT, 1, NOT_ROW_DIMENSION)
{
}

int ObExprNot::calc_result_type1(ObExprResType &type,
                                 ObExprResType &type1,
                                 ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (NOT_ROW_DIMENSION == row_dimension_) {
    if (ObMaxType == type1.get_type()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      type.set_int32();
      ObExprOperator::calc_result_flag1(type, type1);
      type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
      type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
      //keep enumset as enumset
    }
  } else {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  }
  return ret;
}

int ObExprNot::cg_expr(ObExprCGCtx &expr_cg_ctx,
                       const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (OB_UNLIKELY(rt_expr.type_ != T_OP_NOT)
      || OB_UNLIKELY(rt_expr.arg_cnt_ != 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    rt_expr.eval_func_ = &eval_not;
    rt_expr.eval_batch_func_ = &eval_not_batch;
  }
  return ret;
}

int ObExprNot::eval_not(const ObExpr &expr,
                        ObEvalCtx &ctx,
                        ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr.args_)
      || OB_UNLIKELY(expr.arg_cnt_ != 1)
      || OB_ISNULL(expr.args_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    // not indicates that the parameter has been added with a bool expression, so the logic reaches here
    // The value of the parameter must be an int (0 or 1)
    ObDatum *param = NULL;
    if (OB_FAIL(expr.args_[0]->eval(ctx, param))) {
    } else if (param->is_null()) {
      expr_datum.set_null();
    } else {
      expr_datum.set_int(param->get_int() == 0);
      LOG_DEBUG("expr not calc result", K(expr_datum.is_null()), K(expr_datum.get_int()));
    }
  }
  return ret;
}

int ObExprNot::eval_not_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr.args_)
      || OB_UNLIKELY(expr.arg_cnt_ != 1)
      || OB_ISNULL(expr.args_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
    } else {
      ObDatum *results = expr.locate_batch_datums(ctx);
      for (int64_t i = 0; i < batch_size; ++i) {
        if (skip.at(i) || eval_flags.at(i)) {
          continue;
        }
        ObDatum &datum = expr.args_[0]->locate_expr_datum(ctx, i);
        eval_flags.set(i);
        if (datum.is_null()) {
          results[i].set_null();
        } else {
          results[i].set_int(0 == datum.get_int());
        }
      }
    }
  }
  return ret;
}
