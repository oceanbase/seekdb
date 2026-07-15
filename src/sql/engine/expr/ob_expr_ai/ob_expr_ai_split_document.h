/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_SQL_OB_EXPR_AI_SPLIT_DOCUMENT_H_
#define OCEANBASE_SQL_OB_EXPR_AI_SPLIT_DOCUMENT_H_

#include "lib/container/ob_se_array.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

struct ObAISplitDocumentChunk
{
  ObAISplitDocumentChunk() : chunk_id_(0), chunk_offset_(0), chunk_length_(0), chunk_text_() {}
  int64_t chunk_id_;
  int64_t chunk_offset_;
  int64_t chunk_length_;
  common::ObString chunk_text_;
};

class ObExprAISplitDocument : public ObFuncExprOperator
{
public:
  class ObAISplitDocumentCtx : public ObExprOperatorCtx
  {
  public:
    ObAISplitDocumentCtx() : inited_(false), next_idx_(0), chunks_() {}
    virtual ~ObAISplitDocumentCtx() = default;
    void reset()
    {
      inited_ = false;
      next_idx_ = 0;
      chunks_.reset();
    }
    bool inited_;
    int64_t next_idx_;
    common::ObSEArray<ObAISplitDocumentChunk, 16> chunks_;
  };

  explicit ObExprAISplitDocument(common::ObIAllocator &alloc);
  virtual ~ObExprAISplitDocument();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_ai_split_document(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_next_chunk(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             ObAISplitDocumentChunk &chunk);
  static int reset_ctx(const ObExpr &expr, ObEvalCtx &ctx);

private:
  static int init_chunks(const ObExpr &expr,
                         ObEvalCtx &ctx,
                         ObAISplitDocumentCtx &split_ctx);
  static int parse_params(const common::ObString &params,
                          bool &is_markdown,
                          bool &by_word,
                          int64_t &max_units,
                          int64_t &overlap);
  static int split_sentences(common::ObIAllocator &allocator,
                             const common::ObString &content,
                             const char *prefix,
                             int64_t prefix_len,
                             common::ObIArray<ObAISplitDocumentChunk> &chunks);
  static int split_words(common::ObIAllocator &allocator,
                         const common::ObString &content,
                         int64_t max_words,
                         int64_t overlap,
                         common::ObIArray<ObAISplitDocumentChunk> &chunks);
  static int split_markdown(common::ObIAllocator &allocator,
                            const common::ObString &content,
                            common::ObIArray<ObAISplitDocumentChunk> &chunks);
  static int add_chunk(common::ObIAllocator &allocator,
                       int64_t offset,
                       int64_t length,
                       const char *prefix,
                       int64_t prefix_len,
                       const common::ObString &text,
                       common::ObIArray<ObAISplitDocumentChunk> &chunks);
  DISALLOW_COPY_AND_ASSIGN(ObExprAISplitDocument);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_EXPR_AI_SPLIT_DOCUMENT_H_
