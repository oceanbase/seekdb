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
#include "sql/engine/basic/ob_ai_split_document_util.h"
#include "common/json_type/ob_json_parse.h"
#include "common/json_type/ob_json_tree.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

static bool is_space_char(char c)
{
  return c == ' ' || c == '\t' || c == '\r';
}

static int copy_chunk_text(common::ObIAllocator &alloc,
                           const char *ptr,
                           int64_t len,
                           ObString &out)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (len <= 0) {
    out.reset();
  } else if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc chunk text", K(ret), K(len));
  } else {
    MEMCPY(buf, ptr, len);
    out.assign_ptr(buf, static_cast<int32_t>(len));
  }
  return ret;
}

static int append_chunk(ObAiSplitDocumentState &state,
                        common::ObIAllocator &alloc,
                        int64_t offset,
                        const char *ptr,
                        int64_t len)
{
  int ret = OB_SUCCESS;
  ObAiSplitChunk chunk;
  chunk.chunk_id_ = state.chunks_.count();
  chunk.chunk_offset_ = offset;
  chunk.chunk_length_ = len;
  if (OB_FAIL(copy_chunk_text(alloc, ptr, len, chunk.chunk_text_))) {
    LOG_WARN("failed to copy chunk text", K(ret));
  } else if (OB_FAIL(state.chunks_.push_back(chunk))) {
    LOG_WARN("failed to push chunk", K(ret));
  }
  return ret;
}

static int split_sentences(const ObString &content,
                           const ObString &prefix,
                           int64_t max_sentences,
                           common::ObIAllocator &alloc,
                           ObAiSplitDocumentState &state)
{
  int ret = OB_SUCCESS;
  const char *data = content.ptr();
  const int64_t total = content.length();
  int64_t pos = 0;
  while (OB_SUCC(ret) && pos < total) {
    while (pos < total && is_space_char(data[pos])) {
      ++pos;
    }
    if (pos >= total) {
      break;
    }
    int64_t sent_start = pos;
    int64_t sent_count = 0;
    ObSEArray<int64_t, 64> sent_starts;
    ObSEArray<int64_t, 64> sent_lens;
    while (OB_SUCC(ret) && pos < total && sent_count < max_sentences) {
      int64_t start = pos;
      while (pos < total && data[pos] != '.') {
        ++pos;
      }
      if (pos < total) {
        ++pos; // include '.'
      }
      int64_t len = pos - start;
      if (len > 0) {
        if (OB_FAIL(sent_starts.push_back(start))) {
          LOG_WARN("failed to push sentence start", K(ret));
        } else if (OB_FAIL(sent_lens.push_back(len))) {
          LOG_WARN("failed to push sentence len", K(ret));
        } else {
          ++sent_count;
        }
      } else if (pos >= total) {
        break;
      }
    }
    if (OB_SUCC(ret) && sent_count > 0) {
      int64_t chunk_len = prefix.length();
      for (int64_t i = 0; i < sent_count; ++i) {
        chunk_len += sent_lens.at(i);
      }
      char *buf = NULL;
      if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(chunk_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc chunk buffer", K(ret), K(chunk_len));
      } else {
        int64_t write_pos = 0;
        if (prefix.length() > 0) {
          MEMCPY(buf + write_pos, prefix.ptr(), prefix.length());
          write_pos += prefix.length();
        }
        for (int64_t i = 0; i < sent_count; ++i) {
          MEMCPY(buf + write_pos, data + sent_starts.at(i), sent_lens.at(i));
          write_pos += sent_lens.at(i);
        }
        if (OB_FAIL(append_chunk(state, alloc, sent_starts.at(0), buf, write_pos))) {
          LOG_WARN("failed to append sentence chunk", K(ret));
        }
      }
    }
  }
  return ret;
}

static int split_words(const ObString &content,
                       int64_t max_words,
                       int64_t overlap,
                       common::ObIAllocator &alloc,
                       ObAiSplitDocumentState &state)
{
  int ret = OB_SUCCESS;
  ObSEArray<int64_t, 128> word_offsets;
  ObSEArray<int64_t, 128> word_lens;
  const char *data = content.ptr();
  const int64_t total = content.length();
  int64_t pos = 0;
  while (pos < total) {
    while (pos < total && is_space_char(data[pos])) {
      ++pos;
    }
    if (pos >= total) {
      break;
    }
    int64_t start = pos;
    while (pos < total && !is_space_char(data[pos])) {
      ++pos;
    }
    if (OB_FAIL(word_offsets.push_back(start))) {
      LOG_WARN("failed to push word offset", K(ret));
      break;
    } else if (OB_FAIL(word_lens.push_back(pos - start))) {
      LOG_WARN("failed to push word len", K(ret));
      break;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (word_offsets.count() == 0) {
    // no words
  } else {
    const int64_t step = max_words > overlap ? max_words - overlap : 1;
    for (int64_t i = 0; OB_SUCC(ret) && i < word_offsets.count(); i += step) {
      const int64_t end = MIN(i + max_words, word_offsets.count());
      if (end <= i) {
        break;
      }
      int64_t chunk_len = 0;
      for (int64_t j = i; j < end; ++j) {
        chunk_len += word_lens.at(j);
        if (j + 1 < end) {
          ++chunk_len; // space between words
        }
      }
      char *buf = NULL;
      if (OB_ISNULL(buf = static_cast<char *>(alloc.alloc(chunk_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc chunk buffer", K(ret), K(chunk_len));
      } else {
        int64_t write_pos = 0;
        for (int64_t j = i; j < end; ++j) {
          MEMCPY(buf + write_pos, data + word_offsets.at(j), word_lens.at(j));
          write_pos += word_lens.at(j);
          if (j + 1 < end) {
            buf[write_pos++] = ' ';
          }
        }
        const int64_t offset = word_offsets.at(i);
        if (OB_FAIL(append_chunk(state, alloc, offset, buf, write_pos))) {
          LOG_WARN("failed to append word chunk", K(ret));
        }
      }
    }
  }
  return ret;
}

static int split_markdown(const ObString &content,
                          const ObAiSplitDocumentParams &params,
                          common::ObIAllocator &alloc,
                          ObAiSplitDocumentState &state)
{
  int ret = OB_SUCCESS;
  const char *data = content.ptr();
  const int64_t total = content.length();
  ObString heading_prefix;
  int64_t pos = 0;
  while (OB_SUCC(ret) && pos < total) {
    int64_t line_end = pos;
    while (line_end < total && data[line_end] != '\n') {
      ++line_end;
    }
    const int64_t line_len = line_end - pos;
    if (line_len > 0 && data[pos] == '#') {
      int64_t heading_len = line_len + (line_end < total ? 1 : 0);
      if (OB_FAIL(copy_chunk_text(alloc, data + pos, heading_len, heading_prefix))) {
        LOG_WARN("failed to copy heading prefix", K(ret));
      }
      pos = line_end < total ? line_end + 1 : total;
    } else {
      int64_t body_start = pos;
      int64_t body_end = line_end;
      pos = line_end < total ? line_end + 1 : total;
      while (pos < total) {
        int64_t next_line_end = pos;
        while (next_line_end < total && data[next_line_end] != '\n') {
          ++next_line_end;
        }
        if ((next_line_end - pos) > 0 && data[pos] == '#') {
          break;
        }
        body_end = next_line_end;
        pos = next_line_end < total ? next_line_end + 1 : total;
      }
      if (body_end > body_start) {
        ObString body;
        body.assign_ptr(data + body_start, static_cast<int32_t>(body_end - body_start));
        if (params.by_ == ObAiSplitDocumentParams::SENTENCE) {
          if (OB_FAIL(split_sentences(body, heading_prefix, params.max_, alloc, state))) {
            LOG_WARN("failed to split markdown sentences", K(ret));
          }
        } else if (OB_FAIL(split_words(body, params.max_, params.overlap_, alloc, state))) {
          LOG_WARN("failed to split markdown words", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObAiSplitDocumentUtil::parse_params(const ObString &params_json, ObAiSplitDocumentParams &params)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc;
  ObJsonNode *json_tree = NULL;
  ObString json_str = params_json;
  if (json_str.empty()) {
    json_str = ObString::make_string("{}");
  }
  uint32_t parse_flag = ObJsonParser::JSN_RELAXED_FLAG;
  if (OB_FAIL(ObJsonParser::get_tree(&tmp_alloc, json_str, json_tree, parse_flag))) {
    LOG_WARN("failed to parse split params json", K(ret), K(json_str));
  } else if (OB_ISNULL(json_tree) || json_tree->json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("split params is not json object", K(ret));
  } else {
    ObJsonObject *json_obj = static_cast<ObJsonObject *>(json_tree);
    ObIJsonBase *type_node = NULL;
    ObIJsonBase *by_node = NULL;
    ObIJsonBase *max_node = NULL;
    ObIJsonBase *overlap_node = NULL;
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS == (tmp_ret = json_obj->get_object_value(ObString("type"), type_node))
        && OB_NOT_NULL(type_node) && type_node->json_type() == ObJsonNodeType::J_STRING) {
      ObJsonString *type_str = static_cast<ObJsonString *>(type_node);
      ObString type_val = type_str->get_str();
      if (type_val.case_compare_equal("text")) {
        params.type_ = ObAiSplitDocumentParams::TEXT;
      } else if (type_val.case_compare_equal("markdown")) {
        params.type_ = ObAiSplitDocumentParams::MARKDOWN;
      }
    }
    if (OB_SUCCESS == (tmp_ret = json_obj->get_object_value(ObString("by"), by_node))
        && OB_NOT_NULL(by_node) && by_node->json_type() == ObJsonNodeType::J_STRING) {
      ObJsonString *by_str = static_cast<ObJsonString *>(by_node);
      ObString by_val = by_str->get_str();
      if (by_val.case_compare_equal("sentence")) {
        params.by_ = ObAiSplitDocumentParams::SENTENCE;
      } else if (by_val.case_compare_equal("word")) {
        params.by_ = ObAiSplitDocumentParams::WORD;
      }
    }
    if (OB_SUCCESS == (tmp_ret = json_obj->get_object_value(ObString("max"), max_node))
        && OB_NOT_NULL(max_node)) {
      int64_t max_val = 0;
      if (max_node->json_type() == ObJsonNodeType::J_INT) {
        max_val = max_node->get_int();
      } else if (max_node->json_type() == ObJsonNodeType::J_UINT) {
        max_val = static_cast<int64_t>(max_node->get_uint());
      }
      if (max_val > 0) {
        params.max_ = max_val;
      }
    }
    if (OB_SUCCESS == (tmp_ret = json_obj->get_object_value(ObString("overlap"), overlap_node))
        && OB_NOT_NULL(overlap_node)) {
      int64_t overlap_val = 0;
      if (overlap_node->json_type() == ObJsonNodeType::J_INT) {
        overlap_val = overlap_node->get_int();
      } else if (overlap_node->json_type() == ObJsonNodeType::J_UINT) {
        overlap_val = static_cast<int64_t>(overlap_node->get_uint());
      }
      if (overlap_val >= 0) {
        params.overlap_ = overlap_val;
      }
    }
  }
  return ret;
}

int ObAiSplitDocumentUtil::split_document(const ObString &content,
                                          const ObAiSplitDocumentParams &params,
                                          common::ObIAllocator &alloc,
                                          ObAiSplitDocumentState &state)
{
  int ret = OB_SUCCESS;
  state.chunks_.reuse();
  state.current_idx_ = -1;
  if (content.empty()) {
    ret = OB_ITER_END;
  } else if (params.type_ == ObAiSplitDocumentParams::MARKDOWN) {
    if (OB_FAIL(split_markdown(content, params, alloc, state))) {
      LOG_WARN("failed to split markdown document", K(ret));
    }
  } else if (params.by_ == ObAiSplitDocumentParams::SENTENCE) {
    ObString empty_prefix;
    if (OB_FAIL(split_sentences(content, empty_prefix, params.max_, alloc, state))) {
      LOG_WARN("failed to split text sentences", K(ret));
    }
  } else if (OB_FAIL(split_words(content, params.max_, params.overlap_, alloc, state))) {
    LOG_WARN("failed to split text words", K(ret));
  }
  if (OB_SUCC(ret) && state.chunks_.count() == 0) {
    ret = OB_ITER_END;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
