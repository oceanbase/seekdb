/*
 * Copyright (c) 2026 OceanBase.
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
#include "sql/engine/expr/index_value_expr.h"

namespace oceanbase { namespace sql {
using namespace common;

IndexValueExpr::IndexValueExpr(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_INDEX_VALUE, "__seekdb_index_value", 2,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{}

int IndexValueExpr::calc_result_type2(ObExprResType &type, ObExprResType &source,
                                    ObExprResType &witness, ObExprTypeCtx &ctx) const
{
  UNUSED(source);
  UNUSED(ctx);
  type = witness;
  return OB_SUCCESS;
}

int IndexValueExpr::eval_index_value(const ObExpr &, ObEvalCtx &, ObDatum &result)
{
  // Do not evaluate either operand: storage owns the expanded values.
  result.set_null();
  return OB_SUCCESS;
}

int IndexValueExpr::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &expr) const
{
  expr.eval_func_ = eval_index_value;
  return OB_SUCCESS;
}
} }
