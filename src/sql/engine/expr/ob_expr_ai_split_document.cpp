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

#include "sql/engine/expr/ob_expr_ai_split_document.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_AI_SPLIT_DOCUMENT, N_AI_SPLIT_DOCUMENT,
                       PARAM_NUM_UNKNOWN, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprAISplitDocument::~ObExprAISplitDocument()
{
}

int ObExprAISplitDocument::calc_result_typeN(ObExprResType &type,
                                             ObExprResType *types,
                                             int64_t param_num,
                                             ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (param_num < 1 || param_num > 2) {
    ObString function_name(get_name());
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, function_name.length(), function_name.ptr());
  } else if (!ob_is_string_type(types[0].get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("AI_SPLIT_DOCUMENT content must be text", K(ret), K(types[0]));
  } else {
    types[0].set_calc_type(ObLongTextType);
    types[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (param_num == 2) {
      types[1].set_calc_type(ObVarcharType);
      types[1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
    type.set_varchar();
    type.set_length(OB_MAX_VARCHAR_LENGTH);
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_IMPLICIT);
  }
  return ret;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                   ObEvalCtx &ctx,
                                                   ObDatum &res)
{
  UNUSED(expr);
  UNUSED(ctx);
  res.set_null();
  return OB_NOT_SUPPORTED;
}

int ObExprAISplitDocument::cg_expr(ObExprCGCtx &op_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprAISplitDocument::eval_ai_split_document;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
