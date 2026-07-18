/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_split_document.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

namespace
{
bool contains_literal(const char *p, const int64_t len, const char *needle, const int64_t needle_len)
{
  bool found = false;
  for (int64_t i = 0; !found && NULL != p && i + needle_len <= len; ++i) {
    found = (0 == MEMCMP(p + i, needle, needle_len));
  }
  return found;
}
}

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_AI_SPLIT_DOCUMENT, N_AI_SPLIT_DOCUMENT,
                         MORE_THAN_ZERO, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
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
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid ai_split_document param count", K(ret), K(param_num));
  } else {
    type.set_varchar();
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
    type.set_length(OB_MAX_VARCHAR_LENGTH);
    for (int64_t i = 0; i < param_num; ++i) {
      types[i].set_calc_type(ObVarcharType);
      types[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
  }
  return ret;
}

int ObExprAISplitDocument::parse_params(const ObString &params,
                                        bool &is_markdown,
                                        bool &by_word,
                                        int64_t &max_units,
                                        int64_t &overlap)
{
  int ret = OB_SUCCESS;
  is_markdown = false;
  by_word = false;
  max_units = 1;
  overlap = 0;
  const char *p = params.ptr();
  const int64_t len = params.length();
  if (NULL != p && len > 0) {
    is_markdown = contains_literal(p, len, "\"type\":\"markdown\"", 17);
    by_word = contains_literal(p, len, "\"by\":\"word\"", 11);
    for (int64_t i = 0; i + 6 <= len; ++i) {
      if (0 == MEMCMP(p + i, "\"max\":", 6)) {
        max_units = atoll(p + i + 6);
        break;
      }
    }
    if (max_units <= 0) {
      max_units = 1;
    }
    for (int64_t i = 0; i + 10 <= len; ++i) {
      if (0 == MEMCMP(p + i, "\"overlap\":", 10)) {
        overlap = atoll(p + i + 10);
        break;
      }
    }
    if (overlap < 0) {
      overlap = 0;
    }
  }
  return ret;
}

int ObExprAISplitDocument::add_chunk(ObIAllocator &allocator,
                                     int64_t offset,
                                     int64_t length,
                                     const char *prefix,
                                     int64_t prefix_len,
                                     const ObString &text,
                                     ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  ObAISplitDocumentChunk chunk;
  const int64_t total_len = prefix_len + text.length();
  char *buf = static_cast<char *>(allocator.alloc(total_len));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc chunk text", K(ret), K(total_len));
  } else {
    if (prefix_len > 0) {
      MEMCPY(buf, prefix, prefix_len);
    }
    if (text.length() > 0) {
      MEMCPY(buf + prefix_len, text.ptr(), text.length());
    }
    chunk.chunk_id_ = chunks.count();
    chunk.chunk_offset_ = offset;
    chunk.chunk_length_ = length;
    chunk.chunk_text_.assign_ptr(buf, static_cast<int32_t>(total_len));
    if (OB_FAIL(chunks.push_back(chunk))) {
      LOG_WARN("failed to push split chunk", K(ret));
    }
  }
  return ret;
}

int ObExprAISplitDocument::split_sentences(ObIAllocator &allocator,
                                           const ObString &content,
                                           const char *prefix,
                                           int64_t prefix_len,
                                           ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  const char *p = content.ptr();
  int64_t start = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < content.length(); ++i) {
    if ('.' == p[i] || '!' == p[i] || '?' == p[i]) {
      const int64_t len = i - start + 1;
      if (len > 0) {
        ObString text(static_cast<int32_t>(len), p + start);
        if (OB_FAIL(add_chunk(allocator, start, len, prefix, prefix_len, text, chunks))) {
          LOG_WARN("failed to add sentence chunk", K(ret));
        }
      }
      start = i + 1;
      while (start < content.length() && (' ' == p[start] || '\n' == p[start] || '\t' == p[start])) {
        ++start;
      }
      i = start - 1;
    }
  }
  if (OB_SUCC(ret) && start < content.length()) {
    ObString text(static_cast<int32_t>(content.length() - start), p + start);
    OZ(add_chunk(allocator, start, content.length() - start, prefix, prefix_len, text, chunks));
  }
  return ret;
}

int ObExprAISplitDocument::split_words(ObIAllocator &allocator,
                                       const ObString &content,
                                       int64_t max_words,
                                       int64_t overlap,
                                       ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  struct WordSpan {
    int64_t off_;
    int64_t len_;
    TO_STRING_KV(K_(off), K_(len));
  };
  ObSEArray<WordSpan, 32> words;
  const char *p = content.ptr();
  int64_t i = 0;
  while (OB_SUCC(ret) && i < content.length()) {
    while (i < content.length() && (' ' == p[i] || '\n' == p[i] || '\t' == p[i])) {
      ++i;
    }
    const int64_t start = i;
    while (i < content.length() && ' ' != p[i] && '\n' != p[i] && '\t' != p[i]) {
      ++i;
    }
    if (i > start) {
      WordSpan span = {start, i - start};
      OZ(words.push_back(span));
    }
  }
  const int64_t step = MAX(1, max_words - overlap);
  for (int64_t w = 0; OB_SUCC(ret) && w < words.count(); w += step) {
    const int64_t end_w = MIN(words.count(), w + max_words);
    const int64_t start_off = words.at(w).off_;
    const int64_t end_off = words.at(end_w - 1).off_ + words.at(end_w - 1).len_;
    ObString text(static_cast<int32_t>(end_off - start_off), p + start_off);
    OZ(add_chunk(allocator, start_off, end_off - start_off, NULL, 0, text, chunks));
    if (end_w >= words.count()) {
      break;
    }
  }
  return ret;
}

int ObExprAISplitDocument::split_markdown(ObIAllocator &allocator,
                                          const ObString &content,
                                          ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  const char *p = content.ptr();
  int64_t line_start = 0;
  ObString current_heading;
  while (OB_SUCC(ret) && line_start < content.length()) {
    int64_t line_end = line_start;
    while (line_end < content.length() && p[line_end] != '\n') {
      ++line_end;
    }
    ObString line(static_cast<int32_t>(line_end - line_start), p + line_start);
    if (line.length() > 0 && line.ptr()[0] == '#') {
      current_heading = line;
    } else if (line.length() > 0) {
      char prefix_buf[1024];
      int64_t prefix_len = 0;
      if (!current_heading.empty()) {
        prefix_len = snprintf(prefix_buf, sizeof(prefix_buf), "%.*s\n",
                              current_heading.length(), current_heading.ptr());
        if (prefix_len < 0 || prefix_len >= static_cast<int64_t>(sizeof(prefix_buf))) {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("markdown heading is too long", K(ret), K(current_heading.length()));
        }
      }
      if (OB_SUCC(ret)) {
        OZ(split_sentences(allocator, line, prefix_len > 0 ? prefix_buf : NULL, prefix_len, chunks));
      }
    }
    line_start = line_end + 1;
  }
  return ret;
}

int ObExprAISplitDocument::init_chunks(const ObExpr &expr,
                                       ObEvalCtx &ctx,
                                       ObAISplitDocumentCtx &split_ctx)
{
  int ret = OB_SUCCESS;
  ObDatum *content_datum = NULL;
  ObDatum *params_datum = NULL;
  bool is_markdown = false;
  bool by_word = false;
  int64_t max_units = 1;
  int64_t overlap = 0;
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 2)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid ai_split_document arg count", K(ret), K(expr.arg_cnt_));
  } else if (1 == expr.arg_cnt_ && OB_FAIL(expr.eval_param_value(ctx, content_datum))) {
    LOG_WARN("failed to eval ai_split_document content", K(ret));
  } else if (2 == expr.arg_cnt_ && OB_FAIL(expr.eval_param_value(ctx, content_datum, params_datum))) {
    LOG_WARN("failed to eval ai_split_document args", K(ret));
  } else if (content_datum->is_null()) {
    // no rows
  } else {
    ObString content = content_datum->get_string();
    ObString params = (NULL == params_datum || params_datum->is_null()) ? ObString() : params_datum->get_string();
    OZ(parse_params(params, is_markdown, by_word, max_units, overlap));
    if (OB_FAIL(ret)) {
    } else if (is_markdown) {
      OZ(split_markdown(ctx.exec_ctx_.get_allocator(), content, split_ctx.chunks_));
    } else if (by_word) {
      OZ(split_words(ctx.exec_ctx_.get_allocator(), content, max_units, overlap, split_ctx.chunks_));
    } else {
      OZ(split_sentences(ctx.exec_ctx_.get_allocator(), content, NULL, 0, split_ctx.chunks_));
    }
  }
  split_ctx.inited_ = true;
  split_ctx.next_idx_ = 0;
  return ret;
}

int ObExprAISplitDocument::eval_next_chunk(const ObExpr &expr,
                                           ObEvalCtx &ctx,
                                           ObAISplitDocumentChunk &chunk)
{
  int ret = OB_SUCCESS;
  ObAISplitDocumentCtx *split_ctx = NULL;
  const uint64_t op_id = expr.expr_ctx_id_;
  if (OB_ISNULL(split_ctx = static_cast<ObAISplitDocumentCtx *>(ctx.exec_ctx_.get_expr_op_ctx(op_id)))) {
    if (OB_FAIL(ctx.exec_ctx_.create_expr_op_ctx(op_id, split_ctx))) {
      LOG_WARN("failed to create ai split document ctx", K(ret), K(op_id));
    }
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(split_ctx) && !split_ctx->inited_) {
    OZ(init_chunks(expr, ctx, *split_ctx));
  }
  if (OB_SUCC(ret)) {
    if (split_ctx->next_idx_ >= split_ctx->chunks_.count()) {
      ret = OB_ITER_END;
    } else {
      chunk = split_ctx->chunks_.at(split_ctx->next_idx_++);
    }
  }
  return ret;
}

int ObExprAISplitDocument::reset_ctx(const ObExpr &expr, ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObAISplitDocumentCtx *split_ctx = NULL;
  if (OB_NOT_NULL(split_ctx = static_cast<ObAISplitDocumentCtx *>(
                      ctx.exec_ctx_.get_expr_op_ctx(expr.expr_ctx_id_)))) {
    split_ctx->reset();
  }
  return ret;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObAISplitDocumentChunk chunk;
  if (OB_FAIL(eval_next_chunk(expr, ctx, chunk))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("failed to eval next ai_split_document chunk", K(ret));
    }
  } else {
    res.set_string(chunk.chunk_text_);
  }
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
