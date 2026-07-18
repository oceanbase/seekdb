/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ai_split_document.h"

#include "lib/charset/ob_charset.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &alloc)
    : ObStringExprOperator(alloc,
                           T_FUN_SYS_AI_SPLIT_DOCUMENT,
                           N_AI_SPLIT_DOCUMENT,
                           ONE_OR_TWO,
                           NOT_VALID_FOR_GENERATED_COL)
{
}

int ObExprAISplitDocument::calc_result_typeN(ObExprResType &type,
                                             ObExprResType *types,
                                             int64_t param_num,
                                             ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num < 1 || param_num > 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid ai_split_document argument count", K(ret), K(param_num));
  } else {
    type.set_varchar();
    type.set_length(OB_MAX_LONGTEXT_LENGTH);
    type.set_collation_type(types[0].get_collation_type());
    type.set_collation_level(CS_LEVEL_COERCIBLE);
    types[0].set_calc_type(ObVarcharType);
    if (param_num == 2) {
      types[1].set_calc_type(ObVarcharType);
      types[1].set_calc_collation_type(ObCharset::get_system_collation());
    }
  }
  return ret;
}

int ObExprAISplitDocument::cg_expr(ObExprCGCtx &op_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  // The function-table operator evaluates the arguments and materializes rows.
  rt_expr.eval_func_ = ObExprAISplitDocument::eval_ai_split_document;
  return OB_SUCCESS;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                   ObEvalCtx &ctx,
                                                   ObDatum &result)
{
  UNUSED(expr);
  UNUSED(ctx);
  // Function-table execution consumes the child arguments directly.  A valid
  // scalar callback is still required by static expression code generation.
  result.set_null();
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
