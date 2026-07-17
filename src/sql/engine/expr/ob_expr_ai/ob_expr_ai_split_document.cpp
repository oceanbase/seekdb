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

namespace oceanbase
{
using namespace common;
namespace sql
{

static const char *AI_SPLIT_DOCUMENT_FUNC_NAME = "ai_split_document";

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc,
                       T_FUN_SYS_AI_SPLIT_DOCUMENT,
                       AI_SPLIT_DOCUMENT_FUNC_NAME,
                       ONE_OR_TWO,
                       NOT_VALID_FOR_GENERATED_COL,
                       NOT_ROW_DIMENSION)
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
  if (OB_ISNULL(types) || param_num < 1 || param_num > 2) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ai_split_document arguments", K(ret), K(param_num));
  } else if (!ob_is_string_tc(types[0].get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("ai_split_document content must be text", K(ret), K(types[0].get_type()));
  } else if (2 == param_num
             && !ob_is_string_tc(types[1].get_type())
             && !ob_is_json(types[1].get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("ai_split_document parameters must be JSON text", K(ret), K(types[1].get_type()));
  } else {
    if (2 == param_num) {
      types[1].set_calc_type(ObVarcharType);
      types[1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
    // The function table operator produces the actual four output columns.
    // This scalar result is only a carrier used by the existing function-table plan path.
    type.set_int();
  }
  return ret;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                  ObEvalCtx &ctx,
                                                  ObDatum &res)
{
  UNUSED(expr);
  UNUSED(ctx);
  UNUSED(res);
  int ret = OB_NOT_SUPPORTED;
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "AI_SPLIT_DOCUMENT outside a FROM clause");
  return ret;
}

int ObExprAISplitDocument::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprAISplitDocument::eval_ai_split_document;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
