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

#define USING_LOG_PREFIX SQL_EXE
#include "sql/engine/expr/ob_expr_between.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprBetween::ObExprBetween(ObIAllocator &alloc)
    : ObRelationalExprOperator(alloc, T_OP_BTW, N_BTW, 3)
{
}

int calc_between_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  // left <= val <= right
  int ret = OB_SUCCESS;
  ObDatum *val = NULL;
  ObDatum *left = NULL;
  ObDatum *right = NULL;
  const common::ObDatumAccessContext *datum_access_ctx = nullptr;
  if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx))) {
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, val))) {
  } else if (val->is_null()) {
    res_datum.set_null();
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, left))) {
  } else if (OB_FAIL(expr.args_[2]->eval(ctx, right))) {
  } else if (left->is_null() && right->is_null()) {
    res_datum.set_null();
  } else {
    bool left_cmp_succ = true;  // is left <= val true or not
    bool right_cmp_succ = true; // is val <= right true or not
    int cmp_ret = 0;
    if (!left->is_null()) {
      if (OB_FAIL((reinterpret_cast<DatumCmpFunc>(
                       expr.inner_functions_[0]))(
              *left, *val, cmp_ret, datum_access_ctx))) {
      } else {
        left_cmp_succ = cmp_ret <= 0 ? true : false;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (left->is_null() || (left_cmp_succ && !right->is_null())) {
      if (OB_FAIL((reinterpret_cast<DatumCmpFunc>(
                       expr.inner_functions_[1]))(
              *val, *right, cmp_ret, datum_access_ctx))) {
      } else {
        right_cmp_succ = cmp_ret <= 0 ? true : false;
      }
    }
    if (OB_FAIL(ret)) {
    } else if ((left->is_null() && right_cmp_succ) || (right->is_null() && left_cmp_succ)) {
      res_datum.set_null();
    } else if (left_cmp_succ && right_cmp_succ) {
      res_datum.set_int32(1);
    } else {
      res_datum.set_int32(0);
    }
  }
  return ret;
}

int ObExprBetween::cg_expr(ObExprCGCtx &expr_cg_ctx,
                           const ObRawExpr &raw_expr,
                           ObExpr &rt_expr) const
{
  // left <= val <= right
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(3 != rt_expr.arg_cnt_) || OB_ISNULL(rt_expr.args_) ||
      OB_ISNULL(rt_expr.args_[0]) || OB_ISNULL(rt_expr.args_[1]) ||
      OB_ISNULL(rt_expr.args_[2]) || OB_ISNULL(expr_cg_ctx.allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("rt_expr is invalid", K(ret), K(rt_expr.arg_cnt_), KP(rt_expr.args_),
              KP(rt_expr.args_[0]), KP(rt_expr.args_[1]), KP(rt_expr.args_[2]));
  } else {
    DatumCmpFunc cmp_func_1 = NULL;  // left <= val
    DatumCmpFunc cmp_func_2 = NULL;  // val <= right
    const ObDatumMeta &val_meta = rt_expr.args_[0]->datum_meta_;
    const ObDatumMeta &left_meta = rt_expr.args_[1]->datum_meta_;
    const ObDatumMeta &right_meta = rt_expr.args_[2]->datum_meta_;
    const ObCollationType cmp_cs_type = val_meta.cs_type_;
    const bool has_lob_header1 = rt_expr.args_[0]->obj_meta_.has_lob_header() ||
                                 rt_expr.args_[1]->obj_meta_.has_lob_header();
    const bool has_lob_header2 = rt_expr.args_[0]->obj_meta_.has_lob_header() ||
                                 rt_expr.args_[2]->obj_meta_.has_lob_header();
    if (OB_ISNULL(cmp_func_1 = ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
                                                        left_meta.type_, val_meta.type_,
                                                        left_meta.scale_, val_meta.scale_,
                                                        left_meta.precision_, val_meta.precision_,
                                                        cmp_cs_type,
                                                        has_lob_header1))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get_datum_expr_cmp_func failed", K(ret), K(left_meta), K(val_meta), K(rt_expr));
    } else if (OB_ISNULL(cmp_func_2 = ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
                                                        val_meta.type_, right_meta.type_,
                                                        val_meta.scale_, right_meta.scale_,
                                                        val_meta.precision_, right_meta.precision_,
                                                        cmp_cs_type,
                                                        has_lob_header2))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get_datum_expr_cmp_func failed", K(ret), K(val_meta), K(right_meta), K(rt_expr));
    } else {
      rt_expr.eval_func_ = calc_between_expr;
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
