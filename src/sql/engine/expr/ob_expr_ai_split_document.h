/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_SPLIT_DOCUMENT_H_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_SPLIT_DOCUMENT_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

// AI_SPLIT_DOCUMENT is evaluated by ObFunctionTableOp.  The expression object
// owns argument type deduction and provides a recognizable runtime type.
class ObExprAISplitDocument : public ObStringExprOperator
{
public:
  explicit ObExprAISplitDocument(common::ObIAllocator &alloc);
  virtual ~ObExprAISplitDocument() = default;

  int calc_result_typeN(ObExprResType &type,
                        ObExprResType *types,
                        int64_t param_num,
                        common::ObExprTypeCtx &type_ctx) const override;
  int cg_expr(ObExprCGCtx &op_cg_ctx,
              const ObRawExpr &raw_expr,
              ObExpr &rt_expr) const override;

  static int eval_ai_split_document(const ObExpr &expr,
                                    ObEvalCtx &ctx,
                                    ObDatum &result);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprAISplitDocument);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_SPLIT_DOCUMENT_H_
