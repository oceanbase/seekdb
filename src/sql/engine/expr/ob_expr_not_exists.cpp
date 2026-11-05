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
#include "sql/engine/expr/ob_expr_not_exists.h"
namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprNotExists::ObExprNotExists(ObIAllocator &alloc)
  : ObSubQueryRelationalExpr(alloc, T_OP_NOT_EXISTS, N_NOT_EXISTS, 1, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprNotExists::~ObExprNotExists()
{
}

int ObExprNotExists::calc_result_type1(ObExprResType &type,
                                       ObExprResType &type1,
                                       ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!type1.is_int())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else {
    type.set_int32();
    type.set_result_flag(NOT_NULL_FLAG);
    type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
    type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
  }
  return ret;
}

int ObExprNotExists::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  CK(1 == rt_expr.arg_cnt_);
  rt_expr.eval_func_ = &not_exists_eval;
  return ret;
};

int ObExprNotExists::not_exists_eval(
    const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  if (OB_FAIL(check_exists(expr, ctx, exists))) {
    LOG_WARN("check exists failed", K(ret));
  } else {
    expr_datum.set_bool(!exists);
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
