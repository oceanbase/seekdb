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

#include "sql/engine/expr/ob_expr_ai/ob_expr_ai_split_document.h"

#include <algorithm>
#include <cctype>

#include "common/json_type/ob_json_tree.h"
#include "share/ob_json_access_utils.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_expr_multi_mode_func_helper.h"

namespace oceanbase
{
using namespace common;

namespace sql
{

namespace
{

struct TextSpan
{
  TextSpan() : begin_(0), end_(0) {}
  TextSpan(int64_t begin, int64_t end) : begin_(begin), end_(end) {}
  int64_t begin_;
  int64_t end_;
  TO_STRING_KV(K_(begin), K_(end));
};

struct SplitOptions
{
  SplitOptions() : markdown_(true), by_sentence_(false), max_(256), overlap_(0) {}
  bool markdown_;
  bool by_sentence_;
  int64_t max_;
  int64_t overlap_;
};

bool is_space(const char ch)
{
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

TextSpan trim_span(const ObString &content, int64_t begin, int64_t end)
{
  while (begin < end && is_space(content.ptr()[begin])) {
    ++begin;
  }
  while (end > begin && is_space(content.ptr()[end - 1])) {
    --end;
  }
  return TextSpan(begin, end);
}

bool is_sentence_terminator(const ObString &content, int64_t pos, int64_t &width)
{
  bool terminator = false;
  width = 1;
  const char ch = content.ptr()[pos];
  if (ch == '.' || ch == '!' || ch == '?') {
    terminator = true;
  } else if (pos + 3 <= content.length()) {
    const unsigned char *ptr = reinterpret_cast<const unsigned char *>(content.ptr() + pos);
    if ((ptr[0] == 0xE3 && ptr[1] == 0x80 && ptr[2] == 0x82)  // Chinese full stop
        || (ptr[0] == 0xEF && ptr[1] == 0xBC && ptr[2] == 0x81) // full-width !
        || (ptr[0] == 0xEF && ptr[1] == 0xBC && ptr[2] == 0x9F)) { // full-width ?
      width = 3;
      terminator = true;
    }
  }
  return terminator;
}

int collect_word_spans(const ObString &content,
                       int64_t begin,
                       int64_t end,
                       ObIArray<TextSpan> &spans)
{
  int ret = OB_SUCCESS;
  int64_t pos = begin;
  while (OB_SUCC(ret) && pos < end) {
    while (pos < end && is_space(content.ptr()[pos])) {
      ++pos;
    }
    const int64_t word_begin = pos;
    while (pos < end && !is_space(content.ptr()[pos])) {
      ++pos;
    }
    if (word_begin < pos && OB_FAIL(spans.push_back(TextSpan(word_begin, pos)))) {
      LOG_WARN("failed to append word span", K(ret));
    }
  }
  return ret;
}

int collect_sentence_spans(const ObString &content,
                           int64_t begin,
                           int64_t end,
                           ObIArray<TextSpan> &spans)
{
  int ret = OB_SUCCESS;
  int64_t sentence_begin = begin;
  int64_t pos = begin;
  while (OB_SUCC(ret) && pos < end) {
    int64_t width = 1;
    if (is_sentence_terminator(content, pos, width)) {
      const TextSpan span = trim_span(content, sentence_begin, pos + width);
      if (span.begin_ < span.end_ && OB_FAIL(spans.push_back(span))) {
        LOG_WARN("failed to append sentence span", K(ret));
      }
      pos += width;
      sentence_begin = pos;
    } else {
      ++pos;
    }
  }
  if (OB_SUCC(ret)) {
    const TextSpan tail = trim_span(content, sentence_begin, end);
    if (tail.begin_ < tail.end_ && OB_FAIL(spans.push_back(tail))) {
      LOG_WARN("failed to append trailing sentence", K(ret));
    }
  }
  return ret;
}

int add_chunk_json(ObIAllocator &allocator,
                   ObJsonArray &result,
                   int64_t chunk_id,
                   const TextSpan &source_span,
                   const ObString &chunk_text)
{
  int ret = OB_SUCCESS;
  ObJsonObject *chunk = OB_NEWx(ObJsonObject, &allocator, &allocator);
  ObJsonInt *id = OB_NEWx(ObJsonInt, &allocator, chunk_id);
  ObJsonInt *offset = OB_NEWx(ObJsonInt, &allocator, source_span.begin_);
  ObJsonInt *length = OB_NEWx(ObJsonInt, &allocator, source_span.end_ - source_span.begin_);
  ObJsonString *text = OB_NEWx(ObJsonString, &allocator, chunk_text);
  if (OB_ISNULL(chunk) || OB_ISNULL(id) || OB_ISNULL(offset)
      || OB_ISNULL(length) || OB_ISNULL(text)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate split document JSON node", K(ret));
  } else if (OB_FAIL(chunk->add("chunk_id", id))) {
    LOG_WARN("failed to add chunk id", K(ret));
  } else if (OB_FAIL(chunk->add("chunk_offset", offset))) {
    LOG_WARN("failed to add chunk offset", K(ret));
  } else if (OB_FAIL(chunk->add("chunk_length", length))) {
    LOG_WARN("failed to add chunk length", K(ret));
  } else if (OB_FAIL(chunk->add("chunk_text", text))) {
    LOG_WARN("failed to add chunk text", K(ret));
  } else if (OB_FAIL(result.append(chunk))) {
    LOG_WARN("failed to append split document chunk", K(ret));
  }
  return ret;
}

int make_chunk_text(ObIAllocator &allocator,
                    const ObString &content,
                    const TextSpan &span,
                    const ObString &heading,
                    ObString &chunk_text)
{
  int ret = OB_SUCCESS;
  const int64_t content_len = span.end_ - span.begin_;
  if (heading.empty()) {
    chunk_text.assign_ptr(content.ptr() + span.begin_, content_len);
  } else {
    const int64_t total_len = heading.length() + 1 + content_len;
    char *buf = static_cast<char *>(allocator.alloc(total_len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate markdown chunk text", K(ret), K(total_len));
    } else {
      MEMCPY(buf, heading.ptr(), heading.length());
      buf[heading.length()] = '\n';
      MEMCPY(buf + heading.length() + 1, content.ptr() + span.begin_, content_len);
      chunk_text.assign_ptr(buf, total_len);
    }
  }
  return ret;
}

int split_range(ObIAllocator &allocator,
                const ObString &content,
                int64_t begin,
                int64_t end,
                const ObString &heading,
                const SplitOptions &options,
                int64_t &chunk_id,
                ObJsonArray &result)
{
  int ret = OB_SUCCESS;
  ObSEArray<TextSpan, 64> units;
  const TextSpan range = trim_span(content, begin, end);
  if (range.begin_ >= range.end_) {
  } else if (options.by_sentence_
             && OB_FAIL(collect_sentence_spans(content, range.begin_, range.end_, units))) {
    LOG_WARN("failed to collect sentence spans", K(ret));
  } else if (!options.by_sentence_
             && OB_FAIL(collect_word_spans(content, range.begin_, range.end_, units))) {
    LOG_WARN("failed to collect word spans", K(ret));
  } else {
    int64_t unit_begin = 0;
    while (OB_SUCC(ret) && unit_begin < units.count()) {
      const int64_t unit_end = std::min(unit_begin + options.max_, units.count());
      const TextSpan source_span(units.at(unit_begin).begin_, units.at(unit_end - 1).end_);
      ObString chunk_text;
      if (OB_FAIL(make_chunk_text(allocator, content, source_span, heading, chunk_text))) {
        LOG_WARN("failed to construct chunk text", K(ret));
      } else if (OB_FAIL(add_chunk_json(allocator, result, chunk_id++, source_span, chunk_text))) {
        LOG_WARN("failed to append chunk result", K(ret));
      } else if (unit_end == units.count()) {
        break;
      } else {
        unit_begin = unit_end - options.overlap_;
      }
    }
  }
  return ret;
}

bool parse_markdown_heading(const ObString &content,
                            int64_t line_begin,
                            int64_t line_end,
                            ObString &heading)
{
  int64_t pos = line_begin;
  int64_t leading_spaces = 0;
  while (pos < line_end && leading_spaces < 4 && content.ptr()[pos] == ' ') {
    ++pos;
    ++leading_spaces;
  }
  const int64_t heading_begin = pos;
  while (pos < line_end && content.ptr()[pos] == '#') {
    ++pos;
  }
  const bool valid = leading_spaces <= 3
      && pos > heading_begin
      && (pos == line_end || content.ptr()[pos] == ' ' || content.ptr()[pos] == '\t');
  if (valid) {
    while (line_end > heading_begin
           && (content.ptr()[line_end - 1] == '\r' || content.ptr()[line_end - 1] == ' ')) {
      --line_end;
    }
    heading.assign_ptr(content.ptr() + heading_begin, line_end - heading_begin);
  }
  return valid;
}

int split_markdown(ObIAllocator &allocator,
                   const ObString &content,
                   const SplitOptions &options,
                   ObJsonArray &result)
{
  int ret = OB_SUCCESS;
  int64_t chunk_id = 0;
  int64_t section_begin = 0;
  int64_t line_begin = 0;
  ObString heading;
  while (OB_SUCC(ret) && line_begin <= content.length()) {
    int64_t line_end = line_begin;
    while (line_end < content.length() && content.ptr()[line_end] != '\n') {
      ++line_end;
    }
    ObString next_heading;
    if (parse_markdown_heading(content, line_begin, line_end, next_heading)) {
      if (OB_FAIL(split_range(allocator, content, section_begin, line_begin,
                              heading, options, chunk_id, result))) {
        LOG_WARN("failed to split markdown section", K(ret));
      } else {
        heading = next_heading;
        section_begin = line_end < content.length() ? line_end + 1 : line_end;
      }
    }
    if (line_end >= content.length()) {
      break;
    }
    line_begin = line_end + 1;
  }
  if (OB_SUCC(ret)
      && OB_FAIL(split_range(allocator, content, section_begin, content.length(),
                             heading, options, chunk_id, result))) {
    LOG_WARN("failed to split final markdown section", K(ret));
  }
  return ret;
}

int get_string_option(ObJsonObject *config,
                      const char *key,
                      ObString &value)
{
  int ret = OB_SUCCESS;
  ObJsonNode *node = config->get_value(key);
  if (OB_ISNULL(node)) {
  } else if (node->json_type() != ObJsonNodeType::J_STRING) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document string option has invalid type");
  } else {
    value = static_cast<ObJsonString *>(node)->value();
  }
  return ret;
}

int get_int_option(ObJsonObject *config,
                   const char *key,
                   int64_t &value)
{
  int ret = OB_SUCCESS;
  ObJsonNode *node = config->get_value(key);
  if (OB_ISNULL(node)) {
  } else if (node->json_type() != ObJsonNodeType::J_INT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document integer option has invalid type");
  } else {
    value = static_cast<ObJsonInt *>(node)->value();
  }
  return ret;
}

int parse_options(ObJsonObject *config, SplitOptions &options)
{
  int ret = OB_SUCCESS;
  ObString type("markdown");
  ObString by("word");
  if (OB_ISNULL(config)) {
  } else if (OB_FAIL(get_string_option(config, "type", type))) {
    LOG_WARN("failed to parse split type", K(ret));
  } else if (OB_FAIL(get_string_option(config, "by", by))) {
    LOG_WARN("failed to parse split unit", K(ret));
  } else if (OB_FAIL(get_int_option(config, "max", options.max_))) {
    LOG_WARN("failed to parse split max", K(ret));
  } else if (OB_FAIL(get_int_option(config, "overlap", options.overlap_))) {
    LOG_WARN("failed to parse split overlap", K(ret));
  }

  if (OB_SUCC(ret)) {
    if (type.case_compare("markdown") == 0) {
      options.markdown_ = true;
    } else if (type.case_compare("text") == 0) {
      options.markdown_ = false;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document type must be text or markdown");
    }
  }
  if (OB_SUCC(ret)) {
    if (by.case_compare("sentence") == 0) {
      options.by_sentence_ = true;
    } else if (by.case_compare("word") == 0) {
      options.by_sentence_ = false;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document by must be word or sentence");
    }
  }
  if (OB_SUCC(ret) && (options.max_ <= 0
                       || options.overlap_ < 0
                       || options.overlap_ >= options.max_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document requires max > 0 and 0 <= overlap < max");
  }
  return ret;
}

} // namespace

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_AI_SPLIT_DOCUMENT, N_AI_SPLIT_DOCUMENT,
                         MORE_THAN_ZERO, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprAISplitDocument::~ObExprAISplitDocument()
{
}

int ObExprAISplitDocument::calc_result_typeN(ObExprResType &type,
                                             ObExprResType *types_array,
                                             int64_t param_num,
                                             ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (param_num < 1 || param_num > 2) {
    ret = OB_ERR_PARAM_SIZE;
    const ObString func_name(get_name());
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name.length(), func_name.ptr());
  } else if (!ob_is_string_tc(types_array[0].get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_OP,
                   "STRING", ob_obj_type_str(types_array[0].get_type()));
  } else {
    types_array[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (param_num == 2
        && OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_array, 1, N_AI_SPLIT_DOCUMENT))) {
      LOG_WARN("invalid ai_split_document parameters type", K(ret), K(types_array[1]));
    }
  }
  if (OB_SUCC(ret)) {
    type.set_json();
    type.set_length(ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType].get_length());
  }
  return ret;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                  ObEvalCtx &ctx,
                                                  ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *content_datum = nullptr;
  ObDatum *config_datum = nullptr;
  if ((expr.arg_cnt_ == 1 && OB_FAIL(expr.eval_param_value(ctx, content_datum)))
      || (expr.arg_cnt_ == 2 && OB_FAIL(expr.eval_param_value(ctx, content_datum, config_datum)))) {
    LOG_WARN("failed to evaluate ai_split_document arguments", K(ret));
  } else if (content_datum->is_null()) {
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard alloc_guard(ctx);
    MultimodeAlloctor allocator(alloc_guard.get_allocator(), expr.type_, ret);
    ObString content;
    ObJsonObject *config = nullptr;
    bool is_null = false;
    SplitOptions options;
    ObJsonArray result(&allocator);
    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
            allocator, *content_datum, expr.args_[0]->datum_meta_,
            expr.args_[0]->obj_meta_.has_lob_header(), content))) {
      LOG_WARN("failed to read ai_split_document content", K(ret));
    } else if (expr.arg_cnt_ == 2 && OB_NOT_NULL(config_datum) && !config_datum->is_null()) {
      ObIJsonBase *config_base = nullptr;
      if (OB_FAIL(ObJsonExprHelper::get_json_doc(expr, ctx, allocator, 1, config_base, is_null))) {
        LOG_WARN("failed to parse ai_split_document parameters", K(ret));
      } else if (OB_ISNULL(config_base) || config_base->json_type() != ObJsonNodeType::J_OBJECT) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_split_document parameters must be a JSON object");
      } else {
        config = static_cast<ObJsonObject *>(config_base);
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(parse_options(config, options))) {
      LOG_WARN("failed to parse ai_split_document options", K(ret));
    } else if (options.markdown_ && OB_FAIL(split_markdown(allocator, content, options, result))) {
      LOG_WARN("failed to split markdown document", K(ret));
    } else if (!options.markdown_) {
      int64_t chunk_id = 0;
      if (OB_FAIL(split_range(allocator, content, 0, content.length(), ObString(),
                              options, chunk_id, result))) {
        LOG_WARN("failed to split text document", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      ObString raw_binary;
      if (OB_FAIL(ObJsonWrapper::get_raw_binary(&result, raw_binary, &allocator))) {
        LOG_WARN("failed to serialize split document result", K(ret));
      } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_binary))) {
        LOG_WARN("failed to pack split document result", K(ret));
      }
    }
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
