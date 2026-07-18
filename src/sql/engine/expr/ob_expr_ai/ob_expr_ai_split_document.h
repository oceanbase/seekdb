/**
 * OceanBase seekdb - Document AI: AI_SPLIT_DOCUMENT table function.
 *
 * Splits text/markdown content into chunk rows. Drives an ObExprOperatorCtx
 * that materializes all chunks on first eval, then returns one chunk per call
 * (OB_ITER_END when exhausted). The ObFunctionTableOp reads 4 column values
 * from the rt_ctx.
 *
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef OCEANBASE_SQL_OB_EXPR_AI_SPLIT_DOCUMENT_H_
#define OCEANBASE_SQL_OB_EXPR_AI_SPLIT_DOCUMENT_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace sql
{

// Runtime context: materializes all chunks on first eval, advances curr_idx_
// per row. ObFunctionTableOp reads curr_chunk_* after each eval.
class ObExprAISplitDocumentCtx : public ObExprOperatorCtx
{
public:
  struct ChunkInfo
  {
    int64_t chunk_id_;
    int64_t chunk_offset_;
    int64_t chunk_length_;
    ObString chunk_text_;   // points into allocator_-owned memory (deep-copied)
    TO_STRING_KV(K_(chunk_id), K_(chunk_offset), K_(chunk_length), K_(chunk_text));
  };
  ObExprAISplitDocumentCtx()
      : curr_idx_(0), initialized_(false),
        curr_chunk_id_(0), curr_chunk_offset_(0), curr_chunk_length_(0) {}
  ~ObExprAISplitDocumentCtx() = default;

  ObArenaAllocator allocator_;
  ObArray<ChunkInfo> chunks_;
  int64_t curr_idx_;
  bool initialized_;
  // current row values (set by eval, read by op)
  int64_t curr_chunk_id_;
  int64_t curr_chunk_offset_;
  int64_t curr_chunk_length_;
  ObString curr_chunk_text_;
};

class ObExprAISplitDocument : public ObFuncExprOperator
{
public:
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
  static int eval_split_document(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprAISplitDocument);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_EXPR_AI_SPLIT_DOCUMENT_H_
