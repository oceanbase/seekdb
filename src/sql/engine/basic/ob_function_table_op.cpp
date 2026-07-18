/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_function_table_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include <string.h>


namespace oceanbase
{
using namespace common;
namespace sql
{


namespace
{
struct ObAiSplitOptions
{
  ObAiSplitOptions()
      : is_markdown_(true), by_word_(true), max_(256), overlap_(0)
  {}
  bool is_markdown_;
  bool by_word_;
  int64_t max_;
  int64_t overlap_;
};

struct ObAiSplitSpan
{
  int64_t start_;
  int64_t end_;
  TO_STRING_KV(K_(start), K_(end));
};

static bool ai_split_is_space(const char c)
{
  return ' ' == c || '\t' == c || '\n' == c || '\r' == c;
}

static bool ai_split_is_digit(const char c)
{
  return c >= '0' && c <= '9';
}

static bool ai_split_equal_literal(const ObString &value, const char *literal)
{
  const int64_t literal_len = static_cast<int64_t>(strlen(literal));
  return value.length() == literal_len
         && (0 == literal_len || 0 == MEMCMP(value.ptr(), literal, literal_len));
}

static int ai_split_find_json_string(const ObString &json,
                                     const char *key,
                                     ObString &value,
                                     bool &found)
{
  int ret = OB_SUCCESS;
  found = false;
  value.reset();
  const char *data = json.ptr();
  const int64_t len = json.length();
  const int64_t key_len = static_cast<int64_t>(strlen(key));
  for (int64_t i = 0; OB_SUCC(ret) && !found && OB_NOT_NULL(data) && i + key_len + 2 <= len; ++i) {
    if ('"' == data[i]
        && 0 == MEMCMP(data + i + 1, key, key_len)
        && '"' == data[i + key_len + 1]) {
      int64_t pos = i + key_len + 2;
      while (pos < len && ai_split_is_space(data[pos])) {
        ++pos;
      }
      if (pos >= len || ':' != data[pos]) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid ai_split_document json parameter", K(ret), K(json), K(key));
      } else {
        ++pos;
        while (pos < len && ai_split_is_space(data[pos])) {
          ++pos;
        }
        if (pos >= len || '"' != data[pos]) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid ai_split_document string parameter", K(ret), K(json), K(key));
        } else {
          const int64_t start = ++pos;
          while (pos < len && '"' != data[pos]) {
            ++pos;
          }
          if (pos >= len) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("unterminated ai_split_document string parameter", K(ret), K(json), K(key));
          } else {
            value.assign_ptr(data + start, static_cast<ObString::obstr_size_t>(pos - start));
            found = true;
          }
        }
      }
    }
  }
  return ret;
}

static int ai_split_find_json_int(const ObString &json,
                                  const char *key,
                                  int64_t &value,
                                  bool &found)
{
  int ret = OB_SUCCESS;
  found = false;
  value = 0;
  const char *data = json.ptr();
  const int64_t len = json.length();
  const int64_t key_len = static_cast<int64_t>(strlen(key));
  for (int64_t i = 0; OB_SUCC(ret) && !found && OB_NOT_NULL(data) && i + key_len + 2 <= len; ++i) {
    if ('"' == data[i]
        && 0 == MEMCMP(data + i + 1, key, key_len)
        && '"' == data[i + key_len + 1]) {
      int64_t pos = i + key_len + 2;
      while (pos < len && ai_split_is_space(data[pos])) {
        ++pos;
      }
      if (pos >= len || ':' != data[pos]) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid ai_split_document json parameter", K(ret), K(json), K(key));
      } else {
        ++pos;
        while (pos < len && ai_split_is_space(data[pos])) {
          ++pos;
        }
        if (pos >= len || !ai_split_is_digit(data[pos])) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid ai_split_document integer parameter", K(ret), K(json), K(key));
        } else {
          int64_t result = 0;
          while (pos < len && ai_split_is_digit(data[pos])) {
            result = result * 10 + data[pos] - '0';
            ++pos;
          }
          value = result;
          found = true;
        }
      }
    }
  }
  return ret;
}

static int ai_split_parse_options(const ObString &params, ObAiSplitOptions &options)
{
  int ret = OB_SUCCESS;
  if (0 == params.length() || OB_ISNULL(params.ptr())) {
    // use defaults
  } else {
    ObString str_value;
    int64_t int_value = 0;
    bool found = false;
    if (OB_FAIL(ai_split_find_json_string(params, "type", str_value, found))) {
      LOG_WARN("failed to parse ai_split_document type", K(ret), K(params));
    } else if (found && ai_split_equal_literal(str_value, "markdown")) {
      options.is_markdown_ = true;
    } else if (found && ai_split_equal_literal(str_value, "text")) {
      options.is_markdown_ = false;
    } else if (found) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("unsupported ai_split_document type", K(ret), K(str_value));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "unsupported ai_split_document type");
    }
    if (OB_SUCC(ret)) {
      found = false;
      if (OB_FAIL(ai_split_find_json_string(params, "by", str_value, found))) {
        LOG_WARN("failed to parse ai_split_document by", K(ret), K(params));
      } else if (found && ai_split_equal_literal(str_value, "word")) {
        options.by_word_ = true;
      } else if (found && ai_split_equal_literal(str_value, "sentence")) {
        options.by_word_ = false;
      } else if (found) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("unsupported ai_split_document split method", K(ret), K(str_value));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "unsupported ai_split_document split method");
      }
    }
    if (OB_SUCC(ret)) {
      found = false;
      if (OB_FAIL(ai_split_find_json_int(params, "max", int_value, found))) {
        LOG_WARN("failed to parse ai_split_document max", K(ret), K(params));
      } else if (found) {
        options.max_ = int_value;
      }
    }
    if (OB_SUCC(ret)) {
      found = false;
      if (OB_FAIL(ai_split_find_json_int(params, "overlap", int_value, found))) {
        LOG_WARN("failed to parse ai_split_document overlap", K(ret), K(params));
      } else if (found) {
        options.overlap_ = int_value;
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (options.max_ <= 0 || options.overlap_ < 0) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid ai_split_document numeric option", K(ret), K(options.max_), K(options.overlap_));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "invalid ai_split_document numeric option");
    } else if (options.overlap_ >= options.max_) {
      options.overlap_ = options.max_ - 1;
    }
  }
  return ret;
}

static int ai_split_collect_sentence_spans(const ObString &content,
                                           const int64_t range_start,
                                           const int64_t range_end,
                                           ObIArray<ObAiSplitSpan> &spans)
{
  int ret = OB_SUCCESS;
  const char *data = content.ptr();
  int64_t pos = range_start;
  while (OB_SUCC(ret) && OB_NOT_NULL(data) && pos < range_end) {
    while (pos < range_end && ai_split_is_space(data[pos])) {
      ++pos;
    }
    const int64_t start = pos;
    while (pos < range_end && '.' != data[pos]) {
      ++pos;
    }
    if (pos < range_end && '.' == data[pos]) {
      ++pos;
    }
    int64_t end = pos;
    while (end > start && ai_split_is_space(data[end - 1])) {
      --end;
    }
    if (end > start) {
      ObAiSplitSpan span;
      span.start_ = start;
      span.end_ = end;
      if (OB_FAIL(spans.push_back(span))) {
        LOG_WARN("failed to push ai_split_document sentence span", K(ret));
      }
    }
  }
  return ret;
}

static int ai_split_collect_word_spans(const ObString &content,
                                       const int64_t range_start,
                                       const int64_t range_end,
                                       ObIArray<ObAiSplitSpan> &spans)
{
  int ret = OB_SUCCESS;
  const char *data = content.ptr();
  int64_t pos = range_start;
  while (OB_SUCC(ret) && OB_NOT_NULL(data) && pos < range_end) {
    while (pos < range_end && ai_split_is_space(data[pos])) {
      ++pos;
    }
    const int64_t start = pos;
    while (pos < range_end && !ai_split_is_space(data[pos])) {
      ++pos;
    }
    if (pos > start) {
      ObAiSplitSpan span;
      span.start_ = start;
      span.end_ = pos;
      if (OB_FAIL(spans.push_back(span))) {
        LOG_WARN("failed to push ai_split_document word span", K(ret));
      }
    }
  }
  return ret;
}

static int ai_split_copy_text(ObIAllocator &allocator, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  dst.reset();
  if (src.length() < 0 || src.length() > INT32_MAX) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("ai_split_document text size overflow", K(ret), K(src.length()));
  } else if (0 == src.length()) {
    // keep empty
  } else {
    char *buf = static_cast<char *>(allocator.alloc(src.length()));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate ai_split_document text", K(ret), K(src.length()));
    } else {
      MEMCPY(buf, src.ptr(), src.length());
      dst.assign_ptr(buf, static_cast<ObString::obstr_size_t>(src.length()));
    }
  }
  return ret;
}

} // namespace

OB_SERIALIZE_MEMBER((ObFunctionTableSpec, ObOpSpec), value_expr_, column_exprs_, has_correlated_expr_);


void ObFunctionTableOp::reset_ai_split_document_state()
{
  ai_split_chunks_.reset();
  ai_split_done_ = false;
  ai_split_row_idx_ = 0;
}

int ObFunctionTableOp::append_ai_split_chunk(const int64_t chunk_offset,
                                             const int64_t chunk_length,
                                             const ObString &chunk_text)
{
  int ret = OB_SUCCESS;
  ObString copied_text;
  if (OB_FAIL(ai_split_copy_text(ctx_.get_allocator(), chunk_text, copied_text))) {
    LOG_WARN("failed to copy ai_split_document chunk text", K(ret));
  } else {
    ObAiSplitChunk chunk;
    chunk.chunk_id_ = ai_split_chunks_.count();
    chunk.chunk_offset_ = chunk_offset;
    chunk.chunk_length_ = chunk_length;
    chunk.chunk_text_ = copied_text;
    if (OB_FAIL(ai_split_chunks_.push_back(chunk))) {
      LOG_WARN("failed to push ai_split_document chunk", K(ret));
    }
  }
  return ret;
}

int ObFunctionTableOp::prepare_ai_split_document_chunks()
{
  int ret = OB_SUCCESS;
  ObExpr *value_expr = MY_SPEC.value_expr_;
  ObDatum *content_datum = NULL;
  ObDatum *params_datum = NULL;
  ObString content;
  ObString params;
  ObAiSplitOptions options;
  ai_split_chunks_.reset();
  ai_split_row_idx_ = 0;
  if (OB_ISNULL(value_expr) || OB_ISNULL(value_expr->args_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ai_split_document value expr is invalid", K(ret), KP(value_expr));
  } else if (OB_UNLIKELY(value_expr->arg_cnt_ < 1 || value_expr->arg_cnt_ > 2)) {
    ret = OB_INVALID_ARGUMENT_NUM;
    LOG_WARN("invalid ai_split_document argument count", K(ret), K(value_expr->arg_cnt_));
  } else if (OB_FAIL(value_expr->args_[0]->eval(eval_ctx_, content_datum))) {
    LOG_WARN("failed to eval ai_split_document content", K(ret));
  } else if (OB_ISNULL(content_datum) || content_datum->is_null()) {
    // NULL content returns an empty table.
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx_.get_allocator(),
                                                               *content_datum,
                                                               value_expr->args_[0]->datum_meta_,
                                                               value_expr->args_[0]->obj_meta_.has_lob_header(),
                                                               content))) {
    LOG_WARN("failed to read ai_split_document content", K(ret));
  } else {
    if (2 == value_expr->arg_cnt_) {
      if (OB_FAIL(value_expr->args_[1]->eval(eval_ctx_, params_datum))) {
        LOG_WARN("failed to eval ai_split_document params", K(ret));
      } else if (OB_ISNULL(params_datum) || params_datum->is_null()) {
        params.reset();
      } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx_.get_allocator(),
                                                                  *params_datum,
                                                                  value_expr->args_[1]->datum_meta_,
                                                                  value_expr->args_[1]->obj_meta_.has_lob_header(),
                                                                  params))) {
        LOG_WARN("failed to read ai_split_document params", K(ret));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ai_split_parse_options(params, options))) {
      LOG_WARN("failed to parse ai_split_document options", K(ret), K(params));
    }
    if (OB_SUCC(ret) && !options.is_markdown_ && options.by_word_) {
      ObSEArray<ObAiSplitSpan, 16> spans;
      if (OB_FAIL(ai_split_collect_word_spans(content, 0, content.length(), spans))) {
        LOG_WARN("failed to collect ai_split_document word spans", K(ret));
      } else {
        const int64_t step = options.max_ - options.overlap_;
        for (int64_t idx = 0; OB_SUCC(ret) && idx < spans.count(); idx += step) {
          const int64_t group_end_idx = (idx + options.max_ < spans.count()) ? idx + options.max_ - 1 : spans.count() - 1;
          int64_t total_len = group_end_idx - idx;
          for (int64_t word_idx = idx; word_idx <= group_end_idx; ++word_idx) {
            total_len += spans.at(word_idx).end_ - spans.at(word_idx).start_;
          }
          if (total_len > INT32_MAX) {
            ret = OB_SIZE_OVERFLOW;
            LOG_WARN("ai_split_document word chunk is too large", K(ret), K(total_len));
          } else {
            char *buf = static_cast<char *>(ctx_.get_allocator().alloc(total_len));
            if (OB_ISNULL(buf)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("failed to allocate ai_split_document word chunk", K(ret), K(total_len));
            } else {
              int64_t pos = 0;
              for (int64_t word_idx = idx; word_idx <= group_end_idx; ++word_idx) {
                if (word_idx > idx) {
                  buf[pos++] = ' ';
                }
                const int64_t word_len = spans.at(word_idx).end_ - spans.at(word_idx).start_;
                MEMCPY(buf + pos, content.ptr() + spans.at(word_idx).start_, word_len);
                pos += word_len;
              }
              ObString chunk_text(static_cast<ObString::obstr_size_t>(total_len),
                                  static_cast<ObString::obstr_size_t>(total_len),
                                  buf);
              if (OB_FAIL(append_ai_split_chunk(spans.at(idx).start_, total_len, chunk_text))) {
                LOG_WARN("failed to append ai_split_document word chunk", K(ret));
              }
            }
          }
        }
      }
    } else if (OB_SUCC(ret) && !options.is_markdown_) {
      ObSEArray<ObAiSplitSpan, 16> spans;
      if (OB_FAIL(ai_split_collect_sentence_spans(content, 0, content.length(), spans))) {
        LOG_WARN("failed to collect ai_split_document sentence spans", K(ret));
      } else {
        const int64_t step = options.max_ - options.overlap_;
        for (int64_t idx = 0; OB_SUCC(ret) && idx < spans.count(); idx += step) {
          const int64_t group_end_idx = (idx + options.max_ < spans.count()) ? idx + options.max_ - 1 : spans.count() - 1;
          const int64_t offset = spans.at(idx).start_;
          const int64_t end = spans.at(group_end_idx).end_;
          ObString chunk_text(static_cast<ObString::obstr_size_t>(end - offset),
                              static_cast<ObString::obstr_size_t>(end - offset),
                              content.ptr() + offset);
          if (OB_FAIL(append_ai_split_chunk(offset, end - offset, chunk_text))) {
            LOG_WARN("failed to append ai_split_document sentence chunk", K(ret));
          }
        }
      }
    } else if (OB_SUCC(ret)) {
      const char *data = content.ptr();
      ObString heading;
      int64_t pos = 0;
      while (OB_SUCC(ret) && OB_NOT_NULL(data) && pos < content.length()) {
        const int64_t line_start = pos;
        while (pos < content.length() && '\n' != data[pos]) {
          ++pos;
        }
        const int64_t line_end = pos;
        if (pos < content.length() && '\n' == data[pos]) {
          ++pos;
        }
        int64_t first_non_space = line_start;
        while (first_non_space < line_end
               && (' ' == data[first_non_space] || '\t' == data[first_non_space] || '\r' == data[first_non_space])) {
          ++first_non_space;
        }
        if (first_non_space >= line_end) {
          // skip empty line
        } else if ('#' == data[first_non_space]) {
          int64_t heading_end = line_end;
          while (heading_end > first_non_space && ai_split_is_space(data[heading_end - 1])) {
            --heading_end;
          }
          heading.assign_ptr(data + first_non_space,
                             static_cast<ObString::obstr_size_t>(heading_end - first_non_space));
        } else {
          ObSEArray<ObAiSplitSpan, 8> spans;
          if (options.by_word_
              && OB_FAIL(ai_split_collect_word_spans(content, line_start, line_end, spans))) {
            LOG_WARN("failed to collect ai_split_document markdown word spans", K(ret));
          } else if (!options.by_word_
                     && OB_FAIL(ai_split_collect_sentence_spans(content, line_start, line_end, spans))) {
            LOG_WARN("failed to collect ai_split_document markdown sentence spans", K(ret));
          } else {
            const int64_t step = options.max_ - options.overlap_;
            for (int64_t idx = 0; OB_SUCC(ret) && idx < spans.count(); idx += step) {
              const int64_t group_end_idx = (idx + options.max_ < spans.count()) ? idx + options.max_ - 1 : spans.count() - 1;
              const int64_t unit_start = spans.at(idx).start_;
              const int64_t unit_end = spans.at(group_end_idx).end_;
              int64_t unit_len = unit_end - unit_start;
              if (options.by_word_) {
                unit_len = group_end_idx - idx;
                for (int64_t word_idx = idx; word_idx <= group_end_idx; ++word_idx) {
                  unit_len += spans.at(word_idx).end_ - spans.at(word_idx).start_;
                }
              }
              const int64_t total_len = unit_len + (heading.length() > 0 ? heading.length() + 1 : 0);
              if (total_len > INT32_MAX) {
                ret = OB_SIZE_OVERFLOW;
                LOG_WARN("ai_split_document markdown chunk is too large", K(ret), K(total_len));
              } else {
                char *buf = static_cast<char *>(ctx_.get_allocator().alloc(total_len));
                if (OB_ISNULL(buf)) {
                  ret = OB_ALLOCATE_MEMORY_FAILED;
                  LOG_WARN("failed to allocate ai_split_document markdown chunk", K(ret), K(total_len));
                } else {
                  int64_t write_pos = 0;
                  if (heading.length() > 0) {
                    MEMCPY(buf + write_pos, heading.ptr(), heading.length());
                    write_pos += heading.length();
                    buf[write_pos++] = '\n';
                  }
                  if (options.by_word_) {
                    for (int64_t word_idx = idx; word_idx <= group_end_idx; ++word_idx) {
                      if (word_idx > idx) {
                        buf[write_pos++] = ' ';
                      }
                      const int64_t word_len = spans.at(word_idx).end_ - spans.at(word_idx).start_;
                      MEMCPY(buf + write_pos, content.ptr() + spans.at(word_idx).start_, word_len);
                      write_pos += word_len;
                    }
                  } else {
                    MEMCPY(buf + write_pos, content.ptr() + unit_start, unit_len);
                    write_pos += unit_len;
                  }
                  ObString chunk_text(static_cast<ObString::obstr_size_t>(total_len),
                                      static_cast<ObString::obstr_size_t>(total_len),
                                      buf);
                  if (OB_FAIL(append_ai_split_chunk(unit_start, total_len, chunk_text))) {
                    LOG_WARN("failed to append ai_split_document markdown chunk", K(ret));
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    ai_split_done_ = true;
  }
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_ai_split_document()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  clear_evaluated_flag();
  if (OB_ISNULL(plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get plan ctx", K(ret), K(plan_ctx));
  } else if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check status ", K(ret));
  } else if (!ai_split_done_ && OB_FAIL(prepare_ai_split_document_chunks())) {
    LOG_WARN("failed to prepare ai_split_document chunks", K(ret));
  } else if (ai_split_row_idx_ >= ai_split_chunks_.count()) {
    ret = OB_ITER_END;
  } else if (OB_UNLIKELY(MY_SPEC.column_exprs_.count() < 4)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected ai_split_document column count", K(ret), K(MY_SPEC.column_exprs_.count()));
  } else {
    const ObAiSplitChunk &chunk = ai_split_chunks_.at(ai_split_row_idx_++);
    MY_SPEC.column_exprs_.at(0)->locate_datum_for_write(eval_ctx_).set_int(chunk.chunk_id_);
    MY_SPEC.column_exprs_.at(1)->locate_datum_for_write(eval_ctx_).set_int(chunk.chunk_offset_);
    MY_SPEC.column_exprs_.at(2)->locate_datum_for_write(eval_ctx_).set_int(chunk.chunk_length_);
    ObExpr *text_expr = MY_SPEC.column_exprs_.at(3);
    ObDatum &text_datum = text_expr->locate_datum_for_write(eval_ctx_);
    if (is_lob_storage(text_expr->obj_meta_.get_type())) {
      if (OB_FAIL(ObTextStringHelper::string_to_templob_result(*text_expr, eval_ctx_, text_datum, chunk.chunk_text_))) {
        LOG_WARN("failed to set ai_split_document text lob result", K(ret));
      }
    } else {
      text_datum.set_string(chunk.chunk_text_);
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < 4; ++i) {
      MY_SPEC.column_exprs_.at(i)->set_evaluated_projected(eval_ctx_);
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_open()
{
  int ret = OB_SUCCESS;
  node_idx_ = 0;
  already_calc_ = false;
  reset_ai_split_document_state();
  if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value expr is not init", K(ret));
  } else if (T_FUN_SYS_AI_SPLIT_DOCUMENT == MY_SPEC.value_expr_->type_) {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_ai_split_document;
  } else if (ObExtendType == MY_SPEC.value_expr_->datum_meta_.type_) {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_udf;
  } else {
    next_row_func_ = &ObFunctionTableOp::inner_get_next_row_sys_func;
  }
  return ret;
}

int ObFunctionTableOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
    LOG_WARN("failed to inner rescan", K(ret));
  } else {
    node_idx_ = 0;
    reset_ai_split_document_state();
    if (MY_SPEC.has_correlated_expr_) {
      row_count_ = 0;
      col_count_ = 0;
      value_table_ = NULL;
      already_calc_ = false;
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_close()
{
  int ret = OB_SUCCESS;
  node_idx_ = 0;
  row_count_ = 0;
  col_count_ = 0;
  value_table_ = NULL;
  reset_ai_split_document_state();
  return ret;
}

//ObFunctionTableOp has its own switch_iterator
int ObFunctionTableOp::switch_iterator()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_switch_iterator())) {
    LOG_WARN("failed to switch iterator", K(ret));
  } else if (OB_ISNULL(ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get session", K(ret));
  } else if (NULL == ctx_.get_my_session()->get_pl_implicit_cursor()
            || !ctx_.get_my_session()->get_pl_implicit_cursor()->get_in_forall()) {
    ret = OB_ITER_END;
  } else {
    node_idx_ = 0;
  }
  return ret;
}

void ObFunctionTableOp::destroy()
{
  ObOperator::destroy();
}


int ObFunctionTableOp::get_current_result(ObObj &result)
{
  int ret = OB_SUCCESS;
  void *data = NULL;
  CK (already_calc_);
  if (node_idx_ < 0 || node_idx_ >= row_count_ * col_count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get current result in table function",
              K(node_idx_), K(row_count_), K(col_count_));
  }
  do {
    CK (node_idx_ >= 0);
    if (OB_SUCC(ret) && node_idx_ >= row_count_) {
      ret = OB_ITER_END;
    }
    CK (OB_NOT_NULL(value_table_));
    OX (data = value_table_->get_data());
    CK (OB_NOT_NULL(data));
    OX (result = (static_cast<ObObj*>(data))[node_idx_++]);
  } while (OB_SUCC(ret) && result.get_meta().get_type() == ObMaxType);
  return ret;
}

int ObFunctionTableOp::inner_get_next_row()
{
  return (this->*next_row_func_)();
}

int ObFunctionTableOp::inner_get_next_row_udf()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  clear_evaluated_flag();
  if (OB_ISNULL(plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get plan ctx", K(ret), K(plan_ctx));
  } else if (OB_ISNULL(MY_SPEC.value_expr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value expr is not init", K(ret));
  } else if (ObExtendType != MY_SPEC.value_expr_->datum_meta_.type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected value", K(ret), K(MY_SPEC.value_expr_->datum_meta_.type_));
  } else if (FALSE_IT(plan_ctx->set_autoinc_id_tmp(0))) {
  } else if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check status ", K(ret));
  } else {
    ObDatum *value = nullptr;
    if (!already_calc_) {
      if (OB_FAIL(MY_SPEC.value_expr_->eval(eval_ctx_, value))) {
        LOG_WARN("failed to eval value expr", K(ret));
      } else if (value->is_null()) {
        //do nothing
      } else if (OB_ISNULL(value_table_ 
                 = reinterpret_cast<pl::ObPLCollection*>(value->get_ext()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get value table", K(ret));
      } else {
        row_count_ = value_table_->is_inited() ? value_table_->get_count() : 0;
        col_count_ = value_table_->get_column_count();
      }
      already_calc_ = true;
    }
    if (OB_SUCC(ret) && OB_UNLIKELY(node_idx_ >= row_count_)) {
      ret = OB_ITER_END;
    }
    CK (MY_SPEC.column_exprs_.count() >= col_count_);
    ObObj obj_stack[col_count_];
    if (OB_SUCC(ret)) {
      if (nullptr != value_table_ 
          && ObExtendType == value_table_->get_element_type().get_obj_type()) {
        pl::ObPLComposite *composite = NULL;
        pl::ObPLRecord *record = NULL;
        ObObj record_obj;
        OZ (get_current_result(record_obj));
        if (OB_FAIL(ret)) {
        } else if (ObUserDefinedSQLType == record_obj.get_type()) {
          obj_stack[0] = record_obj;   
        } else if (record_obj.is_pl_extend()) {
          CK (OB_NOT_NULL(composite = reinterpret_cast<pl::ObPLComposite*>(record_obj.get_ext())));
          if (OB_SUCC(ret)) {
            if (composite->is_record()) {
              OX (record = static_cast<pl::ObPLRecord*>(composite));
              CK (record->get_count() == col_count_);
              for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
                OZ (record->get_element(i, obj_stack[i]));
              }
            } else if (composite->is_collection()) {
              OX (obj_stack[0] = record_obj);
            } else {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected composite type", K(ret), K(composite->get_type()));
            }
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("unexpected here", K(ret), K(record_obj), K(record_obj.meta_));
        }
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
          if (OB_FAIL(get_current_result(obj_stack[i]))) {
            LOG_WARN("failed to get current result", K(ret), K(i));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count_; ++i) {
        if (obj_stack[i].is_null()) {
          MY_SPEC.column_exprs_.at(i)->locate_datum_for_write(eval_ctx_).set_null();
        } else {
          const ObObjDatumMapType &datum_map = MY_SPEC.column_exprs_.at(i)->obj_datum_map_;
          ObExpr * const &expr = MY_SPEC.column_exprs_.at(i);
          ObDatum &datum = expr->locate_datum_for_write(eval_ctx_);
          if (OB_FAIL(datum.from_obj(obj_stack[i], datum_map))) {
            LOG_WARN("failed to convert datum", K(ret));
          } else if (is_lob_storage(obj_stack[i].get_type()) &&
                     OB_FAIL(ob_adjust_lob_datum(obj_stack[i], expr->obj_meta_, datum_map,
                                                 get_exec_ctx().get_allocator(), datum))) {
            LOG_WARN("adjust lob datum failed", K(ret), K(obj_stack[i].get_meta()), K(expr->obj_meta_));
          }
        }
        if (OB_SUCC(ret)) {
          MY_SPEC.column_exprs_.at(i)->set_evaluated_projected(eval_ctx_);
        }
      }
    }
  }
  return ret;
}

int ObFunctionTableOp::inner_get_next_row_sys_func()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  ObDatum *value = nullptr;
  clear_evaluated_flag();
  if (OB_ISNULL(plan_ctx = ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get plan ctx", K(ret), K(plan_ctx));
  } else if (OB_FAIL(ctx_.check_status())) {
    LOG_WARN("failed to check status ", K(ret));
  } else if (OB_FAIL(MY_SPEC.value_expr_->eval(eval_ctx_, value))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("failed to eval value expr", K(ret));
    }
  } else {
    MY_SPEC.column_exprs_.at(0)->locate_datum_for_write(eval_ctx_).set_datum(*value);
    MY_SPEC.column_exprs_.at(0)->set_evaluated_projected(eval_ctx_);
  }
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
