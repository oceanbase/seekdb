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
#include "ob_expr_json_func_helper.h"
#include "ob_expr_json_set.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonSet::ObExprJsonSet(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_SET, N_JSON_SET, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonSet::~ObExprJsonSet()
{
}

int ObExprJsonSet::calc_result_typeN(ObExprResType& type,
                                     ObExprResType* types_stack,
                                     int64_t param_num,
                                     ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx); 
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num < 3 || param_num % 2 == 0)) {
    ret = OB_ERR_PARAM_SIZE;
    ObString func_name_(N_JSON_SET);
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, N_JSON_SET))) {
    }

    for (int64_t i = 1; OB_SUCC(ret) && i < param_num; i+=2) {
      if (OB_FAIL(ObJsonExprHelper::is_valid_for_path(types_stack, i))) {
      } else {
        ObJsonExprHelper::set_type_for_value(types_stack, i+1);
      }
    }
    type.set_json();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
  }
  return ret;
}                   

int ObExprJsonSet::eval_json_set(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return ObJsonExprHelper::eval_json_mutation(expr, ctx, res, JsonMutation::Set);
}

int ObExprJsonSet::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                           ObExpr &rt_expr) const
{
  INIT_SUCC(ret);
  if (OB_FAIL(ObJsonExprHelper::init_json_expr_extra_info(expr_cg_ctx.allocator_, raw_expr, type_, rt_expr))) {
  } else {
    rt_expr.eval_func_ = eval_json_set;
  }
  return ret;
}

}
}
