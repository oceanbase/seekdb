/**
 * Copyright (c) 2025 OceanBase
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OCEANBASE_SQL_OB_EXPR_AI_PARSE_DOC_H_
#define OCEANBASE_SQL_OB_EXPR_AI_PARSE_DOC_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "ob_ai_func_client.h"
#include "ob_ai_func.h"
#include "ob_ai_func_utils.h"

namespace oceanbase
{
namespace sql
{
// ai_parse_doc(model_name varchar, context blob, [params] json) -> longtext
// Parses a document via a vision/OCR model. Phase 1 supports input_type='url'
// (context = an image URL); input_type='pdf' (the default) returns OB_NOT_SUPPORTED
// until the pdfium PDF->image render pipeline lands (Phase 2).
class ObExprAIParseDoc : public ObFuncExprOperator
{
public:
  explicit ObExprAIParseDoc(common::ObIAllocator &alloc);
  virtual ~ObExprAIParseDoc();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_array,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_ai_parse_doc(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  virtual bool need_rt_ctx() const override { return true; }
private:
  // read input_type / output_format / prompt out of the optional params json
  static int parse_params(common::ObIAllocator &allocator,
                          common::ObJsonObject *params_obj,
                          common::ObString &input_type,
                          common::ObString &output_format,
                          common::ObString &prompt);
  // vendor fixed prompt when the user supplies none
  static int get_default_prompt(const common::ObString &request_model_name,
                                const common::ObString &output_format,
                                common::ObString &prompt);
  static constexpr int MODEL_IDX = 0;
  static constexpr int CONTEXT_IDX = 1;
  static constexpr int PARAMS_IDX = 2;
  DISALLOW_COPY_AND_ASSIGN(ObExprAIParseDoc);
};

} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_AI_PARSE_DOC_H_
