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
#include "sql/engine/expr/ob_expr_not_between.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprNotBetween::ObExprNotBetween(ObIAllocator &alloc)
    : ObRelationalExprOperator(alloc, T_OP_NOT_BTW, N_NOT_BTW, 3)
{
}

int calc_not_between_expr(const ObExpr &expr,
                                ObEvalCtx &ctx,
                                ObDatum &res_datum)
{
  // val < left or right < val
  int ret = OB_SUCCESS;
  ObDatum *val = NULL;
  ObDatum *left = NULL;
  ObDatum *right = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, val))) {
    LOG_WARN("eval arg 0 failed", K(ret));
  } else if (val->is_null()) {
    res_datum.set_null();
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, left))) {
    LOG_WARN("eval arg 1 failed", K(ret));
  } else if (OB_FAIL(expr.args_[2]->eval(ctx, right))) {
    LOG_WARN("eval arg 2 failed", K(ret));
  } else if (left->is_null() && right->is_null()) {
    res_datum.set_null();
  } else {
    bool left_cmp_succ = true;  // is val < left true or not
    bool right_cmp_succ = true; // is right < val true or not
    int cmp_ret = 0;
    if (!left->is_null()) {
      if (OB_FAIL((reinterpret_cast<DatumCmpFunc>(expr.inner_functions_[0]))(*val, *left, cmp_ret))) {
        LOG_WARN("cmp left failed", K(ret));
      } else {
        left_cmp_succ = cmp_ret < 0 ? true : false;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (left->is_null() || (!left_cmp_succ && !right->is_null())) {
      if (OB_FAIL((reinterpret_cast<DatumCmpFunc>(expr.inner_functions_[1]))(*right, *val, cmp_ret))) {
        LOG_WARN("cmp right failed", K(ret));
      } else {
        right_cmp_succ = cmp_ret < 0 ? true : false;
      }
    }
    if (OB_FAIL(ret)) {
    } else if ((left->is_null() && !right_cmp_succ) || (right->is_null() && !left_cmp_succ)) {
      res_datum.set_null();
    } else if (!left_cmp_succ && !right_cmp_succ) {
      res_datum.set_int32(0);
    } else {
      res_datum.set_int32(1);
    }
  }
  return ret;
}

int ObExprNotBetween::cg_expr(ObExprCGCtx &expr_cg_ctx,
                           const ObRawExpr &raw_expr,
                           ObExpr &rt_expr) const
{
  // val < left or right < val
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(3 != rt_expr.arg_cnt_) || OB_ISNULL(rt_expr.args_) ||
      OB_ISNULL(rt_expr.args_[0]) || OB_ISNULL(rt_expr.args_[1]) ||
      OB_ISNULL(rt_expr.args_[2]) || OB_ISNULL(expr_cg_ctx.allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("rt_expr is invalid", K(ret), K(rt_expr.arg_cnt_), KP(rt_expr.args_),
              KP(rt_expr.args_[0]), KP(rt_expr.args_[1]), KP(rt_expr.args_[2]));
  } else {
    DatumCmpFunc cmp_func_1 = NULL;  // val < left
    DatumCmpFunc cmp_func_2 = NULL;  // right < val
    const ObDatumMeta &val_meta = rt_expr.args_[0]->datum_meta_;
    const ObDatumMeta &left_meta = rt_expr.args_[1]->datum_meta_;
    const ObDatumMeta &right_meta = rt_expr.args_[2]->datum_meta_;
    const ObCollationType cmp_cs_type = val_meta.cs_type_;
    const bool has_lob_header1 = rt_expr.args_[0]->obj_meta_.has_lob_header() ||
                                 rt_expr.args_[1]->obj_meta_.has_lob_header();
    const bool has_lob_header2 = rt_expr.args_[0]->obj_meta_.has_lob_header() ||
                                 rt_expr.args_[2]->obj_meta_.has_lob_header();
    if (OB_ISNULL(cmp_func_1 = ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
                                                      val_meta.type_, left_meta.type_,
                                                      val_meta.scale_, left_meta.scale_,
                                                      val_meta.precision_, left_meta.precision_,
                                                      cmp_cs_type,
                                                      has_lob_header1))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get_datum_expr_cmp_func failed", K(ret), K(left_meta), K(val_meta), K(rt_expr));
    } else if (OB_ISNULL(cmp_func_2 = ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
                                                        right_meta.type_, val_meta.type_,
                                                        right_meta.scale_, val_meta.scale_,
                                                        right_meta.precision_, val_meta.precision_,
                                                        cmp_cs_type,
                                                        has_lob_header2))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get_datum_expr_cmp_func failed", K(ret), K(val_meta), K(right_meta), K(rt_expr));
    } else {
      rt_expr.eval_func_ = calc_not_between_expr;
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(rt_expr.inner_functions_ = reinterpret_cast<void **>(
              expr_cg_ctx.allocator_->alloc(sizeof(DatumCmpFunc) * 2)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc memory for inner_functions_ failed", K(ret));
      } else {
        rt_expr.inner_func_cnt_ = 2;
        rt_expr.inner_functions_[0] = reinterpret_cast<void *>(cmp_func_1);
        rt_expr.inner_functions_[1] = reinterpret_cast<void *>(cmp_func_2);
      }
    }
  }
  return ret;
}

}
}
