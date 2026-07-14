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
#include "sql/engine/basic/ob_ai_split_document_utils.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_parse.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

static bool is_json_number(const ObIJsonBase *val)
{
  ObJsonNodeType type = val->json_type();
  return type == ObJsonNodeType::J_INT
         || type == ObJsonNodeType::J_UINT
         || type == ObJsonNodeType::J_DECIMAL;
}

static bool is_sentence_end_char(char c)
{
  return c == '.' || c == '!' || c == '?';
}

static int append_chunk(common::ObIAllocator &alloc,
                        ObAiSplitDocumentIter &result,
                        const ObString &content,
                        int64_t start,
                        int64_t end)
{
  int ret = OB_SUCCESS;
  ObAiSplitDocumentChunk chunk;
  ObString chunk_text;
  if (start < 0 || end <= start || end > content.length()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid chunk range", K(ret), K(start), K(end), K(content.length()));
  } else if (OB_FAIL(ob_write_string(alloc, ObString(end - start, content.ptr() + start), chunk_text))) {
    LOG_WARN("failed to copy chunk text", K(ret));
  } else {
    chunk.chunk_id_ = result.chunks_.count();
    chunk.chunk_offset_ = start;
    chunk.chunk_length_ = end - start;
    chunk.chunk_text_ = chunk_text;
    if (OB_FAIL(result.chunks_.push_back(chunk))) {
      LOG_WARN("failed to push back chunk", K(ret));
    }
  }
  return ret;
}

static int append_chunk_text(common::ObIAllocator &alloc,
                             ObAiSplitDocumentIter &result,
                             const ObString &chunk_text,
                             int64_t offset)
{
  int ret = OB_SUCCESS;
  ObAiSplitDocumentChunk chunk;
  ObString copied;
  if (OB_FAIL(ob_write_string(alloc, chunk_text, copied))) {
    LOG_WARN("failed to copy chunk text", K(ret));
  } else {
    chunk.chunk_id_ = result.chunks_.count();
    chunk.chunk_offset_ = offset;
    chunk.chunk_length_ = copied.length();
    chunk.chunk_text_ = copied;
    if (OB_FAIL(result.chunks_.push_back(chunk))) {
      LOG_WARN("failed to push back chunk", K(ret));
    }
  }
  return ret;
}

static int split_text_by_sentence(const ObString &content,
                                  common::ObIAllocator &alloc,
                                  const int64_t max_units,
                                  ObAiSplitDocumentIter &result)
{
  int ret = OB_SUCCESS;
  ObSEArray<int64_t, 16> starts;
  ObSEArray<int64_t, 16> ends;
  int64_t i = 0;
  int64_t start = 0;
  while (OB_SUCC(ret) && i < content.length()) {
    if (is_sentence_end_char(content[i])) {
      if (OB_FAIL(starts.push_back(start))) {
        LOG_WARN("failed to push sentence start", K(ret));
      } else if (OB_FAIL(ends.push_back(i + 1))) {
        LOG_WARN("failed to push sentence end", K(ret));
      } else {
        while (i + 1 < content.length() && content[i + 1] == ' ') {
          ++i;
        }
        start = i + 1;
      }
    }
    ++i;
  }
  if (OB_SUCC(ret)) {
    for (int64_t j = 0; OB_SUCC(ret) && j < starts.count(); j += max_units) {
      int64_t group_end = MIN(j + max_units, starts.count());
      int64_t chunk_start = starts.at(j);
      int64_t chunk_end = ends.at(group_end - 1);
      if (OB_FAIL(append_chunk(alloc, result, content, chunk_start, chunk_end))) {
        LOG_WARN("failed to append sentence chunk", K(ret));
      }
    }
  }
  return ret;
}

static int split_text_by_word(const ObString &content,
                              common::ObIAllocator &alloc,
                              const int64_t max_units,
                              const int64_t overlap,
                              ObAiSplitDocumentIter &result)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObString, 32> words;
  ObSEArray<int64_t, 32> word_starts;
  int64_t i = 0;
  while (OB_SUCC(ret) && i <= content.length()) {
    while (i < content.length() && content[i] == ' ') {
      ++i;
    }
    if (i >= content.length()) {
      break;
    }
    int64_t word_start = i;
    while (i < content.length() && content[i] != ' ') {
      ++i;
    }
    if (OB_FAIL(words.push_back(ObString(i - word_start, content.ptr() + word_start)))) {
      LOG_WARN("failed to push word", K(ret));
    } else if (OB_FAIL(word_starts.push_back(word_start))) {
      LOG_WARN("failed to push word start", K(ret));
    }
  }
  if (OB_SUCC(ret) && words.count() > 0) {
    const int64_t step = max_units > overlap ? max_units - overlap : 1;
    for (int64_t start = 0; OB_SUCC(ret) && start < words.count(); start += step) {
      int64_t end = MIN(start + max_units, words.count());
      int64_t chunk_start = word_starts.at(start);
      int64_t chunk_end = word_starts.at(end - 1) + words.at(end - 1).length();
      if (OB_FAIL(append_chunk(alloc, result, content, chunk_start, chunk_end))) {
        LOG_WARN("failed to append word chunk", K(ret));
      } else if (end >= words.count()) {
        break;
      }
    }
  }
  return ret;
}

static int split_markdown_by_sentence(const ObString &content,
                                      common::ObIAllocator &alloc,
                                      const int64_t max_units,
                                      ObAiSplitDocumentIter &result)
{
  int ret = OB_SUCCESS;
  ObString header;
  int64_t line_start = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i <= content.length(); ++i) {
    if (i == content.length() || content[i] == '\n') {
      ObString line(i - line_start, content.ptr() + line_start);
      if (line.length() > 0 && line[0] == '#') {
        if (OB_FAIL(ob_write_string(alloc, line, header))) {
          LOG_WARN("failed to copy header", K(ret));
        }
      } else if (line.length() > 0) {
        int64_t sent_start = 0;
        for (int64_t j = 0; OB_SUCC(ret) && j <= line.length(); ++j) {
          if (j == line.length() || is_sentence_end_char(line[j])) {
            if (j > sent_start) {
              int64_t sent_end = j == line.length() ? j : j + 1;
              ObString sentence(sent_end - sent_start, line.ptr() + sent_start);
              ObSqlString chunk_buf;
              int64_t offset = line_start + sent_start;
              if (header.empty()) {
                if (OB_FAIL(append_chunk_text(alloc, result, sentence, offset))) {
                  LOG_WARN("failed to append markdown chunk", K(ret));
                }
              } else if (OB_FAIL(chunk_buf.append(header))) {
                LOG_WARN("failed to append header", K(ret));
              } else if (OB_FAIL(chunk_buf.append("\n"))) {
                LOG_WARN("failed to append newline", K(ret));
              } else if (OB_FAIL(chunk_buf.append(sentence))) {
                LOG_WARN("failed to append sentence", K(ret));
              } else if (OB_FAIL(append_chunk_text(alloc, result, chunk_buf.string(), offset))) {
                LOG_WARN("failed to append markdown chunk", K(ret));
              }
            }
            sent_start = j + 1;
            while (sent_start < line.length() && line[sent_start] == ' ') {
              ++sent_start;
            }
          }
        }
      }
      line_start = i + 1;
    }
  }
  UNUSED(max_units);
  return ret;
}

int ObAiSplitDocumentUtils::split_document(const ObString &content,
                                           const ObAiSplitDocumentParam &param,
                                           common::ObIAllocator &alloc,
                                           ObAiSplitDocumentIter &result)
{
  int ret = OB_SUCCESS;
  result.chunks_.reset();
  if (content.empty()) {
    // empty result
  } else if (param.type_text_) {
    if (param.by_sentence_) {
      if (OB_FAIL(split_text_by_sentence(content, alloc, param.max_units_, result))) {
        LOG_WARN("failed to split text by sentence", K(ret));
      }
    } else if (OB_FAIL(split_text_by_word(content, alloc, param.max_units_, param.overlap_, result))) {
      LOG_WARN("failed to split text by word", K(ret));
    }
  } else if (OB_FAIL(split_markdown_by_sentence(content, alloc, param.max_units_, result))) {
    LOG_WARN("failed to split markdown by sentence", K(ret));
  }
  return ret;
}

int ObAiSplitDocumentUtils::parse_param_json(const ObString &json_str,
                                             ObAiSplitDocumentParam &param)
{
  int ret = OB_SUCCESS;
  param.type_text_ = false;
  param.by_sentence_ = false;
  param.max_units_ = 256;
  param.overlap_ = 0;
  if (json_str.empty()) {
    // use defaults: markdown + word
  } else {
    ObArenaAllocator tmp_alloc;
    ObJsonNode *j_tree = NULL;
    ObIJsonBase *json_doc = NULL;
    if (OB_FAIL(ObJsonParser::get_tree(&tmp_alloc, json_str, j_tree,
                                       ObJsonParser::JSN_RELAXED_FLAG,
                                       ObJsonExprHelper::get_json_max_depth_config()))) {
      LOG_WARN("failed to parse json param", K(ret));
    } else if (OB_ISNULL(json_doc = j_tree)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("json doc is null", K(ret));
    } else if (ObJsonNodeType::J_OBJECT != json_doc->json_type()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("json param should be object", K(ret));
    } else {
      ObString key;
      ObIJsonBase *val = NULL;
      for (int64_t i = 0; OB_SUCC(ret) && i < json_doc->element_count(); ++i) {
        if (OB_FAIL(json_doc->get_object_value(i, key, val))) {
          LOG_WARN("failed to get json object value", K(ret), K(i));
        } else if (0 == key.case_compare("type")) {
          if (OB_ISNULL(val) || ObJsonNodeType::J_STRING != val->json_type()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("type should be string", K(ret));
          } else {
            ObString type_val(val->get_data_length(), val->get_data());
            if (0 == type_val.case_compare("text")) {
              param.type_text_ = true;
            } else if (0 == type_val.case_compare("markdown")) {
              param.type_text_ = false;
            } else {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("invalid type value", K(ret), K(type_val));
            }
          }
        } else if (0 == key.case_compare("by")) {
          if (OB_ISNULL(val) || ObJsonNodeType::J_STRING != val->json_type()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("by should be string", K(ret));
          } else {
            ObString by_val(val->get_data_length(), val->get_data());
            if (0 == by_val.case_compare("word")) {
              param.by_sentence_ = false;
            } else if (0 == by_val.case_compare("sentence")) {
              param.by_sentence_ = true;
            } else {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("invalid by value", K(ret), K(by_val));
            }
          }
        } else if (0 == key.case_compare("max")) {
          if (OB_ISNULL(val) || !is_json_number(val)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("max should be int", K(ret));
          } else {
            param.max_units_ = val->get_int();
            if (param.max_units_ <= 0) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("max should be positive", K(ret), K(param.max_units_));
            }
          }
        } else if (0 == key.case_compare("overlap")) {
          if (OB_ISNULL(val) || !is_json_number(val)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("overlap should be int", K(ret));
          } else {
            param.overlap_ = val->get_int();
            if (param.overlap_ < 0) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("overlap should be non-negative", K(ret), K(param.overlap_));
            }
          }
        }
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
