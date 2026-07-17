/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef OCEANBASE_SQL_ENGINE_EXPR_AI_OB_EXPR_AI_SPLIT_DOCUMENT_H_
#define OCEANBASE_SQL_ENGINE_EXPR_AI_OB_EXPR_AI_SPLIT_DOCUMENT_H_

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprAISplitDocument : public ObFuncExprOperator
{
public:
  struct DocumentChunk
  {
    DocumentChunk() : offset_(0), length_(0), text_() {}
    int64_t offset_;
    int64_t length_;
    common::ObString text_;
    TO_STRING_KV(K_(offset), K_(length), K_(text));
  };

  class SplitDocumentCtx : public ObExprOperatorCtx
  {
  public:
    SplitDocumentCtx();
    virtual ~SplitDocumentCtx() = default;
    void reset();

    common::ObArenaAllocator allocator_;
    common::ObArray<DocumentChunk> chunks_;
    int64_t next_chunk_idx_;
    bool initialized_;
  };

  explicit ObExprAISplitDocument(common::ObIAllocator &alloc);
  virtual ~ObExprAISplitDocument() = default;

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  virtual bool need_rt_ctx() const override { return true; }
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_ai_split_document(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_next_chunk(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             int64_t &chunk_id,
                             int64_t &chunk_offset,
                             int64_t &chunk_length,
                             common::ObString &chunk_text);
  static int reset_context(const ObExpr &expr, ObExecContext &exec_ctx);

private:
  struct SplitConfig
  {
    SplitConfig() : markdown_(true), by_sentence_(true), max_(1), overlap_(0) {}
    bool markdown_;
    bool by_sentence_;
    int64_t max_;
    int64_t overlap_;
  };

  struct Unit
  {
    Unit() : start_(0), end_(0) {}
    Unit(int64_t start, int64_t end) : start_(start), end_(end) {}
    int64_t start_;
    int64_t end_;
    TO_STRING_KV(K_(start), K_(end));
  };

  static int initialize_context(const ObExpr &expr, ObEvalCtx &ctx, SplitDocumentCtx &split_ctx);
  static int parse_config(common::ObIAllocator &allocator,
                          const common::ObString &json,
                          SplitConfig &config);
  static int build_chunks(const common::ObString &content,
                          const SplitConfig &config,
                          SplitDocumentCtx &ctx);
  static int build_markdown_chunks(const common::ObString &content,
                                   const SplitConfig &config,
                                   SplitDocumentCtx &ctx);
  static int build_range_chunks(const common::ObString &content,
                                int64_t range_start,
                                int64_t range_end,
                                const common::ObString &heading,
                                const SplitConfig &config,
                                SplitDocumentCtx &ctx);
  static int tokenize_range(const common::ObString &content,
                            int64_t range_start,
                            int64_t range_end,
                            bool by_sentence,
                            common::ObIArray<Unit> &units);
  static bool is_markdown_heading(const common::ObString &content,
                                  int64_t line_start,
                                  int64_t line_end);
  DISALLOW_COPY_AND_ASSIGN(ObExprAISplitDocument);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_AI_OB_EXPR_AI_SPLIT_DOCUMENT_H_
