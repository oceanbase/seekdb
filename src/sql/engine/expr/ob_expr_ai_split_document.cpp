#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ai_split_document.h"

namespace oceanbase
{
using namespace common;

namespace sql
{

ObExprAiSplitDocument::ObExprAiSplitDocument(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_AI_SPLIT_DOCUMENT, "ai_split_document", MORE_THAN_ZERO,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprAiSplitDocument::~ObExprAiSplitDocument()
{
}

int ObExprAiSplitDocument::calc_result_typeN(ObExprResType &type,
                                             ObExprResType *types,
                                             int64_t param_num,
                                             ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  if (OB_UNLIKELY(param_num < 1 || param_num > 2)) {
    ret = OB_INVALID_ARGUMENT_NUM;
    LOG_WARN("invalid argument count for ai_split_document", K(ret), K(param_num));
  } else if (OB_ISNULL(types)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument types are null", K(ret));
  } else {
    type.set_varchar();
    type.set_collation_type(ObCharset::get_system_collation());
    type.set_collation_level(CS_LEVEL_COERCIBLE);
    type.set_length(OB_MAX_VARCHAR_LENGTH);
    for (int64_t i = 0; i < param_num; ++i) {
      types[i].set_calc_type(ObVarcharType);
      types[i].set_calc_collation_type(ObCharset::get_system_collation());
    }
  }
  return ret;
}

int ObExprAiSplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                  ObEvalCtx &ctx,
                                                  ObDatum &expr_datum)
{
  UNUSED(expr);
  UNUSED(ctx);
  UNUSED(expr_datum);
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("ai_split_document can only be used as a table function", K(ret));
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "ai_split_document as scalar function");
  return ret;
}

int ObExprAiSplitDocument::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                   const ObRawExpr &raw_expr,
                                   ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  const int64_t param_count = raw_expr.get_param_count();
  if (OB_UNLIKELY(param_count < 1 || param_count > 2)) {
    ret = OB_INVALID_ARGUMENT_NUM;
    LOG_WARN("invalid argument count for ai_split_document", K(ret), K(param_count));
  } else {
    rt_expr.eval_func_ = ObExprAiSplitDocument::eval_ai_split_document;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
