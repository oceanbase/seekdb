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

#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace
{
enum class DocumentType : uint8_t
{
  TEXT,
  MARKDOWN
};

enum class SplitUnit : uint8_t
{
  WORD,
  SENTENCE
};

struct SplitOptions final
{
  SplitOptions() : type_(DocumentType::MARKDOWN), unit_(SplitUnit::WORD), max_(256), overlap_(0) {}

  DocumentType type_;
  SplitUnit unit_;
  int64_t max_;
  int64_t overlap_;
};

struct TextSpan final
{
  TextSpan() : begin_(0), end_(0) {}
  TextSpan(const int64_t begin, const int64_t end) : begin_(begin), end_(end) {}

  int64_t begin_;
  int64_t end_;

  TO_STRING_KV(K_(begin), K_(end));
};

bool is_ascii_space(const char ch)
{
  return ' ' == ch || '\t' == ch || '\r' == ch || '\n' == ch || '\f' == ch || '\v' == ch;
}

bool is_sentence_terminator(const char *ptr, const int64_t remaining, int64_t &terminator_length)
{
  bool is_terminator = false;
  terminator_length = 0;
  if (remaining > 0 && ('.' == ptr[0] || '!' == ptr[0] || '?' == ptr[0])) {
    is_terminator = true;
    terminator_length = 1;
  } else if (remaining >= 3) {
    const unsigned char first = static_cast<unsigned char>(ptr[0]);
    const unsigned char second = static_cast<unsigned char>(ptr[1]);
    const unsigned char third = static_cast<unsigned char>(ptr[2]);
    if ((0xE3 == first && 0x80 == second && 0x82 == third)
        || (0xEF == first && 0xBC == second && (0x81 == third || 0x9F == third))) {
      is_terminator = true;
      terminator_length = 3;
    }
  }
  return is_terminator;
}

int copy_string(ObIAllocator &allocator, const ObString &source, ObString &target)
{
  int ret = OB_SUCCESS;
  char *buffer = nullptr;
  if (source.empty()) {
    target.reset();
  } else if (OB_ISNULL(buffer = static_cast<char *>(allocator.alloc(source.length())))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate split document chunk", K(ret), "length", source.length());
  } else {
    MEMCPY(buffer, source.ptr(), source.length());
    target.assign_ptr(buffer, source.length());
  }
  return ret;
}

int get_json_value(ObJsonObject &object, const ObString &key, ObIJsonBase *&value)
{
  int ret = object.get_object_value(key, value);
  if (OB_SEARCH_NOT_FOUND == ret) {
    ret = OB_SUCCESS;
    value = nullptr;
  }
  return ret;
}

int parse_string_option(ObJsonObject &object, const ObString &key, ObString &value, bool &exists)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *node = nullptr;
  exists = false;
  if (OB_FAIL(get_json_value(object, key, node))) {
    LOG_WARN("failed to read split document string option", K(ret), K(key));
  } else if (OB_NOT_NULL(node)) {
    exists = true;
    if (OB_UNLIKELY(ObJsonNodeType::J_STRING != node->json_type())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("split document option must be a string", K(ret), K(key), "json_type", node->json_type());
    } else {
      value = static_cast<ObJsonString *>(node)->get_str();
    }
  }
  return ret;
}

int parse_integer_option(ObJsonObject &object, const ObString &key, int64_t &value, bool &exists)
{
  int ret = OB_SUCCESS;
  ObIJsonBase *node = nullptr;
  exists = false;
  if (OB_FAIL(get_json_value(object, key, node))) {
    LOG_WARN("failed to read split document integer option", K(ret), K(key));
  } else if (OB_NOT_NULL(node)) {
    exists = true;
    if (OB_UNLIKELY(ObJsonNodeType::J_INT != node->json_type())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("split document option must be an integer", K(ret), K(key), "json_type", node->json_type());
    } else {
      value = node->get_int();
    }
  }
  return ret;
}

int parse_options(ObIAllocator &allocator, const ObString *parameters, SplitOptions &options)
{
  int ret = OB_SUCCESS;
  ObJsonObject *object = nullptr;
  if (OB_ISNULL(parameters) || parameters->empty()) {
  } else {
    ObString parameters_copy = *parameters;
    if (OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(
            allocator, parameters_copy, object))) {
      LOG_WARN("failed to parse AI_SPLIT_DOCUMENT parameters", K(ret), KPC(parameters));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(parameters) || parameters->empty()) {
  } else if (OB_ISNULL(object)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI_SPLIT_DOCUMENT parameter object is null", K(ret));
  } else {
    ObString type;
    ObString unit;
    bool has_type = false;
    bool has_unit = false;
    bool has_max = false;
    bool has_overlap = false;
    for (uint64_t index = 0; OB_SUCC(ret) && index < object->element_count(); ++index) {
      ObString key;
      if (OB_FAIL(object->get_key(index, key))) {
        LOG_WARN("failed to read AI_SPLIT_DOCUMENT parameter key", K(ret), K(index));
      } else if (0 != key.compare("type") && 0 != key.compare("by")
                 && 0 != key.compare("max") && 0 != key.compare("overlap")) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("unknown AI_SPLIT_DOCUMENT parameter", K(ret), K(key));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "unknown AI_SPLIT_DOCUMENT parameter");
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(parse_string_option(*object, ObString("type"), type, has_type))) {
    } else if (OB_FAIL(parse_string_option(*object, ObString("by"), unit, has_unit))) {
    } else if (OB_FAIL(parse_integer_option(*object, ObString("max"), options.max_, has_max))) {
    } else if (OB_FAIL(parse_integer_option(*object, ObString("overlap"), options.overlap_, has_overlap))) {
    } else if (has_type && 0 == type.case_compare("text")) {
      options.type_ = DocumentType::TEXT;
    } else if (has_type && 0 == type.case_compare("markdown")) {
      options.type_ = DocumentType::MARKDOWN;
    } else if (has_type) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid AI_SPLIT_DOCUMENT type", K(ret), K(type));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "AI_SPLIT_DOCUMENT type must be text or markdown");
    }
    if (OB_SUCC(ret)) {
      if (!has_unit || 0 == unit.case_compare("word") || 0 == unit.case_compare("wod")) {
        options.unit_ = SplitUnit::WORD;
      } else if (0 == unit.case_compare("sentence")) {
        options.unit_ = SplitUnit::SENTENCE;
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid AI_SPLIT_DOCUMENT split unit", K(ret), K(unit));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "AI_SPLIT_DOCUMENT by must be word or sentence");
      }
    }
    if (OB_SUCC(ret) && (options.max_ <= 0 || options.overlap_ < 0 || options.overlap_ >= options.max_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid AI_SPLIT_DOCUMENT window", K(ret), K(options.max_), K(options.overlap_));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "AI_SPLIT_DOCUMENT requires max > 0 and 0 <= overlap < max");
    }
  }
  return ret;
}

int collect_word_spans(const ObString &content,
                       const int64_t begin,
                       const int64_t end,
                       ObIArray<TextSpan> &spans)
{
  int ret = OB_SUCCESS;
  int64_t pos = begin;
  while (OB_SUCC(ret) && pos < end) {
    while (pos < end && is_ascii_space(content.ptr()[pos])) {
      ++pos;
    }
    const int64_t word_begin = pos;
    while (pos < end && !is_ascii_space(content.ptr()[pos])) {
      ++pos;
    }
    if (word_begin < pos && OB_FAIL(spans.push_back(TextSpan(word_begin, pos)))) {
      LOG_WARN("failed to append word span", K(ret), K(word_begin), K(pos));
    }
  }
  return ret;
}

int collect_sentence_spans(const ObString &content,
                           const int64_t begin,
                           const int64_t end,
                           ObIArray<TextSpan> &spans)
{
  int ret = OB_SUCCESS;
  int64_t sentence_begin = begin;
  while (sentence_begin < end && is_ascii_space(content.ptr()[sentence_begin])) {
    ++sentence_begin;
  }
  int64_t pos = sentence_begin;
  while (OB_SUCC(ret) && pos < end) {
    int64_t terminator_length = 0;
    if (is_sentence_terminator(content.ptr() + pos, end - pos, terminator_length)) {
      const int64_t sentence_end = pos + terminator_length;
      if (sentence_begin < sentence_end && OB_FAIL(spans.push_back(TextSpan(sentence_begin, sentence_end)))) {
        LOG_WARN("failed to append sentence span", K(ret), K(sentence_begin), K(sentence_end));
      }
      pos = sentence_end;
      while (pos < end && is_ascii_space(content.ptr()[pos])) {
        ++pos;
      }
      sentence_begin = pos;
    } else {
      ++pos;
    }
  }
  int64_t sentence_end = end;
  while (sentence_end > sentence_begin && is_ascii_space(content.ptr()[sentence_end - 1])) {
    --sentence_end;
  }
  if (OB_SUCC(ret) && sentence_begin < sentence_end
      && OB_FAIL(spans.push_back(TextSpan(sentence_begin, sentence_end)))) {
    LOG_WARN("failed to append final sentence span", K(ret), K(sentence_begin), K(sentence_end));
  }
  return ret;
}

int append_chunks(ObIAllocator &allocator,
                  const ObString &content,
                  const ObString &heading,
                  const ObIArray<TextSpan> &units,
                  const SplitOptions &options,
                  ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  const int64_t step = options.max_ - options.overlap_;
  for (int64_t unit_begin = 0; OB_SUCC(ret) && unit_begin < units.count();) {
    const int64_t unit_end = std::min(unit_begin + options.max_, units.count());
    const int64_t text_begin = units.at(unit_begin).begin_;
    const int64_t text_end = units.at(unit_end - 1).end_;
    const ObString body(static_cast<int32_t>(text_end - text_begin), content.ptr() + text_begin);
    ObString chunk_text;
    if (heading.empty()) {
      if (OB_FAIL(copy_string(allocator, body, chunk_text))) {
        LOG_WARN("failed to copy document chunk", K(ret));
      }
    } else {
      const int64_t text_length = heading.length() + 1 + body.length();
      char *buffer = static_cast<char *>(allocator.alloc(text_length));
      if (OB_ISNULL(buffer)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate markdown chunk", K(ret), K(text_length));
      } else {
        MEMCPY(buffer, heading.ptr(), heading.length());
        buffer[heading.length()] = '\n';
        MEMCPY(buffer + heading.length() + 1, body.ptr(), body.length());
        chunk_text.assign_ptr(buffer, static_cast<int32_t>(text_length));
      }
    }
    if (OB_SUCC(ret)
        && OB_FAIL(chunks.push_back(ObAISplitDocumentChunk(text_begin,
                                                          text_end - text_begin,
                                                          chunk_text)))) {
      LOG_WARN("failed to append document chunk", K(ret), K(text_begin), K(text_end));
    } else if (unit_end == units.count()) {
      break;
    } else {
      unit_begin += step;
    }
  }
  return ret;
}

int split_range(ObIAllocator &allocator,
                const ObString &content,
                const int64_t begin,
                const int64_t end,
                const ObString &heading,
                const SplitOptions &options,
                ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  ObSEArray<TextSpan, 64> units;
  if (SplitUnit::WORD == options.unit_) {
    ret = collect_word_spans(content, begin, end, units);
  } else {
    ret = collect_sentence_spans(content, begin, end, units);
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("failed to collect document split units", K(ret), K(begin), K(end));
  } else if (OB_FAIL(append_chunks(allocator, content, heading, units, options, chunks))) {
    LOG_WARN("failed to append document chunks", K(ret), K(begin), K(end));
  }
  return ret;
}

bool parse_markdown_heading(const ObString &content,
                            const int64_t line_begin,
                            const int64_t line_end,
                            ObString &heading)
{
  int64_t pos = line_begin;
  while (pos < line_end && '#' == content.ptr()[pos] && pos - line_begin < 6) {
    ++pos;
  }
  const int64_t marker_count = pos - line_begin;
  const bool is_heading = marker_count > 0 && (pos == line_end || ' ' == content.ptr()[pos]);
  if (is_heading) {
    heading.assign_ptr(content.ptr() + line_begin, static_cast<int32_t>(line_end - line_begin));
  }
  return is_heading;
}

int split_markdown(ObIAllocator &allocator,
                   const ObString &content,
                   const SplitOptions &options,
                   ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  ObString heading;
  int64_t section_begin = 0;
  int64_t line_begin = 0;
  while (OB_SUCC(ret) && line_begin <= content.length()) {
    int64_t line_end = line_begin;
    while (line_end < content.length() && '\n' != content.ptr()[line_end]) {
      ++line_end;
    }
    int64_t visible_line_end = line_end;
    if (visible_line_end > line_begin && '\r' == content.ptr()[visible_line_end - 1]) {
      --visible_line_end;
    }
    ObString next_heading;
    if (parse_markdown_heading(content, line_begin, visible_line_end, next_heading)) {
      if (section_begin < line_begin
          && OB_FAIL(split_range(allocator, content, section_begin, line_begin, heading, options, chunks))) {
        LOG_WARN("failed to split markdown section", K(ret), K(section_begin), K(line_begin));
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
  if (OB_SUCC(ret) && section_begin < content.length()
      && OB_FAIL(split_range(allocator, content, section_begin, content.length(), heading, options, chunks))) {
    LOG_WARN("failed to split final markdown section", K(ret), K(section_begin));
  }
  return ret;
}
} // namespace

ObExprAISplitDocument::ObExprAISplitDocument(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator,
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
  if (OB_UNLIKELY(param_num < 1 || param_num > 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, static_cast<int>(STRLEN(N_AI_SPLIT_DOCUMENT)), N_AI_SPLIT_DOCUMENT);
  } else {
    types[0].set_calc_type(ObLongTextType);
    types[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (2 == param_num) {
      types[1].set_calc_type(ObVarcharType);
      types[1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
    type.set_type(ObLongTextType);
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_IMPLICIT);
    type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  }
  return ret;
}

int ObExprAISplitDocument::eval_ai_split_document(const ObExpr &expr,
                                                  ObEvalCtx &ctx,
                                                  ObDatum &result)
{
  UNUSED(expr);
  UNUSED(ctx);
  result.set_null();
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "AI_SPLIT_DOCUMENT must be used in the FROM clause");
  return OB_NOT_SUPPORTED;
}

int ObExprAISplitDocument::split_document(ObIAllocator &allocator,
                                          const ObString &content,
                                          const ObString *parameters,
                                          ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  SplitOptions options;
  if (OB_FAIL(parse_options(allocator, parameters, options))) {
    LOG_WARN("failed to parse AI_SPLIT_DOCUMENT options", K(ret));
  } else if (content.empty()) {
  } else if (DocumentType::MARKDOWN == options.type_) {
    ret = split_markdown(allocator, content, options, chunks);
  } else {
    ret = split_range(allocator, content, 0, content.length(), ObString(), options, chunks);
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
