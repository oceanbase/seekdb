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

#include "ob_expr_ai_split_document.h"

#include <cctype>
#include <climits>

#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_se_array.h"
#include "lib/utility/utility.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

namespace
{

enum ObAISplitDocumentType
{
  AI_DOC_TEXT = 0,
  AI_DOC_MARKDOWN
};

enum ObAISplitDocumentBy
{
  AI_SPLIT_BY_WORD = 0,
  AI_SPLIT_BY_SENTENCE
};

struct ObAISplitDocumentParams
{
  ObAISplitDocumentParams()
      : type_(AI_DOC_MARKDOWN),
        by_(AI_SPLIT_BY_WORD),
        max_(256),
        overlap_(0)
  {
  }

  ObAISplitDocumentType type_;
  ObAISplitDocumentBy by_;
  int64_t max_;
  int64_t overlap_;
};

struct ObAISplitUnit
{
  ObAISplitUnit()
      : begin_(0), end_(0)
  {
  }

  ObAISplitUnit(const int64_t begin, const int64_t end)
      : begin_(begin), end_(end)
  {
  }

  int64_t begin_;
  int64_t end_;

  TO_STRING_KV(K_(begin), K_(end));
};

bool is_ascii_space(const char ch)
{
  return 0 != std::isspace(static_cast<unsigned char>(ch));
}

bool is_all_space(const ObString &str)
{
  bool all_space = true;
  for (int64_t i = 0; all_space && i < str.length(); ++i) {
    all_space = is_ascii_space(str.ptr()[i]);
  }
  return all_space;
}

int get_json_int(const ObJsonNode &node, int64_t &value)
{
  int ret = OB_SUCCESS;

  if (node.json_type() == ObJsonNodeType::J_INT) {
    value = static_cast<const ObJsonInt &>(node).value();
  } else if (node.json_type() == ObJsonNodeType::J_UINT) {
    const uint64_t uint_value = static_cast<const ObJsonUint &>(node).value();
    if (OB_UNLIKELY(uint_value > static_cast<uint64_t>(INT64_MAX))) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("integer parameter overflow", K(ret), K(uint_value));
    } else {
      value = static_cast<int64_t>(uint_value);
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("integer parameter expected", K(ret), K(node.json_type()));
  }

  return ret;
}

int parse_parameters(const ObString &parameters,
                     ObAISplitDocumentParams &params)
{
  int ret = OB_SUCCESS;

  if (parameters.empty() || is_all_space(parameters)) {
    // Keep defaults.
  } else {
    ObArenaAllocator json_allocator(ObModIds::OB_SQL_EXPR_CALC);
    ObIJsonBase *json_base = NULL;

    if (OB_FAIL(ObJsonBaseFactory::get_json_base(
            &json_allocator, parameters, ObJsonInType::JSON_TREE,
            ObJsonInType::JSON_TREE, json_base))) {
      LOG_WARN("failed to parse ai split document parameters", K(ret));
    } else if (OB_ISNULL(json_base)
               || json_base->json_type() != ObJsonNodeType::J_OBJECT) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("parameters must be a json object", K(ret), KP(json_base));
    } else {
      ObJsonObject *object = static_cast<ObJsonObject *>(json_base);

      for (uint64_t i = 0; OB_SUCC(ret) && i < object->element_count(); ++i) {
        ObString key;
        ObJsonNode *value = NULL;

        if (OB_FAIL(object->get_value_by_idx(i, key, value))) {
          LOG_WARN("failed to get split parameter", K(ret), K(i));
        } else if (OB_ISNULL(value)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("split parameter value is null", K(ret), K(key));
        } else if (key == "type" || key == "by") {
          if (value->json_type() != ObJsonNodeType::J_STRING) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("string parameter expected", K(ret), K(key), K(value->json_type()));
          } else {
            const ObString string_value =
                static_cast<ObJsonString *>(value)->value();
            if (key == "type" && string_value == "text") {
              params.type_ = AI_DOC_TEXT;
            } else if (key == "type" && string_value == "markdown") {
              params.type_ = AI_DOC_MARKDOWN;
            } else if (key == "by" && string_value == "word") {
              params.by_ = AI_SPLIT_BY_WORD;
            } else if (key == "by" && string_value == "sentence") {
              params.by_ = AI_SPLIT_BY_SENTENCE;
            } else {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("invalid string parameter", K(ret), K(key), K(string_value));
            }
          }
        } else if (key == "max") {
          if (OB_FAIL(get_json_int(*value, params.max_))) {
            LOG_WARN("failed to parse max parameter", K(ret));
          }
        } else if (key == "overlap") {
          if (OB_FAIL(get_json_int(*value, params.overlap_))) {
            LOG_WARN("failed to parse overlap parameter", K(ret));
          }
        } else {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("unsupported ai split document parameter", K(ret), K(key));
        }
      }
    }

    if (OB_SUCC(ret) && OB_UNLIKELY(params.max_ <= 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("max must be greater than zero", K(ret), K(params.max_));
    } else if (OB_SUCC(ret)
               && OB_UNLIKELY(params.overlap_ < 0
                              || params.overlap_ >= params.max_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("overlap must satisfy 0 <= overlap < max",
               K(ret), K(params.overlap_), K(params.max_));
    }
  }

  return ret;
}

int sentence_mark_length(const ObString &text,
                         const int64_t pos,
                         const int64_t end)
{
  int mark_length = 0;

  if (pos < end) {
    const unsigned char ch = static_cast<unsigned char>(text[pos]);

    if (ch == '.' || ch == '!' || ch == '?') {
      mark_length = 1;
    } else if (pos + 2 < end) {
      const unsigned char ch1 = static_cast<unsigned char>(text[pos + 1]);
      const unsigned char ch2 = static_cast<unsigned char>(text[pos + 2]);

      if ((ch == 0xE3 && ch1 == 0x80 && ch2 == 0x82)
          || (ch == 0xEF && ch1 == 0xBC && ch2 == 0x81)
          || (ch == 0xEF && ch1 == 0xBC && ch2 == 0x9F)) {
        mark_length = 3;
      }
    }
  }

  return mark_length;
}

int collect_word_units(const ObString &text,
                       const int64_t begin,
                       const int64_t end,
                       ObIArray<ObAISplitUnit> &units)
{
  int ret = OB_SUCCESS;
  int64_t pos = begin;

  while (OB_SUCC(ret) && pos < end) {
    while (pos < end && is_ascii_space(text[pos])) {
      ++pos;
    }

    const int64_t unit_begin = pos;

    while (pos < end && !is_ascii_space(text[pos])) {
      ++pos;
    }

    if (unit_begin < pos) {
      if (OB_FAIL(units.push_back(ObAISplitUnit(unit_begin, pos)))) {
        LOG_WARN("failed to append word unit", K(ret));
      }
    }
  }

  return ret;
}

int collect_sentence_units(const ObString &text,
                           const int64_t begin,
                           const int64_t end,
                           ObIArray<ObAISplitUnit> &units)
{
  int ret = OB_SUCCESS;
  int64_t pos = begin;

  while (OB_SUCC(ret) && pos < end) {
    while (pos < end && is_ascii_space(text[pos])) {
      ++pos;
    }

    if (pos >= end) {
      break;
    }

    const int64_t unit_begin = pos;
    int64_t unit_end = end;
    bool found_mark = false;

    while (pos < end && !found_mark) {
      const int mark_length = sentence_mark_length(text, pos, end);

      if (mark_length > 0) {
        pos += mark_length;
        unit_end = pos;
        found_mark = true;
      } else {
        ++pos;
      }
    }

    if (!found_mark) {
      unit_end = end;
      while (unit_end > unit_begin && is_ascii_space(text[unit_end - 1])) {
        --unit_end;
      }
      pos = end;
    }

    if (unit_begin < unit_end) {
      if (OB_FAIL(units.push_back(ObAISplitUnit(unit_begin, unit_end)))) {
        LOG_WARN("failed to append sentence unit", K(ret));
      }
    }
  }

  return ret;
}

int append_chunk(const ObString &source,
                 const int64_t begin,
                 const int64_t end,
                 const ObString &heading,
                 ObIAllocator &allocator,
                 ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  const int64_t separator_length = heading.empty() ? 0 : 1;
  int64_t body_length = 0;
  int64_t text_length = 0;

  if (OB_UNLIKELY(begin < 0 || end < begin || end > source.length())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid document chunk range",
             K(ret), K(begin), K(end), K(source.length()));
  } else if (FALSE_IT(body_length = end - begin)) {
  } else if (OB_UNLIKELY(heading.length() > INT32_MAX - separator_length
                         || body_length > INT32_MAX - heading.length()
                                              - separator_length)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("chunk text is too large",
             K(ret), K(heading.length()), K(body_length));
  } else {
    text_length = heading.length() + separator_length + body_length;
    char *buffer = NULL;

    if (text_length > 0) {
      buffer = static_cast<char *>(allocator.alloc(text_length));

      if (OB_ISNULL(buffer)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate chunk text", K(ret), K(text_length));
      } else {
        int64_t pos = 0;
        if (!heading.empty()) {
          MEMCPY(buffer, heading.ptr(), heading.length());
          pos += heading.length();
          buffer[pos++] = '\n';
        }
        if (body_length > 0) {
          MEMCPY(buffer + pos, source.ptr() + begin, body_length);
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObAISplitDocumentChunk chunk;
      chunk.chunk_id_ = chunks.count();
      chunk.chunk_offset_ = begin;
      chunk.chunk_length_ = body_length;
      chunk.chunk_text_.assign_ptr(buffer, static_cast<int32_t>(text_length));

      if (OB_FAIL(chunks.push_back(chunk))) {
        LOG_WARN("failed to append document chunk", K(ret));
        if (OB_NOT_NULL(buffer)) {
          allocator.free(buffer);
        }
      }
    }
  }

  return ret;
}

int build_chunks_from_units(
    const ObString &source,
    const ObIArray<ObAISplitUnit> &units,
    const ObString &heading,
    const ObAISplitDocumentParams &params,
    ObIAllocator &allocator,
    ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;

  if (units.count() > 0) {
    const int64_t step = params.max_ - params.overlap_;

    for (int64_t start = 0;
         OB_SUCC(ret) && start < units.count();
         start += step) {
      const int64_t remaining = units.count() - start;
      const int64_t finish =
          params.max_ >= remaining ? units.count() : start + params.max_;

      if (OB_FAIL(append_chunk(source,
                               units.at(start).begin_,
                               units.at(finish - 1).end_,
                               heading,
                               allocator,
                               chunks))) {
        LOG_WARN("failed to build document chunk", K(ret));
      }

      if (finish >= units.count()) {
        break;
      }
    }
  }

  return ret;
}

int split_range(const ObString &source,
                const int64_t begin,
                const int64_t end,
                const ObString &heading,
                const ObAISplitDocumentParams &params,
                ObIAllocator &allocator,
                ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObAISplitUnit, 64> units;

  if (OB_UNLIKELY(begin < 0
                  || end < begin
                  || end > source.length())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid document split range",
             K(ret), K(begin), K(end), K(source.length()));
  } else {
    if (params.by_ == AI_SPLIT_BY_SENTENCE) {
      if (OB_FAIL(collect_sentence_units(source, begin, end, units))) {
        LOG_WARN("failed to collect sentence units", K(ret));
      }
    } else {
      if (OB_FAIL(collect_word_units(source, begin, end, units))) {
        LOG_WARN("failed to collect word units", K(ret));
      }
    }

    if (OB_SUCC(ret)
        && OB_FAIL(build_chunks_from_units(source, units, heading, params,
                                           allocator, chunks))) {
      LOG_WARN("failed to split document range", K(ret));
    }
  }

  return ret;
}

bool parse_markdown_heading(const ObString &source,
                            const int64_t line_begin,
                            const int64_t line_end,
                            ObString &heading)
{
  bool is_heading = false;
  int64_t pos = line_begin;
  int64_t leading_spaces = 0;

  while (pos < line_end && source[pos] == ' ' && leading_spaces < 4) {
    ++pos;
    ++leading_spaces;
  }

  if (leading_spaces <= 3 && pos < line_end && source[pos] == '#') {
    const int64_t heading_begin = pos;
    int64_t hash_count = 0;

    while (pos < line_end && source[pos] == '#' && hash_count < 7) {
      ++pos;
      ++hash_count;
    }

    if (hash_count >= 1
        && hash_count <= 6
        && (pos == line_end || source[pos] == ' ' || source[pos] == '\t')) {
      int64_t trimmed_end = line_end;
      while (trimmed_end > heading_begin
             && (source[trimmed_end - 1] == ' '
                 || source[trimmed_end - 1] == '\t'
                 || source[trimmed_end - 1] == '\r')) {
        --trimmed_end;
      }

      heading.assign_ptr(source.ptr() + heading_begin,
                         static_cast<int32_t>(trimmed_end - heading_begin));
      is_heading = true;
    }
  }

  return is_heading;
}

int split_markdown(const ObString &source,
                   const ObAISplitDocumentParams &params,
                   ObIAllocator &allocator,
                   ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t body_begin = 0;
  ObString current_heading;

  while (OB_SUCC(ret) && pos <= source.length()) {
    const int64_t line_begin = pos;
    int64_t line_end = pos;

    while (line_end < source.length()
           && source[line_end] != '\n') {
      ++line_end;
    }

    ObString heading;
    if (parse_markdown_heading(source, line_begin, line_end, heading)) {
      if (body_begin < line_begin) {
        if (OB_FAIL(split_range(source,
                                body_begin,
                                line_begin,
                                current_heading,
                                params,
                                allocator,
                                chunks))) {
          LOG_WARN("failed to split markdown section", K(ret));
        }
      }

      current_heading = heading;
      body_begin = line_end < source.length() ? line_end + 1 : line_end;
    }

    if (line_end >= source.length()) {
      break;
    }

    pos = line_end + 1;
  }

  if (OB_SUCC(ret)
      && body_begin < source.length()) {
    if (OB_FAIL(split_range(source,
                            body_begin,
                            source.length(),
                            current_heading,
                            params,
                            allocator,
                            chunks))) {
      LOG_WARN("failed to split final markdown section", K(ret));
    }
  }

  return ret;
}

} // namespace

ObExprAISplitDocument::ObExprAISplitDocument(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_AI_SPLIT_DOCUMENT,
                         N_AI_SPLIT_DOCUMENT,
                         ONE_OR_TWO,
                         NOT_VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

ObExprAISplitDocument::~ObExprAISplitDocument()
{
}

int ObExprAISplitDocument::calc_result_typeN(
    ObExprResType &type,
    ObExprResType *types_array,
    int64_t param_num,
    common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(param_num < 1 || param_num > 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("ai_split_document requires one or two arguments",
             K(ret), K(param_num));
  } else if (OB_ISNULL(types_array)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("types array is null", K(ret));
  } else if (!ob_is_string_tc(types_array[0].get_type())
             && ObNullType != types_array[0].get_type()) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid document content type",
             K(ret), K(types_array[0].get_type()));
  } else if (2 == param_num
             && !ob_is_string_tc(types_array[1].get_type())
             && ObNullType != types_array[1].get_type()) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid document parameters type",
             K(ret), K(types_array[1].get_type()));
  } else {
    type.set_varchar();
    type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_collation_level(CS_LEVEL_IMPLICIT);
    type.set_length(1);

    types_array[0].set_calc_type(ObVarcharType);
    types_array[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    types_array[0].set_calc_collation_level(CS_LEVEL_IMPLICIT);

    if (2 == param_num) {
      types_array[1].set_calc_type(ObVarcharType);
      types_array[1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      types_array[1].set_calc_collation_level(CS_LEVEL_IMPLICIT);
    }
  }

  UNUSED(type_ctx);
  return ret;
}

int ObExprAISplitDocument::eval_ai_split_document(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    ObDatum &res)
{
  UNUSED(expr);
  UNUSED(ctx);
  res.set_null();

  const int ret = OB_NOT_SUPPORTED;
  LOG_WARN("ai_split_document can only be used as a table function", K(ret));
  return ret;
}

int ObExprAISplitDocument::cg_expr(
    ObExprCGCtx &expr_cg_ctx,
    const ObRawExpr &raw_expr,
    ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);

  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(rt_expr.arg_cnt_ < 1 || rt_expr.arg_cnt_ > 2)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid ai_split_document runtime argument count",
             K(ret), K(rt_expr.arg_cnt_));
  } else {
    rt_expr.eval_func_ = ObExprAISplitDocument::eval_ai_split_document;
  }

  return ret;
}

int ObExprAISplitDocument::split_document(
    const ObString &content,
    const ObString &parameters,
    ObIAllocator &allocator,
    ObIArray<ObAISplitDocumentChunk> &chunks)
{
  int ret = OB_SUCCESS;
  ObAISplitDocumentParams params;

  chunks.reset();

  if (OB_UNLIKELY(content.length() < 0
                  || (content.length() > 0 && OB_ISNULL(content.ptr())))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid document content", K(ret), K(content.length()));
  } else if (OB_UNLIKELY(parameters.length() < 0
                         || (parameters.length() > 0
                             && OB_ISNULL(parameters.ptr())))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid document parameters",
             K(ret), K(parameters.length()));
  } else if (OB_FAIL(parse_parameters(parameters, params))) {
    LOG_WARN("failed to parse split parameters", K(ret));
  } else if (!content.empty()) {
    if (params.type_ == AI_DOC_MARKDOWN) {
      if (OB_FAIL(split_markdown(content, params, allocator, chunks))) {
        LOG_WARN("failed to split markdown document", K(ret));
      }
    } else if (OB_FAIL(split_range(content,
                                   0,
                                   content.length(),
                                   ObString(),
                                   params,
                                   allocator,
                                   chunks))) {
      LOG_WARN("failed to split text document", K(ret));
    }
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
