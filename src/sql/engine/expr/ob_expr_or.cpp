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
#include "sql/engine/expr/ob_expr_or.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprOr::ObExprOr(ObIAllocator &alloc)
    : ObLogicalExprOperator(alloc, T_OP_OR, N_OR, PARAM_NUM_UNKNOWN, NOT_ROW_DIMENSION)
{
  param_lazy_eval_ = true;
}

int ObExprOr::calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  UNUSED(types_stack);
  UNUSED(param_num);
  int ret = OB_SUCCESS;
  //just keep enumset as origin
  type.set_int32();
  type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
  type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
  return ret;
}


// create table t1 as (select null or ''); -> NULL;
// create table t1 as (select 1 or ''); -> 1;
// create table t1 as (select 0 or ''); -> 0;
/*static int calc_with_one_empty_str(const ObDatum &in_datum, ObDatum &out_datum)
{
  int ret = OB_SUCCESS;
  if (in_datum.is_null()) {
    out_datum.set_null();
  } else {
    out_datum.set_bool(in_datum.get_bool());
  }
  return ret;
}*/

static int calc_or_expr2(const ObDatum &left, const ObDatum &right, ObDatum &res)
{
  int ret = OB_SUCCESS;
  if (left.is_true() || right.is_true()) {
    res.set_true();
  } else if (left.is_null() || right.is_null()) {
    res.set_null();
  } else {
    res.set_false();
  }
  return ret;
}
// Special processing:
// 1. select null or ''; -> or result is 0
//    Reason: For the select statement, cast_mode will automatically add WARN_ON_FAIL, so an empty string is converted to a number when,
//    Result is 0, error code is overwritten, the above statement will return 0
// 2. create table t1 as (select null or ''); -> the result of or is NULL
//    Reason: For non-select/explain statements, cast_mode does not have WARN_ON_FAIL, so the above empty string conversion to number,
//    report OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD, but for compatibility with MySQL, special handling will be performed here,
//    Overwrite the error code, and the result of or should be NULL rather than 0
//
// The special handling mentioned above for empty strings is specific to MySQL, because in Oracle mode both sub-nodes on either side of OR must be present
// Boolean semantic expression, will not be an empty string directly
int calc_or_exprN(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *tmp_res = NULL;
  ObDatum *child_res = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, tmp_res))) {
    LOG_WARN("eval arg 0 failed", K(ret));
  } else if (tmp_res->is_null()) {
    res_datum.set_null();
  } else {
    res_datum.set_bool(tmp_res->get_bool());
  }
  if (OB_SUCC(ret) && !res_datum.is_true()) {
    for (int64_t i = 1; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, child_res))) {
        LOG_WARN("eval arg failed", K(ret), K(i));
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(calc_or_expr2(res_datum, *child_res, res_datum))) {
          LOG_WARN("calc_or_expr2 failed", K(ret), K(i));
        } else if (res_datum.is_true()) {
          break;
        }
      }
    }
  }
  return ret;
}

int ObExprOr::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
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
    rt_expr.eval_func_ = calc_or_exprN;
    rt_expr.eval_batch_func_ = eval_or_batch_exprN;
    rt_expr.eval_vector_func_ = eval_or_vector;
  }
  return ret;
}

int ObExprOr::eval_or_batch_exprN(const ObExpr &expr, ObEvalCtx &ctx,
                                    const ObBitVector &skip, const int64_t batch_size)
{
  LOG_DEBUG("eval or batch mode", K(batch_size));
  int ret = OB_SUCCESS;
  ObDatum *results = expr.locate_batch_datums(ctx);
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
    *args_[first] needs to set eval flags, set initial value, set skip to true
    *args_[middle] needs to set a non-false result, set skip to true
    *args_[last] only needs to set a non-false result*/

    //eval first
    if (real_param == skip_cnt) {
    } else if (OB_FAIL(expr.args_[0]->eval_batch(ctx, my_skip, batch_size))) {
      LOG_WARN("failed to eval batch result args0", K(ret));
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
        } else if (true == curr_datum->get_bool()) {
          results[j].set_bool(true);
          my_skip.set(j);
        } else {
          results[j].set_bool(false);
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
        } else if (true == curr_datum->get_bool()) {
          results[j].set_bool(true);
          my_skip.set(j);
        } else {
          results[j].set_bool(false);
        }
        eval_flags.set(j);
      }
    }
    //eval middle
    int64_t arg_idx = 1;
    for (; OB_SUCC(ret) && arg_idx < expr.arg_cnt_ - 1 && skip_cnt < real_param; ++arg_idx) {
      if (OB_FAIL(expr.args_[arg_idx]->eval_batch(ctx, my_skip, batch_size))) {
        LOG_WARN("failed to eval batch result", K(ret), K(arg_idx));
      } else if (expr.args_[arg_idx]->is_batch_result()) {
        ObDatum *curr_datum = nullptr;
        ObDatum *datum_array = expr.args_[arg_idx]->locate_batch_datums(ctx);
        for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
          if (my_skip.at(j)) {
            continue;
          }
          curr_datum = &datum_array[j];
          if (curr_datum->is_null()) {
            results[j].set_null();
          } else if (true == curr_datum->get_bool()) {
            results[j].set_bool(true);
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
          } else if (true == curr_datum->get_bool()) {
            results[j].set_bool(true);
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
      LOG_WARN("failed to eval batch result args0", K(ret));
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
        } else if (true == curr_datum->get_bool()) {
          results[j].set_bool(true);
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
        } else if (true == curr_datum->get_bool()) {
          results[j].set_bool(true);
        } else {
          //do nothing
        }
      }
    }
  }
  return ret;
}

template <typename ArgVec, typename ResVec,
          ObExprOr::EvalOrStage Stage>
static int inner_eval_or_vector(const ObExpr &expr,
                                 ObEvalCtx &ctx,
                                 ObBitVector &my_skip,
                                 const EvalBound &bound,
                                 const int64_t& arg_idx,
                                 int64_t& skip_cnt)
{
  int ret = OB_SUCCESS;
  ArgVec *curr_vec = static_cast<ArgVec *>(expr.args_[arg_idx]->get_vector(ctx));
  ResVec *results = static_cast<ResVec *>(expr.get_vector(ctx));
  for (int64_t j = bound.start(); OB_SUCC(ret) && j < bound.end(); ++j) {
    if (my_skip.at(j)) {
      continue;
    }
    if (curr_vec->is_null(j)) {
      results->set_null(j);
    } else if (true == curr_vec->get_bool(j)) {
      if (Stage == ObExprOr::FIRST) {
        my_skip.set(j);
        ++skip_cnt;
      } else if (Stage == ObExprOr::MIDDLE) {
        results->unset_null(j);
        my_skip.set(j);
        ++skip_cnt;
      } else {
        results->unset_null(j);
      }
      results->set_bool(j, true);
    } else {
      if (Stage == ObExprOr::FIRST) {
        results->set_bool(j, false);
      }
    }
  }
  return ret;
}

static int dispatch_eval_or_vector(const ObExpr &expr,
                                    ObEvalCtx &ctx,
                                    ObBitVector &my_skip,
                                    const EvalBound &bound,
                                    const int64_t& arg_idx,
                                    int64_t& skip_cnt)
{
  int ret = OB_SUCCESS;
  VectorFormat res_format = expr.get_format(ctx);
  VectorFormat arg_format = expr.args_[arg_idx]->get_format(ctx);
  // When res_format == VEC_FIXED,
  // the template parameter cannot be passed as ObVectorBase.
  // If ObVectorBase is passed,
  // the condition typeid(ResVec) == typeid(IntegerFixedVec) will become invalid,
  // resulting in unset_null not being called and causing correctness issues.
  if (arg_idx == 0 &&
      arg_format == VEC_FIXED &&
      res_format == VEC_FIXED) {
    ret = inner_eval_or_vector<IntegerFixedVec, IntegerFixedVec, ObExprOr::FIRST>(
                                        expr, ctx, my_skip, bound, arg_idx, skip_cnt);
  } else if (arg_idx == 0) {
    ret = inner_eval_or_vector<ObVectorBase, ObVectorBase, ObExprOr::FIRST>(
                                  expr, ctx, my_skip, bound, arg_idx, skip_cnt);
  } else if (arg_idx == expr.arg_cnt_ - 1 &&
             arg_format == VEC_FIXED &&
             res_format == VEC_FIXED) {
    ret = inner_eval_or_vector<IntegerFixedVec, IntegerFixedVec, ObExprOr::LAST>(
                                      expr, ctx, my_skip, bound, arg_idx, skip_cnt);
  } else if (arg_idx == expr.arg_cnt_ - 1) {
    ret = inner_eval_or_vector<ObVectorBase, ObVectorBase, ObExprOr::LAST>(
                                expr, ctx, my_skip, bound, arg_idx, skip_cnt);
  } else if (arg_format == VEC_FIXED &&
             res_format == VEC_FIXED) {
    ret = inner_eval_or_vector<IntegerFixedVec, IntegerFixedVec, ObExprOr::MIDDLE>(
                                        expr, ctx, my_skip, bound, arg_idx, skip_cnt);
  } else {
    ret = inner_eval_or_vector<ObVectorBase, ObVectorBase, ObExprOr::MIDDLE>(
                                  expr, ctx, my_skip, bound, arg_idx, skip_cnt);
  }
  return ret;
}

int ObExprOr::eval_or_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound)
{
  int ret = OB_SUCCESS;
  ObBitVector &my_skip = expr.get_pvt_skip(ctx);
  my_skip.deep_copy(skip, bound.start(), bound.end());
  const int64_t total_cnt = bound.end() - bound.start();
  // Record the number of skips in a batch,
  // end when all parameters are skipped.
  int64_t skip_cnt = my_skip.accumulate_bit_cnt(bound);
  EvalBound my_bound = bound;

  for (int64_t arg_idx = 0; OB_SUCC(ret) &&
       arg_idx < expr.arg_cnt_ && skip_cnt < total_cnt; ++arg_idx) {
    if (skip_cnt > 0) {
      my_bound.set_all_row_active(false);
    }
    if (OB_FAIL(expr.args_[arg_idx]->eval_vector(ctx, my_skip, my_bound))) {
      LOG_WARN("failed to eval vector result", K(ret), K(arg_idx));
    } else if (OB_FAIL(dispatch_eval_or_vector(
                expr, ctx, my_skip, my_bound, arg_idx, skip_cnt))){
      LOG_WARN("failed to dispatch eval vector or", K(ret),
      K(expr), K(ctx), K(my_bound), K(arg_idx), K(skip_cnt));
    }
  }

  // It would be more reasonable for eval_flags to be set after the calculation is completed
  // rather than setting it to 1 before the calculation.
  if (OB_SUCC(ret)) {
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    eval_flags.bit_not(skip, my_bound);
  }

  return ret;
}

}
}

