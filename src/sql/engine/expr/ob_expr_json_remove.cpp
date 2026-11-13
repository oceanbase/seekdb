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
#include "ob_expr_json_remove.h"
#include "ob_expr_json_func_helper.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase
{
namespace sql
{
ObExprJsonRemove::ObExprJsonRemove(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_REMOVE, N_JSON_REMOVE, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonRemove::~ObExprJsonRemove()
{
}

int ObExprJsonRemove::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;

  // json doc
  if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, N_JSON_REMOVE))) {
  }
  // json path
  for (int64_t i = 1; OB_SUCC(ret) && i < param_num; i++) {
    if (OB_FAIL(ObJsonExprHelper::is_valid_for_path(types_stack, i))) {
    }
  }
  type.set_json();
  type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
  return ret;
}

int ObExprJsonRemove::eval_json_remove(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return ObJsonExprHelper::eval_json_mutation(expr, ctx, res, JsonMutation::Remove);
}

int ObExprJsonRemove::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  INIT_SUCC(ret);
  if (OB_FAIL(ObJsonExprHelper::init_json_expr_extra_info(expr_cg_ctx.allocator_, raw_expr, type_, rt_expr))) {
  } else {
    rt_expr.eval_func_ = eval_json_remove;
  }
  return ret;
}

}
}
