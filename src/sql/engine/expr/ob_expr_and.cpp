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
#include "sql/engine/expr/ob_expr_and.h"
#include "src/sql/resolver/ddl/ob_explain_stmt.h"
namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprAnd::ObExprAnd(ObIAllocator &alloc):
    ObLogicalExprOperator(alloc, T_OP_AND, N_AND, PARAM_NUM_UNKNOWN, NOT_ROW_DIMENSION)
{
  param_lazy_eval_ = true;
};
// scale and precision also unify
int ObExprAnd::calc_result_typeN(ObExprResType &type,
                                 ObExprResType *types_stack,
                                 int64_t param_num,
                                 ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(types_stack)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null types", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_num; i++) {
      if (types_stack[i].is_ext()) {
        ret = OB_ERR_EXPRESSION_WRONG_TYPE;
        LOG_WARN("expression is of wrong type", K(ret), K(types_stack[i].get_type()));
      }
    }
  }

  if (OB_SUCC(ret)) {
    type.set_type(ObInt32Type);
    type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
    type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
  }
  return ret;
}

static int calc_and_expr2(const ObDatum &left, const ObDatum &right,
                          ObDatum &res)
{
  int ret = OB_SUCCESS;
  if (left.is_false() || right.is_false()) {
    res.set_false();
  } else if (left.is_null() || right.is_null()) {
    res.set_null();
  } else {
    res.set_true();
  }
  return ret;
}

int calc_and_exprN(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *tmp_res = NULL;
  ObDatum *child_res = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, tmp_res))) {
  } else if (tmp_res->is_null()) {
    res_datum.set_null();
  } else {
    res_datum.set_bool(tmp_res->get_bool());
  }
  if (OB_SUCC(ret) && !res_datum.is_false()) {
    for (int64_t i = 1; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, child_res))) {
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(calc_and_expr2(res_datum, *child_res, res_datum))) {
        } else if (res_datum.is_false()) {
          break;
        }
      }
    }
  }
  return ret;
}

int ObExprAnd::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  if (OB_ISNULL(rt_expr.args_) || OB_UNLIKELY(2 > rt_expr.arg_cnt_) ||
      OB_UNLIKELY(rt_expr.arg_cnt_ != raw_expr.get_param_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("args_ is NULL or arg_cnt_ is invalid or raw_expr is invalid",
              K(ret), K(rt_expr), K(raw_expr));
  } else {
    rt_expr.eval_func_ = calc_and_exprN;
    rt_expr.eval_batch_func_ = eval_and_batch_exprN;
  }
  return ret;
}

int ObExprAnd::eval_and_batch_exprN(const ObExpr &expr, ObEvalCtx &ctx,
                                    const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  ObDatum* results = expr.locate_batch_datums(ctx);
  if (OB_ISNULL(results)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expr results frame is not init", K(ret));
  } else {
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    ObBitVector &my_skip = expr.get_pvt_skip(ctx);
    my_skip.deep_copy(skip, batch_size);
    const int64_t real_param = batch_size - my_skip.accumulate_bit_cnt(batch_size);
    int64_t skip_cnt = 0; // record the number of skips in a batch, all parameters are skipped to end

    /*To omit the initialization of results and reduce writes to my_skip, the evaluation is divided into 3 segments
    *args_[first] needs to set eval flags, set initial value, and set skip to false
    *args_[middle] needs to set a non-true result, and skip to false
    *args_[last] only needs to set a non-true result
    */

    //eval first
    if (real_param == skip_cnt) {
    } else if (OB_FAIL(expr.args_[0]->eval_batch(ctx, my_skip, batch_size))) {
    } else if (expr.args_[0]->is_batch_result()) {
      ObDatum *curr_datum  = nullptr;
      ObDatum *datum_array = expr.args_[0]->locate_batch_datums(ctx);
      for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
        if (my_skip.at(j)) {
          continue;
        }
        curr_datum = &datum_array[j];
        if (curr_datum->is_null()) {
          results[j].set_null();
        } else if (false == curr_datum->get_bool()) {
          results[j].set_bool(false);
          my_skip.set(j);
          ++skip_cnt;
        } else {
          results[j].set_bool(true);
        }
        eval_flags.set(j);
      }
    } else {
      ObDatum *curr_datum = &expr.args_[0]->locate_expr_datum(ctx);
      for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
        if (my_skip.at(j)) {
          continue;
        }
        if (curr_datum->is_null()) {
          results[j].set_null();
        } else if (false == curr_datum->get_bool()) {
          results[j].set_bool(false);
          my_skip.set(j);
          ++skip_cnt;
        } else {
          results[j].set_bool(true);
        }
        eval_flags.set(j);
      }
    }

    //eval middle
    int64_t arg_idx = 1;
    for (; OB_SUCC(ret) && arg_idx < expr.arg_cnt_ - 1 && skip_cnt < real_param; ++arg_idx) {
      if (OB_FAIL(expr.args_[arg_idx]->eval_batch(ctx, my_skip, batch_size))) {
      } else if (expr.args_[arg_idx]->is_batch_result()) {
        ObDatum *curr_datum  = nullptr;
        ObDatum *datum_array = expr.args_[arg_idx]->locate_batch_datums(ctx);
        for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
          if (my_skip.at(j)) {
            continue;
          }
          curr_datum = &datum_array[j];
          if (curr_datum->is_null()) {
            results[j].set_null();
          } else if (false == curr_datum->get_bool()) {
            results[j].set_bool(false);
            my_skip.set(j);
          } else {
            //do nothing
          }
        }
      } else {
        ObDatum *curr_datum = &expr.args_[arg_idx]->locate_expr_datum(ctx);
        for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
          if (my_skip.at(j)) {
            continue;
          }
          if (curr_datum->is_null()) {
            results[j].set_null();
          } else if (false == curr_datum->get_bool()) {
            results[j].set_bool(false);
            my_skip.set(j);
          } else {
            //do nothing
          }
        }
      }
    }

    //eval last
    if (OB_FAIL(ret)) {
    } else if (real_param == skip_cnt) {
    } else if (OB_FAIL(expr.args_[arg_idx]->eval_batch(ctx, my_skip, batch_size))) {
    } else if (expr.args_[arg_idx]->is_batch_result()) {
      ObDatum *curr_datum  = nullptr;
      ObDatum *datum_array = expr.args_[arg_idx]->locate_batch_datums(ctx);
      for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
        if (my_skip.at(j)) {
          continue;
        }
        curr_datum = &datum_array[j];
        if (curr_datum->is_null()) {
          results[j].set_null();
        } else if (false == curr_datum->get_bool()) {
          results[j].set_bool(false);
        } else {
          //do nothing
        }
      }
    } else {
      ObDatum *curr_datum = &expr.args_[arg_idx]->locate_expr_datum(ctx);
      for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
        if (my_skip.at(j)) {
          continue;
        }
        if (curr_datum->is_null()) {
          results[j].set_null();
        } else if (false == curr_datum->get_bool()) {
          results[j].set_bool(false);
        } else {
          //do nothing
        }
      }
    }
  }
  return ret;
}


}
}
