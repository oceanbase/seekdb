/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_split_document.h"

#include <cctype>
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_common.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprAISplitDocument::SplitDocumentCtx::SplitDocumentCtx()
  : allocator_(ObMemAttr("AISplitDoc")),
    chunks_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator_)),
    next_chunk_idx_(0),
    initialized_(false)
{
}

void ObExprAISplitDocument::SplitDocumentCtx::reset()
{
  chunks_.reset();
  allocator_.reuse();
  next_chunk_idx_ = 0;
  initialized_ = false;
}

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc,
                       T_FUN_SYS_AI_SPLIT_DOCUMENT,
                       N_AI_SPLIT_DOCUMENT,
                       ONE_OR_TWO,
                       NOT_VALID_FOR_GENERATED_COL,
                       NOT_ROW_DIMENSION)
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
  } else if ((!ob_is_string_tc(types[0].get_type()) && !types[0].is_null())
             || (param_num == 2 && !ob_is_string_tc(types[1].get_type()) && !types[1].is_null())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_OP, N_AI_SPLIT_DOCUMENT);
  } else {
    for (int64_t i = 0; i < param_num; ++i) {
      if (!types[i].is_null() && types[i].get_charset_type() != CHARSET_UTF8MB4) {
        types[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    }
    // The public SQL surface is a table function.  The scalar result type is
    // only a carrier used by the existing FUNCTION_TABLE optimizer pipeline.
    type.set_varchar();
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
    type.set_length(OB_MAX_MYSQL_VARCHAR_LENGTH);
  }
  return ret;
}

int ObExprAISplitDocument::parse_config(ObIAllocator &allocator,
                                        const ObString &json,
                                        SplitConfig &config)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *root = nullptr;
  if (json.empty()) {
    // defaults
  } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator,
                                                       json,
                                                       ObJsonInType::JSON_TREE,
                                                       ObJsonInType::JSON_TREE,
                                                       root))) {
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document parameters must be valid JSON");
  } else if (OB_ISNULL(root) || root->json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document parameters must be a JSON object");
  } else {
    for (uint64_t i = 0; OB_SUCC(ret) && i < root->element_count(); ++i) {
      ObString key;
      ObIJsonBase *value = nullptr;
      if (OB_FAIL(root->get_object_value(i, key, value)) || OB_ISNULL(value)) {
        LOG_WARN("failed to read ai_split_document parameter", K(ret), K(key));
      } else if (0 == key.case_compare("type")) {
        if (value->json_type() != ObJsonNodeType::J_STRING) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const ObString val(value->get_data_length(), value->get_data());
          if (0 == val.case_compare("markdown")) {
            config.markdown_ = true;
          } else if (0 == val.case_compare("text")) {
            config.markdown_ = false;
          } else {
            ret = OB_INVALID_ARGUMENT;
          }
        }
      } else if (0 == key.case_compare("by")) {
        if (value->json_type() != ObJsonNodeType::J_STRING) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const ObString val(value->get_data_length(), value->get_data());
          if (0 == val.case_compare("sentence")) {
            config.by_sentence_ = true;
          } else if (0 == val.case_compare("word")) {
            config.by_sentence_ = false;
          } else {
            ret = OB_INVALID_ARGUMENT;
          }
        }
      } else if (0 == key.case_compare("max")) {
        if (value->json_type() != ObJsonNodeType::J_INT) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          config.max_ = value->get_int();
        }
      } else if (0 == key.case_compare("overlap")) {
        if (value->json_type() != ObJsonNodeType::J_INT) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          config.overlap_ = value->get_int();
        }
      } else {
        ret = OB_INVALID_ARGUMENT;
      }
    }
  }
  if (OB_SUCC(ret) && (config.max_ <= 0 || config.overlap_ < 0 || config.overlap_ >= config.max_)) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (OB_FAIL(ret)) {
    LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                   "ai_split_document supports type=text|markdown, by=sentence|word, max>0 and 0<=overlap<max");
  }
  return ret;
}

static bool is_ascii_space(const char ch)
{
  return 0 != std::isspace(static_cast<unsigned char>(ch));
}

static bool is_sentence_terminator(const ObString &content, int64_t pos, int64_t end, int64_t &width)
{
  bool terminator = false;
  width = 1;
  const unsigned char ch = static_cast<unsigned char>(content.ptr()[pos]);
  if (ch == '.' || ch == '!' || ch == '?') {
    terminator = true;
  } else if (pos + 2 < end) {
    const unsigned char c1 = static_cast<unsigned char>(content.ptr()[pos + 1]);
    const unsigned char c2 = static_cast<unsigned char>(content.ptr()[pos + 2]);
    if ((ch == 0xE3 && c1 == 0x80 && c2 == 0x82)       // 。
        || (ch == 0xEF && c1 == 0xBC && c2 == 0x81)    // ！
        || (ch == 0xEF && c1 == 0xBC && c2 == 0x9F)) { // ？
      terminator = true;
      width = 3;
    }
  }
  return terminator;
}

int ObExprAISplitDocument::tokenize_range(const ObString &content,
                                           int64_t range_start,
                                           int64_t range_end,
                                           bool by_sentence,
                                           ObIArray<Unit> &units)
{
  int ret = OB_SUCCESS;
  int64_t pos = range_start;
  while (OB_SUCC(ret) && pos < range_end) {
    while (pos < range_end && is_ascii_space(content.ptr()[pos])) {
      ++pos;
    }
    if (pos >= range_end) {
      break;
    }
    const int64_t start = pos;
    if (by_sentence) {
      bool found_end = false;
      while (pos < range_end && !found_end) {
        int64_t width = 1;
        if (is_sentence_terminator(content, pos, range_end, width)) {
          pos += width;
          found_end = true;
        } else {
          ++pos;
        }
      }
      while (pos > start && is_ascii_space(content.ptr()[pos - 1])) {
        --pos;
      }
    } else {
      while (pos < range_end && !is_ascii_space(content.ptr()[pos])) {
        ++pos;
      }
    }
    if (pos > start && OB_FAIL(units.push_back(Unit(start, pos)))) {
      LOG_WARN("failed to append document split unit", K(ret));
    }
  }
  return ret;
}

int ObExprAISplitDocument::build_range_chunks(const ObString &content,
                                               int64_t range_start,
                                               int64_t range_end,
                                               const ObString &heading,
                                               const SplitConfig &config,
                                               SplitDocumentCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObArray<Unit> units(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(ctx.allocator_));
  if (range_start < range_end
      && OB_FAIL(tokenize_range(content, range_start, range_end, config.by_sentence_, units))) {
    LOG_WARN("failed to tokenize document range", K(ret), K(range_start), K(range_end));
  }
  const int64_t step = config.max_ - config.overlap_;
  for (int64_t first = 0; OB_SUCC(ret) && first < units.count(); first += step) {
    const int64_t last = MIN(first + config.max_, units.count()) - 1;
    DocumentChunk chunk;
    chunk.offset_ = units.at(first).start_;
    chunk.length_ = units.at(last).end_ - chunk.offset_;
    const ObString body(chunk.length_, content.ptr() + chunk.offset_);
    if (heading.empty()) {
      chunk.text_ = body;
    } else {
      const int64_t text_len = heading.length() + 1 + body.length();
      char *text_buf = static_cast<char *>(ctx.allocator_.alloc(text_len));
      if (OB_ISNULL(text_buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate markdown chunk text", K(ret), K(text_len));
      } else {
        MEMCPY(text_buf, heading.ptr(), heading.length());
        text_buf[heading.length()] = '\n';
        MEMCPY(text_buf + heading.length() + 1, body.ptr(), body.length());
        chunk.text_.assign_ptr(text_buf, static_cast<ObString::obstr_size_t>(text_len));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ctx.chunks_.push_back(chunk))) {
      LOG_WARN("failed to append document chunk", K(ret));
    }
  }
  return ret;
}

bool ObExprAISplitDocument::is_markdown_heading(const ObString &content,
                                                 int64_t line_start,
                                                 int64_t line_end)
{
  int64_t hashes = 0;
  while (line_start + hashes < line_end
         && hashes < 6
         && content.ptr()[line_start + hashes] == '#') {
    ++hashes;
  }
  return hashes > 0
      && (line_start + hashes == line_end || content.ptr()[line_start + hashes] == ' ');
}

int ObExprAISplitDocument::build_markdown_chunks(const ObString &content,
                                                  const SplitConfig &config,
                                                  SplitDocumentCtx &ctx)
{
  int ret = OB_SUCCESS;
  int64_t line_start = 0;
  int64_t body_start = 0;
  bool found_heading = false;
  ObString heading;
  while (OB_SUCC(ret) && line_start < content.length()) {
    int64_t line_end = line_start;
    while (line_end < content.length() && content.ptr()[line_end] != '\n') {
      ++line_end;
    }
    int64_t visible_end = line_end;
    if (visible_end > line_start && content.ptr()[visible_end - 1] == '\r') {
      --visible_end;
    }
    if (is_markdown_heading(content, line_start, visible_end)) {
      if (found_heading
          && OB_FAIL(build_range_chunks(content, body_start, line_start, heading, config, ctx))) {
        LOG_WARN("failed to split markdown section", K(ret));
      } else if (!found_heading && line_start > 0
                 && OB_FAIL(build_range_chunks(content, 0, line_start, ObString(), config, ctx))) {
        LOG_WARN("failed to split markdown preamble", K(ret));
      } else {
        found_heading = true;
        heading.assign_ptr(content.ptr() + line_start,
                           static_cast<ObString::obstr_size_t>(visible_end - line_start));
        body_start = line_end < content.length() ? line_end + 1 : line_end;
      }
    }
    line_start = line_end < content.length() ? line_end + 1 : line_end;
  }
  if (OB_FAIL(ret)) {
  } else if (found_heading) {
    ret = build_range_chunks(content, body_start, content.length(), heading, config, ctx);
  } else {
    ret = build_range_chunks(content, 0, content.length(), ObString(), config, ctx);
  }
  return ret;
}

int ObExprAISplitDocument::build_chunks(const ObString &content,
                                         const SplitConfig &config,
                                         SplitDocumentCtx &ctx)
{
  return config.markdown_
      ? build_markdown_chunks(content, config, ctx)
      : build_range_chunks(content, 0, content.length(), ObString(), config, ctx);
}

int ObExprAISplitDocument::initialize_context(const ObExpr &expr,
                                               ObEvalCtx &ctx,
                                               SplitDocumentCtx &split_ctx)
{
  int ret = OB_SUCCESS;
  ObDatum *content_datum = nullptr;
  ObDatum *params_datum = nullptr;
  ObString content;
  ObString owned_content;
  ObString params;
  SplitConfig config;
  split_ctx.reset();
  if (OB_UNLIKELY(expr.arg_cnt_ < 1 || expr.arg_cnt_ > 2)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected ai_split_document argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, content_datum))) {
    LOG_WARN("failed to evaluate document content", K(ret));
  } else if (content_datum->is_null()) {
    // NULL input produces an empty table.
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(split_ctx.allocator_,
                                                                *content_datum,
                                                                expr.args_[0]->datum_meta_,
                                                                expr.args_[0]->obj_meta_.has_lob_header(),
                                                                content))) {
    LOG_WARN("failed to read document content", K(ret));
  } else if (OB_FAIL(ob_write_string(split_ctx.allocator_, content, owned_content))) {
    LOG_WARN("failed to retain document content", K(ret), K(content.length()));
  } else if (expr.arg_cnt_ == 2 && OB_FAIL(expr.args_[1]->eval(ctx, params_datum))) {
    LOG_WARN("failed to evaluate document split parameters", K(ret));
  } else if (expr.arg_cnt_ == 2 && !params_datum->is_null()
             && OB_FAIL(ObTextStringHelper::read_real_string_data(split_ctx.allocator_,
                                                                   *params_datum,
                                                                   expr.args_[1]->datum_meta_,
                                                                   expr.args_[1]->obj_meta_.has_lob_header(),
                                                                   params))) {
    LOG_WARN("failed to read document split parameters", K(ret));
  } else if (OB_FAIL(parse_config(split_ctx.allocator_, params, config))) {
    LOG_WARN("failed to parse document split parameters", K(ret));
  } else if (!content_datum->is_null() && OB_FAIL(build_chunks(owned_content, config, split_ctx))) {
    LOG_WARN("failed to split document", K(ret));
  }
  if (OB_SUCC(ret)) {
    split_ctx.initialized_ = true;
  }
  return ret;
}

int ObExprAISplitDocument::eval_next_chunk(const ObExpr &expr,
                                            ObEvalCtx &ctx,
                                            int64_t &chunk_id,
                                            int64_t &chunk_offset,
                                            int64_t &chunk_length,
                                            ObString &chunk_text)
{
  int ret = OB_SUCCESS;
  SplitDocumentCtx *split_ctx = static_cast<SplitDocumentCtx *>(
      ctx.exec_ctx_.get_expr_op_ctx(expr.expr_ctx_id_));
  if (OB_ISNULL(split_ctx)) {
    if (OB_FAIL(ctx.exec_ctx_.create_expr_op_ctx(expr.expr_ctx_id_, split_ctx))) {
      LOG_WARN("failed to create ai_split_document context", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!split_ctx->initialized_ && OB_FAIL(initialize_context(expr, ctx, *split_ctx))) {
    LOG_WARN("failed to initialize ai_split_document context", K(ret));
  } else if (split_ctx->next_chunk_idx_ >= split_ctx->chunks_.count()) {
    ret = OB_ITER_END;
  } else {
    chunk_id = split_ctx->next_chunk_idx_;
    const DocumentChunk &chunk = split_ctx->chunks_.at(split_ctx->next_chunk_idx_++);
    chunk_offset = chunk.offset_;
    chunk_length = chunk.length_;
    chunk_text = chunk.text_;
  }
  return ret;
}

int ObExprAISplitDocument::reset_context(const ObExpr &expr, ObExecContext &exec_ctx)
{
  SplitDocumentCtx *split_ctx = static_cast<SplitDocumentCtx *>(
      exec_ctx.get_expr_op_ctx(expr.expr_ctx_id_));
  if (OB_NOT_NULL(split_ctx)) {
    split_ctx->reset();
  }
  return OB_SUCCESS;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                   ObEvalCtx &ctx,
                                                   ObDatum &res)
{
  int64_t chunk_id = 0;
  int64_t chunk_offset = 0;
  int64_t chunk_length = 0;
  ObString chunk_text;
  int ret = eval_next_chunk(expr, ctx, chunk_id, chunk_offset, chunk_length, chunk_text);
  if (OB_SUCC(ret)) {
    res.set_string(chunk_text);
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
