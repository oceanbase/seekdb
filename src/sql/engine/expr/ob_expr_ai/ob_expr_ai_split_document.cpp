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

#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_split_document.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_AI_SPLIT_DOCUMENT,
                         N_AI_SPLIT_DOCUMENT,
                         ONE_OR_TWO,
                         NOT_VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

ObExprAISplitDocument::~ObExprAISplitDocument()
{
}

int ObExprAISplitDocument::calc_result_typeN(ObExprResType &type,
                                             ObExprResType *types_array,
                                             int64_t param_num,
                                             ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  if (OB_UNLIKELY(param_num < 1 || param_num > 2)) {
    ObString func_name(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("AI_SPLIT_DOCUMENT expects one or two arguments", K(ret), K(param_num));
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name.length(), func_name.ptr());
  } else {
    type.set_type(ObLongTextType);
    type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_IMPLICIT);

    types_array[0].set_calc_type(ObLongTextType);
    types_array[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (2 == param_num) {
      types_array[1].set_calc_type(ObLongTextType);
      types_array[1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
  }
  return ret;
}

int ObExprAISplitDocument::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (OB_UNLIKELY(rt_expr.arg_cnt_ < 1 || rt_expr.arg_cnt_ > 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("AI_SPLIT_DOCUMENT expects one or two arguments", K(ret), K(rt_expr.arg_cnt_));
  } else {
    rt_expr.eval_func_ = ObExprAISplitDocument::eval_ai_split_document;
  }
  return ret;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                  ObEvalCtx &ctx,
                                                  ObDatum &res)
{
  int ret = OB_NOT_SUPPORTED;
  UNUSED(expr);
  UNUSED(ctx);
  UNUSED(res);
  LOG_WARN("AI_SPLIT_DOCUMENT scalar evaluation is not supported", K(ret));
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "AI_SPLIT_DOCUMENT scalar evaluation");
  return ret;
}

} // namespace sql
} // namespace oceanbase
