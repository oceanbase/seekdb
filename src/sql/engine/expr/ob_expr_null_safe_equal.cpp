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
#include "sql/engine/expr/ob_expr_null_safe_equal.h"
#include "sql/session/ob_sql_session_info.h"
using namespace oceanbase::sql;
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObExprNullSafeEqual::ObExprNullSafeEqual(ObIAllocator &alloc)
    : ObRelationalExprOperator(alloc, T_OP_NSEQ, N_NS_EQUAL, 2)
{
}

int ObExprNullSafeEqual::calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObRelationalExprOperator::calc_result_type2(type, type1, type2, type_ctx))) {
    LOG_WARN("failed to calc_result_type2", K(ret));
  }
  // always allow NULL value
  type.set_result_flag(NOT_NULL_FLAG);
  return ret;
}

int ObExprNullSafeEqual::calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObRelationalExprOperator::calc_result_typeN(type, types, param_num, type_ctx))) {
    LOG_WARN("failed to calc_result_typeN", K(ret));
  }
  // always allow NULL value
  type.set_result_flag(NOT_NULL_FLAG);
  return ret;
}

int ObExprNullSafeEqual::cg_expr(
    ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  int row_dim = 0;
  CK(NULL != expr_cg_ctx.allocator_);
  CK(2 == rt_expr.arg_cnt_);
  OZ(is_row_cmp(raw_expr, row_dim));
  if (OB_SUCC(ret)) {
    if (row_dim <= 0) { // non row comparison
      void **funcs = static_cast<void **>(expr_cg_ctx.allocator_->alloc(sizeof(void *)));
      if (OB_ISNULL(funcs)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      } else {
        auto &l = rt_expr.args_[0]->datum_meta_;
        auto &r = rt_expr.args_[1]->datum_meta_;
        bool has_lob_header = rt_expr.args_[0]->obj_meta_.has_lob_header() ||
                              rt_expr.args_[1]->obj_meta_.has_lob_header();
        if (ObDatumFuncs::is_string_type(l.type_) && ObDatumFuncs::is_string_type(r.type_)) {
          CK(l.cs_type_ == r.cs_type_);
        }

        if (OB_SUCC(ret)) {
          funcs[0] = (void *)ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
            l.type_, r.type_, l.scale_, r.scale_, l.precision_, r.precision_, false,
            l.cs_type_, has_lob_header);
          CK(NULL != funcs[0]);
          rt_expr.inner_functions_ = funcs;
          rt_expr.inner_func_cnt_ = 1;
          rt_expr.eval_func_ = &ns_equal_eval;
        }
      }
    } else {
      // for row comparison , inner functions is the same with ObRelationalExprOperator::row_cmp
      OZ(cg_row_cmp_expr(row_dim, *expr_cg_ctx.allocator_, raw_expr, rt_expr));
      if (OB_SUCC(ret)) {
        rt_expr.eval_func_ = &row_ns_equal_eval;
      }
    }
  }
  return ret;
}

int ObExprNullSafeEqual::ns_equal(const ObExpr &expr, ObDatum &res,
                                  ObExpr **left, ObEvalCtx &lctx, ObExpr **right, ObEvalCtx &rctx)
{
  int ret = OB_SUCCESS;
  ObDatum *l = NULL;
  ObDatum *r = NULL;
  bool equal = true;
  int cmp_ret = 0;
  for (int64_t i = 0; OB_SUCC(ret) && equal && i < expr.inner_func_cnt_; i++) {
    if (NULL == expr.inner_functions_[i]) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("NULL inner function", K(ret), K(i), K(expr));
    } else if (OB_FAIL(left[i]->eval(lctx, l))
        || OB_FAIL(right[i]->eval(rctx, r))) {
      LOG_WARN("expr evaluate failed", K(ret));
    } else {
      if (l->is_null() && r->is_null()) {
        equal = true;
      } else if (!l->is_null() && !r->is_null()) {
        if (OB_FAIL(reinterpret_cast<DatumCmpFunc>(expr.inner_functions_[i])(*l, *r, cmp_ret))) {
          LOG_WARN("cmp failed", K(ret));
        } else {
          equal = (0 == cmp_ret);
        }
      } else {
        equal = false;
      }
    }
  }
  if (OB_SUCC(ret)) {
    res.set_int(equal);
  }
  return ret;
}

int ObExprNullSafeEqual::ns_equal_eval(
    const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  CK(NULL != expr.inner_functions_ && 1 == expr.inner_func_cnt_);
  if (OB_SUCC(ret)) {
    ret = ns_equal(expr, expr_datum, expr.args_, ctx, expr.args_ + 1, ctx);
  }
  return ret;
}

int ObExprNullSafeEqual::row_ns_equal_eval(
    const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  CK(NULL != expr.inner_functions_
     && 2 == expr.arg_cnt_
     && expr.args_[0]->arg_cnt_ == expr.inner_func_cnt_
     && expr.args_[1]->arg_cnt_ == expr.inner_func_cnt_);
  if (OB_SUCC(ret)) {
    ret = ns_equal(expr, expr_datum, expr.args_[0]->args_, ctx, expr.args_[1]->args_, ctx);
  }
  return ret;
}

}//end of ns sql
}//end of ns oceanbase
