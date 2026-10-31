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
                                Datum &res_datum)
{
  // val < left or right < val
  int ret = OB_SUCCESS;
  Datum *val = NULL;
  Datum *left = NULL;
  Datum *right = NULL;
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
    bool left_cmp_succ = true;  // is val < left true or not
    bool right_cmp_succ = true; // is right < val true or not
    int cmp_ret = 0;
    if (!left->is_null()) {
      if (OB_FAIL(expr.get_cmp_op(0)(
              *val, *left, cmp_ret, datum_access_ctx))) {
      } else {
        left_cmp_succ = cmp_ret < 0 ? true : false;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (left->is_null() || (!left_cmp_succ && !right->is_null())) {
      if (OB_FAIL(expr.get_cmp_op(1)(
              *right, *val, cmp_ret, datum_access_ctx))) {
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
  int ret = OB_SUCCESS;
  if (rt_expr.arg_cnt_ != 3 || rt_expr.args_ == nullptr || rt_expr.args_[0] == nullptr
      || rt_expr.args_[1] == nullptr || rt_expr.args_[2] == nullptr || expr_cg_ctx.allocator_ == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const ObCollationType collation = rt_expr.args_[0]->datum_meta_.cs_type_;
    OZ(rt_expr.prepare_cmp_ops(*expr_cg_ctx.allocator_, 2));
    OZ(rt_expr.configure_cmp_op(0, 0, 1, NULL_LAST, collation));
    OZ(rt_expr.configure_cmp_op(1, 2, 0, NULL_LAST, collation));
    if (OB_SUCC(ret)) { rt_expr.eval_func_ = calc_not_between_expr; }
  }
  return ret;
}

}
}
