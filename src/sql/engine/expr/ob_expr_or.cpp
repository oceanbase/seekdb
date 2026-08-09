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
// The special handling mentioned above applies when an empty string reaches OR
// evaluation directly.
int calc_or_exprN(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
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
  if (OB_SUCC(ret) && !res_datum.is_true()) {
    for (int64_t i = 1; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, child_res))) {
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(calc_or_expr2(res_datum, *child_res, res_datum))) {
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
  }
  return ret;
}

int ObExprOr::eval_or_batch_exprN(const ObExpr &expr, ObEvalCtx &ctx,
                                    const ObBitVector &skip, const int64_t batch_size)
{
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


}
}
