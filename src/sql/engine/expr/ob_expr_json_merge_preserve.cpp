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
#include "ob_expr_json_merge_preserve.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "share/ob_json_access_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonMergePreserve::ObExprJsonMergePreserve(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
      T_FUN_SYS_JSON_MERGE_PRESERVE,
      N_JSON_MERGE_PRESERVE, 
      MORE_THAN_ONE,
      VALID_FOR_GENERATED_COL,
      NOT_ROW_DIMENSION)
{
}

ObExprJsonMergePreserve::ObExprJsonMergePreserve(
    ObIAllocator &alloc,
    ObExprOperatorType type,
    const char *name,
    int32_t param_num, 
    int32_t dimension) : ObFuncExprOperator(alloc, type, name, param_num, VALID_FOR_GENERATED_COL, dimension) 
{
}

ObExprJsonMergePreserve::~ObExprJsonMergePreserve()
{
}

int ObExprJsonMergePreserve::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  INIT_SUCC(ret);
  type.set_json();
  type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
  bool is_result_null = false;
  for (int64_t i = 0; OB_SUCC(ret) && !is_result_null && i < param_num; i++) {
    if (types_stack[0].get_type() == ObNullType) {
      is_result_null = true;
    } else if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, i, N_JSON_MERGE_PRESERVE))) {
    }
  }
  return ret;
}

int ObExprJsonMergePreserve::eval_json_merge_preserve(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return ObJsonExprHelper::eval_json_merge(expr, ctx, res, false);
}

int ObExprJsonMergePreserve::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_merge_preserve;
  return OB_SUCCESS;
}

}
}
