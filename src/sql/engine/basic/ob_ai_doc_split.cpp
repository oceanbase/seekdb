/**
 * Copyright (c) 2026 OceanBase and/or its affiliates. All rights reserved.
 * MulanPubL v2.
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_ai_doc_split.h"
#include "sql/engine/basic/ob_json_table_op.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace sql
{

bool ObAiDocSplitter::is_ws(char c)
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool ObAiDocSplitter::is_terminator(char c)
{
  return c == '.' || c == '!' || c == '?';
}

bool ObAiDocSplitter::is_heading_line(const common::ObString &content, int64_t start, int64_t end)
{
  bool is_heading = false;
  int64_t i = start;
  int64_t spaces = 0;
  while (i < end && content[i] == ' ' && spaces < 4) {
    ++i;
    ++spaces;
  }
  if (spaces <= 3) {
    int64_t hashes = 0;
    while (i < end && content[i] == '#' && hashes < 7) {
      ++i;
      ++hashes;
    }
    if (hashes >= 1 && hashes <= 6 && (i >= end || is_ws(content[i]))) {
      is_heading = true;
    }
  }
  return is_heading;
}

int ObAiDocSplitter::split_sentences(const common::ObString &content,
                                     int64_t start, int64_t end,
                                     common::ObIArray<Span> &sentences)
{
  INIT_SUCC(ret);
  int64_t s = start;
  int64_t i = start;
  while (OB_SUCC(ret) && i < end) {
    if (is_terminator(content[i]) && (i + 1 >= end || is_ws(content[i + 1]))) {
      int64_t seg_start = s;
      while (seg_start < i && is_ws(content[seg_start])) {
        ++seg_start;
      }
      if (i + 1 > seg_start && OB_FAIL(sentences.push_back(Span(seg_start, i + 1)))) {
        LOG_WARN("failed to push back sentence span", K(ret));
      }
      int64_t j = i + 1;
      while (j < end && is_ws(content[j])) {
        ++j;
      }
      s = j;
      i = j;
    } else {
      ++i;
    }
  }
  // trailing text without a terminator still forms a sentence
  if (OB_SUCC(ret) && s < end) {
    while (s < end && is_ws(content[s])) {
      ++s;
    }
    int64_t e = end;
    while (e > s && is_ws(content[e - 1])) {
      --e;
    }
    if (e > s && OB_FAIL(sentences.push_back(Span(s, e)))) {
      LOG_WARN("failed to push back sentence span", K(ret));
    }
  }
  return ret;
}

int ObAiDocSplitter::build_chunk_text(const common::ObString &content,
                                      int64_t start, int64_t end,
                                      const common::ObString &heading,
                                      common::ObIAllocator &alloc,
                                      ObAiSplitChunk &chunk)
{
  INIT_SUCC(ret);
  int64_t body_len = end - start;
  int64_t heading_len = heading.length();
  int64_t total_len = heading_len + (heading_len > 0 ? 1 : 0) + body_len;
  char *buf = static_cast<char*>(alloc.alloc(total_len));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc chunk text", K(ret), K(total_len));
  } else {
    int64_t pos = 0;
    if (heading_len > 0) {
      MEMCPY(buf, heading.ptr(), heading_len);
      buf[heading_len] = '\n';
      pos = heading_len + 1;
    }
    MEMCPY(buf + pos, content.ptr() + start, body_len);
    chunk.offset_ = start;
    chunk.length_ = total_len;
    chunk.text_.assign_ptr(buf, static_cast<int32_t>(total_len));
  }
  return ret;
}

int ObAiDocSplitter::emit_sentence_chunks(const common::ObString &content,
                                          const common::ObIArray<Span> &sentences,
                                          int64_t max_group,
                                          const common::ObString &heading,
                                          common::ObIAllocator &alloc,
                                          common::ObIArray<ObAiSplitChunk> &chunks)
{
  INIT_SUCC(ret);
  for (int64_t i = 0; OB_SUCC(ret) && i < sentences.count(); i += max_group) {
    int64_t last = (i + max_group - 1 < sentences.count()) ? (i + max_group - 1) : (sentences.count() - 1);
    ObAiSplitChunk chunk;
    if (OB_FAIL(build_chunk_text(content, sentences.at(i).start_, sentences.at(last).end_,
                                 heading, alloc, chunk))) {
      LOG_WARN("failed to build chunk text", K(ret));
    } else if (OB_FAIL(chunks.push_back(chunk))) {
      LOG_WARN("failed to push back chunk", K(ret));
    }
  }
  return ret;
}

int ObAiDocSplitter::emit_word_chunks(const common::ObString &content,
                                      int64_t start, int64_t end,
                                      const ObAiSplitParams &params,
                                      const common::ObString &heading,
                                      common::ObIAllocator &alloc,
                                      common::ObIArray<ObAiSplitChunk> &chunks)
{
  INIT_SUCC(ret);
  common::ObSEArray<Span, 32> words;
  int64_t i = start;
  while (OB_SUCC(ret) && i < end) {
    while (i < end && is_ws(content[i])) {
      ++i;
    }
    int64_t word_start = i;
    while (i < end && !is_ws(content[i])) {
      ++i;
    }
    if (i > word_start && OB_FAIL(words.push_back(Span(word_start, i)))) {
      LOG_WARN("failed to push back word span", K(ret));
    }
  }
  int64_t step = params.max_ - params.overlap_;
  for (int64_t w = 0; OB_SUCC(ret) && w < words.count(); w += step) {
    int64_t last = (w + params.max_ - 1 < words.count()) ? (w + params.max_ - 1) : (words.count() - 1);
    ObAiSplitChunk chunk;
    if (OB_FAIL(build_chunk_text(content, words.at(w).start_, words.at(last).end_,
                                 heading, alloc, chunk))) {
      LOG_WARN("failed to build chunk text", K(ret));
    } else if (OB_FAIL(chunks.push_back(chunk))) {
      LOG_WARN("failed to push back chunk", K(ret));
    }
  }
  return ret;
}

static int apply_string_param(const common::ObString &key, const common::ObString &val,
                              ObAiSplitParams &params)
{
  INIT_SUCC(ret);
  if (0 == key.case_compare("type")) {
    if (0 == val.case_compare("text")) {
      params.type_ = ObAiSplitParams::DOC_TEXT;
    } else if (0 == val.case_compare("markdown")) {
      params.type_ = ObAiSplitParams::DOC_MARKDOWN;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document: type must be 'text' or 'markdown'");
    }
  } else if (0 == key.case_compare("by")) {
    if (0 == val.case_compare("sentence")) {
      params.by_ = ObAiSplitParams::BY_SENTENCE;
    } else if (0 == val.case_compare("word")) {
      params.by_ = ObAiSplitParams::BY_WORD;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document: by must be 'sentence' or 'word'");
    }
  }
  // unknown keys are ignored
  return ret;
}

static int apply_int_param(const common::ObString &key, int64_t val, ObAiSplitParams &params)
{
  INIT_SUCC(ret);
  if (0 == key.case_compare("max")) {
    params.max_ = val;
  } else if (0 == key.case_compare("overlap")) {
    params.overlap_ = val;
  }
  return ret;
}

int ObAiDocSplitter::parse_params(const common::ObString &params_json, ObAiSplitParams &params)
{
  INIT_SUCC(ret);
  const char *str = params_json.ptr();
  int64_t len = params_json.length();
  int64_t i = 0;
  while (i < len && is_ws(str[i])) {
    ++i;
  }
  if (i >= len) {
    // empty parameters: keep defaults
  } else if (str[i] != '{') {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document: parameters must be a json object");
  } else {
    while (OB_SUCC(ret) && i < len && str[i] != '}') {
      // read key
      while (i < len && str[i] != '"' && str[i] != '}') {
        ++i;
      }
      if (i >= len || str[i] == '}') {
        break;
      }
      int64_t key_start = ++i;
      while (i < len && str[i] != '"') {
        ++i;
      }
      if (i >= len) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document: malformed parameters json");
        break;
      }
      common::ObString key(static_cast<int32_t>(i - key_start), str + key_start);
      ++i; // skip closing quote
      while (i < len && str[i] != ':') {
        ++i;
      }
      if (i >= len) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document: malformed parameters json");
        break;
      }
      ++i;
      while (i < len && is_ws(str[i])) {
        ++i;
      }
      // read value
      if (i < len && str[i] == '"') {
        int64_t val_start = ++i;
        while (i < len && str[i] != '"') {
          ++i;
        }
        if (i >= len) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document: malformed parameters json");
          break;
        }
        common::ObString val(static_cast<int32_t>(i - val_start), str + val_start);
        ++i;
        if (OB_FAIL(apply_string_param(key, val, params))) {
          LOG_WARN("invalid ai_split_document parameter", K(ret), K(key), K(val));
        }
      } else {
        int64_t val_start = i;
        if (i < len && (str[i] == '-' || str[i] == '+')) {
          ++i;
        }
        bool is_num = false;
        while (i < len && str[i] >= '0' && str[i] <= '9') {
          ++i;
          is_num = true;
        }
        if (is_num) {
          bool neg = (str[val_start] == '-');
          int64_t v = 0;
          for (int64_t k = val_start + ((str[val_start] == '-' || str[val_start] == '+') ? 1 : 0); k < i; ++k) {
            v = v * 10 + (str[k] - '0');
          }
          if (OB_FAIL(apply_int_param(key, neg ? -v : v, params))) {
            LOG_WARN("invalid ai_split_document parameter", K(ret), K(key));
          }
        }
      }
      // skip to next ',' or '}'
      while (i < len && str[i] != ',' && str[i] != '}') {
        ++i;
      }
      if (i < len && str[i] == ',') {
        ++i;
      }
    }
  }
  return ret;
}

int ObAiDocSplitter::split(const common::ObString &content,
                           const ObAiSplitParams &params,
                           common::ObIAllocator &alloc,
                           ObAiSplitChunks &out)
{
  INIT_SUCC(ret);
  out.count_ = 0;
  out.chunks_ = NULL;
  common::ObSEArray<ObAiSplitChunk, 16> tmp;
  if (params.max_ < 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document: max must be a positive integer");
  } else if (params.by_ == ObAiSplitParams::BY_WORD
             && (params.overlap_ < 0 || params.overlap_ >= params.max_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document: overlap must be in [0, max)");
  } else if (content.length() <= 0) {
    // empty content: no chunks
  } else if (params.type_ == ObAiSplitParams::DOC_MARKDOWN) {
    common::ObString heading;
    int64_t line_start = 0;
    while (OB_SUCC(ret) && line_start < content.length()) {
      int64_t line_end = line_start;
      while (line_end < content.length() && content[line_end] != '\n') {
        ++line_end;
      }
      int64_t trimmed_end = line_end;
      while (trimmed_end > line_start
             && (content[trimmed_end - 1] == '\r' || content[trimmed_end - 1] == ' '
                 || content[trimmed_end - 1] == '\t')) {
        --trimmed_end;
      }
      if (is_heading_line(content, line_start, trimmed_end)) {
        heading.assign_ptr(content.ptr() + line_start, static_cast<int32_t>(trimmed_end - line_start));
      } else {
        bool has_content = false;
        for (int64_t k = line_start; k < trimmed_end; ++k) {
          if (!is_ws(content[k])) {
            has_content = true;
            break;
          }
        }
        if (has_content) {
          if (params.by_ == ObAiSplitParams::BY_WORD) {
            if (OB_FAIL(emit_word_chunks(content, line_start, trimmed_end, params, heading, alloc, tmp))) {
              LOG_WARN("failed to emit word chunks", K(ret));
            }
          } else {
            common::ObSEArray<Span, 16> sentences;
            if (OB_FAIL(split_sentences(content, line_start, trimmed_end, sentences))) {
              LOG_WARN("failed to split sentences", K(ret));
            } else if (OB_FAIL(emit_sentence_chunks(content, sentences, params.max_, heading, alloc, tmp))) {
              LOG_WARN("failed to emit sentence chunks", K(ret));
            }
          }
        }
      }
      line_start = line_end + 1;
    }
  } else {
    if (params.by_ == ObAiSplitParams::BY_WORD) {
      if (OB_FAIL(emit_word_chunks(content, 0, content.length(), params, common::ObString(), alloc, tmp))) {
        LOG_WARN("failed to emit word chunks", K(ret));
      }
    } else {
      common::ObSEArray<Span, 16> sentences;
      if (OB_FAIL(split_sentences(content, 0, content.length(), sentences))) {
        LOG_WARN("failed to split sentences", K(ret));
      } else if (OB_FAIL(emit_sentence_chunks(content, sentences, params.max_, common::ObString(), alloc, tmp))) {
        LOG_WARN("failed to emit sentence chunks", K(ret));
      }
    }
  }
  if (OB_SUCC(ret) && tmp.count() > 0) {
    void *buf = alloc.alloc(tmp.count() * sizeof(ObAiSplitChunk));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc chunk array", K(ret), K(tmp.count()));
    } else {
      out.chunks_ = static_cast<ObAiSplitChunk*>(buf);
      out.count_ = tmp.count();
      for (int64_t i = 0; i < tmp.count(); ++i) {
        out.chunks_[i] = tmp.at(i);
        out.chunks_[i].chunk_id_ = i;
      }
    }
  }
  return ret;
}

int AiSplitTableFunc::init_ctx(ObRegCol &scan_node, JtScanCtx*& ctx)
{
  INIT_SUCC(ret);
  UNUSED(ctx);
  scan_node.tab_type_ = MulModeTableType::OB_AI_SPLIT_TABLE_TYPE;
  return ret;
}

int AiSplitTableFunc::reset_ctx(ObRegCol &scan_node, JtScanCtx*& ctx)
{
  INIT_SUCC(ret);
  UNUSED(scan_node);
  UNUSED(ctx);
  return ret;
}

int AiSplitTableFunc::eval_input(ObJsonTableOp &jt, JtScanCtx &ctx, ObEvalCtx &eval_ctx)
{
  INIT_SUCC(ret);
  if (!ctx.is_ai_split_table_func()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid table func", K(ret));
  } else if (OB_UNLIKELY(ctx.spec_ptr_->value_exprs_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value exprs is empty", K(ret));
  } else {
    jt.reset_columns();
    ObDatum *content_datum = NULL;
    common::ObString content;
    bool is_null_content = false;
    if (OB_FAIL(ctx.spec_ptr_->value_exprs_.at(0)->eval(eval_ctx, content_datum))) {
      LOG_WARN("failed to eval content arg", K(ret));
    } else if (content_datum->is_null()) {
      is_null_content = true;
    } else {
      content = content_datum->get_string();
    }
    ObAiSplitParams params;
    if (OB_FAIL(ret) || is_null_content) {
    } else if (ctx.spec_ptr_->value_exprs_.count() >= 2) {
      ObDatum *params_datum = NULL;
      if (OB_FAIL(ctx.spec_ptr_->value_exprs_.at(1)->eval(eval_ctx, params_datum))) {
        LOG_WARN("failed to eval params arg", K(ret));
      } else if (!params_datum->is_null() && params_datum->get_string().length() > 0) {
        if (OB_FAIL(ObAiDocSplitter::parse_params(params_datum->get_string(), params))) {
          LOG_WARN("failed to parse ai_split_document parameters", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (is_null_content) {
      ret = OB_ITER_END;
    } else {
      void *buf = ctx.row_alloc_.alloc(sizeof(ObAiSplitChunks));
      ObAiSplitChunks *chunks = NULL;
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc chunks container", K(ret));
      } else {
        chunks = new (buf) ObAiSplitChunks();
        if (OB_FAIL(ObAiDocSplitter::split(content, params, ctx.row_alloc_, *chunks))) {
          LOG_WARN("failed to split document", K(ret));
        } else if (0 == chunks->count_) {
          ret = OB_ITER_END;
        } else {
          jt.input_ = chunks;
        }
      }
    }
  }
  return ret;
}

int AiSplitTableFunc::reset_path_iter(ObRegCol &scan_node, void* in, JtScanCtx*& ctx,
                                      ScanType init_flag, bool &is_null_value)
{
  INIT_SUCC(ret);
  UNUSED(init_flag);
  scan_node.iter_ = in;
  if (OB_FAIL(get_iter_value(scan_node, ctx, is_null_value)) && OB_ITER_END != ret) {
    LOG_WARN("failed to get iter value", K(ret));
  }
  return ret;
}

int AiSplitTableFunc::get_iter_value(ObRegCol &col_node, JtScanCtx* ctx, bool &is_null_value)
{
  INIT_SUCC(ret);
  UNUSED(ctx);
  UNUSED(is_null_value);
  col_node.cur_pos_++;
  ObAiSplitChunks *chunks = reinterpret_cast<ObAiSplitChunks*>(col_node.iter_);
  if (OB_ISNULL(chunks) || col_node.cur_pos_ >= chunks->count_) {
    ret = OB_ITER_END;
  }
  return ret;
}

int RegularCol::eval_ai_split_col(ObRegCol &col_node, void* in, JtScanCtx* ctx, ObExpr* col_expr)
{
  INIT_SUCC(ret);
  col_node.cur_pos_++;
  ObAiSplitChunks *chunks = reinterpret_cast<ObAiSplitChunks*>(in);
  ObDatum &res_datum = col_expr->locate_datum_for_write(*ctx->eval_ctx_);
  if (OB_ISNULL(chunks) || col_node.cur_pos_ < 0 || col_node.cur_pos_ >= chunks->count_) {
    res_datum.set_null();
  } else {
    const ObAiSplitChunk &chunk = chunks->chunks_[col_node.cur_pos_];
    switch (col_node.col_info_.id_) {
      case 1:
        res_datum.set_int(chunk.chunk_id_);
        break;
      case 2:
        res_datum.set_int(chunk.offset_);
        break;
      case 3:
        res_datum.set_int(chunk.length_);
        break;
      case 4:
        res_datum.set_string(chunk.text_);
        break;
      default:
        res_datum.set_null();
        break;
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
