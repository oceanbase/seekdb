/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_SPLIT_DOCUMENT_H_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_AI_SPLIT_DOCUMENT_H_

#include "lib/container/ob_se_array.h"
#include "lib/string/ob_string.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

struct ObAISplitDocumentChunk
{
  ObAISplitDocumentChunk() : offset_(0), length_(0), text_() {}
  ObAISplitDocumentChunk(int64_t offset, int64_t length, const common::ObString &text)
      : offset_(offset), length_(length), text_(text) {}
  int64_t offset_;
  int64_t length_;
  common::ObString text_;
  TO_STRING_KV(K_(offset), K_(length), K_(text));
};

class ObExprAISplitDocument : public ObStringExprOperator
{
public:
  explicit ObExprAISplitDocument(common::ObIAllocator &alloc);
  virtual ~ObExprAISplitDocument();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_ai_split_document(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int build_chunks(common::ObIAllocator &allocator,
                          const common::ObString &content,
                          const common::ObString &params,
                          common::ObIArray<ObAISplitDocumentChunk> &chunks);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprAISplitDocument);
};

}
}

#endif
