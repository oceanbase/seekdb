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

#define USING_LOG_PREFIX LIB

#include "lib/ai_split_document/ob_ai_split_document.h"
#include "lib/ai_split_document/ob_ai_split_document_util.h"
#include "lib/alloc/malloc_hook.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/oblog/ob_log_module.h"
#include <cctype>
#include <limits>

namespace oceanbase
{
namespace common
{

namespace
{
bool is_space(const char ch)
{
  return 0 != std::isspace(static_cast<unsigned char>(ch));
}
} // namespace

#define EXTRACT_JSON_STRING(json_key, string_value, process)                         \
  if (0 == elem.first.case_compare(json_key)) {                                     \
    if (elem.second->json_type() != ObJsonNodeType::J_STRING) {                     \
      ret = OB_INVALID_ARGUMENT;                                                     \
      LOG_WARN("invalid document split parameter type", K(ret), K(elem.first),      \
               K(elem.second->json_type()));                                         \
      FORWARD_USER_ERROR(ret, "parameter " json_key " must be a string");          \
    } else {                                                                         \
      string_value.assign_ptr(elem.second->get_data(), elem.second->get_data_length()); \
      process;                                                                       \
    }                                                                                \
  } else

#define EXTRACT_JSON_INTEGER(json_key, integer_value)                               \
  if (0 == elem.first.case_compare(json_key)) {                                     \
    if (elem.second->json_type() != ObJsonNodeType::J_INT                           \
        && elem.second->json_type() != ObJsonNodeType::J_UINT) {                    \
      ret = OB_INVALID_ARGUMENT;                                                     \
      LOG_WARN("invalid document split parameter type", K(ret), K(elem.first),      \
               K(elem.second->json_type()));                                         \
      FORWARD_USER_ERROR(ret, "parameter " json_key " must be an integer");       \
    } else if (elem.second->json_type() == ObJsonNodeType::J_UINT) {                \
      const uint64_t uint_value = elem.second->get_uint();                           \
      if (uint_value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) { \
        ret = OB_INVALID_ARGUMENT;                                                    \
        LOG_WARN("document split parameter is out of range", K(ret), K(elem.first), \
                 K(uint_value));                                                      \
        FORWARD_USER_ERROR(ret, "parameter " json_key " is out of range");       \
      } else {                                                                        \
        integer_value = static_cast<int64_t>(uint_value);                            \
      }                                                                               \
    } else {                                                                          \
      integer_value = elem.second->get_int();                                        \
    }                                                                                \
  } else

int ObAiSplitDocInput::init(const ObString &content, const ObIJsonBase *params_node)
{
  int ret = OB_SUCCESS;
  content_ = content;
  if (OB_FAIL(params_.init(params_node))) {
    LOG_WARN("failed to initialize document split parameters", K(ret));
  }
  return ret;
}

int ObAiSplitDocParams::init(const ObIJsonBase *params_node)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_ISNULL(params_node)) {
    // Use defaults.
  } else if (params_node->json_type() != ObJsonNodeType::J_OBJECT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("document split parameters must be a JSON object", K(ret),
             K(params_node->json_type()));
    FORWARD_USER_ERROR(ret, "parameters must be a JSON object");
  } else {
    ObString type_str;
    ObString by_str;
    JsonObjectIterator iter = params_node->object_iterator();
    while (OB_SUCC(ret) && !iter.end()) {
      ObJsonObjPair elem;
      if (OB_FAIL(iter.get_elem(elem))) {
        LOG_WARN("failed to read document split parameter", K(ret));
      } else {
        EXTRACT_JSON_STRING("type", type_str, parse_type(type_str))
        EXTRACT_JSON_STRING("by", by_str, parse_by(by_str))
        EXTRACT_JSON_INTEGER("max", max_)
        EXTRACT_JSON_INTEGER("overlap", overlap_)
        {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("unknown document split parameter", K(ret), K(elem.first));
          FORWARD_USER_ERROR_MSG(ret, "unknown parameter '%.*s'",
                                 elem.first.length(), elem.first.ptr());
        }
      }
      iter.next();
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(check_validity())) {
    LOG_WARN("invalid document split parameters", K(ret), K(*this));
  }
  return ret;
}

int ObAiSplitDocParams::parse_type(const ObString &type_str)
{
  int ret = OB_SUCCESS;
  if (0 == type_str.case_compare("text")) {
    type_ = ObAiSplitContentType::TEXT;
  } else if (0 == type_str.case_compare("markdown")) {
    type_ = ObAiSplitContentType::MARKDOWN;
  } else {
    type_ = ObAiSplitContentType::MAX_CONTENT_TYPE;
  }
  return ret;
}

int ObAiSplitDocParams::parse_by(const ObString &by_str)
{
  int ret = OB_SUCCESS;
  if (0 == by_str.case_compare("word")) {
    by_ = ObAiSplitByUnit::WORD;
  } else if (0 == by_str.case_compare("sentence")) {
    by_ = ObAiSplitByUnit::SENTENCE;
  } else {
    by_ = ObAiSplitByUnit::MAX_UNIT_TYPE;
  }
  return ret;
}

int ObAiSplitDocParams::check_validity() const
{
  int ret = OB_SUCCESS;
  if (max_ <= 0 || max_ > 1000) {
    ret = OB_INVALID_ARGUMENT;
    FORWARD_USER_ERROR(ret, "parameter max must be between 1 and 1000");
  } else if (overlap_ < 0 || overlap_ > max_ / 2) {
    ret = OB_INVALID_ARGUMENT;
    FORWARD_USER_ERROR(ret, "parameter overlap must be between 0 and max/2");
  } else if (by_ == ObAiSplitByUnit::MAX_UNIT_TYPE) {
    ret = OB_INVALID_ARGUMENT;
    FORWARD_USER_ERROR(ret, "parameter by must be 'word' or 'sentence'");
  } else if (type_ == ObAiSplitContentType::MAX_CONTENT_TYPE) {
    ret = OB_INVALID_ARGUMENT;
    FORWARD_USER_ERROR(ret, "parameter type must be 'text' or 'markdown'");
  }
  return ret;
}

int ObAiSplitDocAdapter::init(ObIAllocator &allocator, const ObString &content,
                              const ObAiSplitDocParams &params)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("document split adapter already initialized", K(ret));
  } else {
    reset();
    allocator_ = &allocator;
    if (OB_FAIL(ObAiSplitDocumentUtil::create_doc_split_iterator(params, allocator,
                                                                 iterator_))) {
      LOG_WARN("failed to create document split iterator", K(ret));
    } else if (OB_ISNULL(iterator_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("document split iterator is null", K(ret));
    } else if (OB_FAIL(iterator_->open(content, allocator, params))) {
      LOG_WARN("failed to open document split iterator", K(ret));
    } else {
      is_inited_ = true;
    }
    if (OB_FAIL(ret)) {
      reset();
    }
  }
  return ret;
}

int ObAiSplitDocAdapter::get_next_row()
{
  int ret = OB_SUCCESS;
  if (!is_inited_ || OB_ISNULL(iterator_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("document split adapter is not initialized", K(ret));
  } else if (OB_FAIL(iterator_->get_next_row(cur_chunk_))) {
    if (ret != OB_ITER_END) {
      LOG_WARN("failed to get next document chunk", K(ret));
    }
  } else {
    ++cur_idx_;
  }
  return ret;
}

void ObAiSplitDocAdapter::reset()
{
  if (OB_NOT_NULL(iterator_)) {
    iterator_->close();
    if (OB_NOT_NULL(allocator_)) {
      OB_DELETEx(ObDocSplitIterator, allocator_, iterator_);
    } else {
      iterator_ = nullptr;
    }
  }
  allocator_ = nullptr;
  is_inited_ = false;
  cur_idx_ = -1;
  cur_chunk_.reset();
}

int ObTextSplitIterator::open(const ObString &content, ObIAllocator &allocator,
                              const ObAiSplitDocParams &params)
{
  UNUSED(allocator);
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("text split iterator already initialized", K(ret));
  } else {
    content_ = content;
    params_.assign(params);
    chunk_id_ = 0;
    chunk_start_offset_ = 0;
    next_chunk_start_ = 0;
    unit_since_window_start_ = 0;
    current_boundary_ = 0;
    is_done_ = false;

    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("AiSplitDoc"));
    UErrorCode status = U_ZERO_ERROR;
    if (params_.by_ == ObAiSplitByUnit::WORD) {
      bi_ = icu::BreakIterator::createWordInstance(icu::Locale::getDefault(), status);
    } else if (params_.by_ == ObAiSplitByUnit::SENTENCE) {
      bi_ = icu::BreakIterator::createSentenceInstance(icu::Locale::getDefault(), status);
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid split unit", K(ret), K(params_.by_));
    }
    if (OB_SUCC(ret) && (U_FAILURE(status) || OB_ISNULL(bi_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to create ICU BreakIterator", K(ret), K(status));
    }
    if (OB_SUCC(ret)) {
      status = U_ZERO_ERROR;
      utext_ = utext_openUTF8(nullptr, content_.ptr(), content_.length(), &status);
      if (U_FAILURE(status) || OB_ISNULL(utext_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create ICU UText", K(ret), K(status));
      }
    }
    if (OB_SUCC(ret)) {
      status = U_ZERO_ERROR;
      bi_->setText(utext_, status);
      if (U_FAILURE(status)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to bind UText to BreakIterator", K(ret), K(status));
      }
    }
    if (OB_SUCC(ret)) {
      is_inited_ = true;
    } else {
      close();
    }
  }
  return ret;
}

int ObTextSplitIterator::get_next_row(ObAiSplitDocChunk &chunk)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("text split iterator is not initialized", K(ret));
  } else if (is_done_) {
    ret = OB_ITER_END;
  } else {
    chunk.reset();
    const int64_t max_unit = params_.max_;
    const int64_t overlap_unit = params_.overlap_;
    const int64_t chunk_step = max_unit - overlap_unit;
    int64_t window_start = chunk_start_offset_;
    int64_t unit_count = (0 == chunk_start_offset_) ? 0 : overlap_unit;
    int64_t boundary = current_boundary_;
    bool chunk_found = false;

    while (boundary != icu::BreakIterator::DONE && !chunk_found) {
      boundary = bi_->next();
      if (boundary != icu::BreakIterator::DONE) {
        if ((params_.by_ == ObAiSplitByUnit::WORD
             && bi_->getRuleStatus() != UBRK_WORD_NONE)
            || params_.by_ == ObAiSplitByUnit::SENTENCE) {
          ++unit_count;
          ++unit_since_window_start_;
        }
        if (unit_count >= max_unit) {
          while (window_start < boundary && window_start < content_.length()
                 && is_space(content_[window_start])) {
            ++window_start;
          }
          int64_t window_end = boundary;
          while (window_end > window_start && is_space(content_[window_end - 1])) {
            --window_end;
          }
          if (window_start < window_end) {
            chunk.init(chunk_id_++, window_start, window_end - window_start,
                       ObString(window_end - window_start, content_.ptr() + window_start));
            chunk_found = true;
          }
          unit_count = overlap_unit;
          if (0 == overlap_unit) {
            next_chunk_start_ = boundary;
          }
          window_start = next_chunk_start_;
          chunk_start_offset_ = window_start;
        }
        if (unit_since_window_start_ >= chunk_step) {
          next_chunk_start_ = boundary;
          unit_since_window_start_ = 0;
        }
      }
    }
    current_boundary_ = boundary;

    if (!chunk_found && boundary == icu::BreakIterator::DONE) {
      if (unit_count > overlap_unit || 0 == chunk_id_) {
        int64_t end = content_.length();
        while (window_start < end && is_space(content_[window_start])) {
          ++window_start;
        }
        while (end > window_start && is_space(content_[end - 1])) {
          --end;
        }
        if (window_start < end) {
          chunk.init(chunk_id_++, window_start, end - window_start,
                     ObString(end - window_start, content_.ptr() + window_start));
          chunk_found = true;
        }
      }
      is_done_ = true;
    }
    if (!chunk_found) {
      ret = OB_ITER_END;
    }
  }
  return ret;
}

int ObTextSplitIterator::close()
{
  int ret = OB_SUCCESS;
  is_inited_ = false;
  content_.reset();
  params_.reset();
  chunk_id_ = 0;
  chunk_start_offset_ = 0;
  next_chunk_start_ = 0;
  unit_since_window_start_ = 0;
  current_boundary_ = 0;
  is_done_ = false;
  if (OB_NOT_NULL(bi_)) {
    delete bi_;
    bi_ = nullptr;
  }
  if (OB_NOT_NULL(utext_)) {
    utext_close(utext_);
    utext_ = nullptr;
  }
  return ret;
}

int ObMarkdownSplitIterator::open(const ObString &content, ObIAllocator &allocator,
                                  const ObAiSplitDocParams &params)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("markdown split iterator already initialized", K(ret));
  } else {
    allocator_ = &allocator;
    content_ = content;
    params_.assign(params);
    chunk_id_ = 0;
    section_start_offset_ = 0;
    section_title_.reset();
    section_content_.reset();
    is_done_ = false;
    row_alloc_.clear();
    void *buf = allocator.alloc(sizeof(ObTextSplitIterator));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate nested text split iterator", K(ret));
    } else {
      iterator_ = new (buf) ObTextSplitIterator();
      is_inited_ = true;
    }
  }
  return ret;
}

int ObMarkdownSplitIterator::get_next_row(ObAiSplitDocChunk &chunk)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("markdown split iterator is not initialized", K(ret));
  } else if (is_done_) {
    ret = OB_ITER_END;
  } else {
    if (!section_title_.empty()) {
      row_alloc_.clear();
    }
    chunk.reset();
    ObAiSplitDocChunk text_chunk;
    bool chunk_found = false;
    while (OB_SUCC(ret) && !chunk_found) {
      if (OB_NOT_NULL(iterator_) && iterator_->is_inited()) {
        if (OB_FAIL(iterator_->get_next_row(text_chunk))) {
          if (ret == OB_ITER_END) {
            iterator_->close();
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to split markdown section", K(ret));
          }
        } else {
          const int64_t section_offset = section_content_.ptr() - content_.ptr();
          chunk.init(chunk_id_++, section_offset + text_chunk.chunk_offset_,
                     text_chunk.chunk_length_, text_chunk.chunk_text_);
          if (!section_title_.empty()) {
            const int64_t total_len = section_title_.length() + text_chunk.chunk_text_.length();
            char *buf = static_cast<char *>(row_alloc_.alloc(total_len));
            if (OB_ISNULL(buf)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("failed to allocate markdown chunk", K(ret), K(total_len));
            } else {
              MEMCPY(buf, section_title_.ptr(), section_title_.length());
              MEMCPY(buf + section_title_.length(), text_chunk.chunk_text_.ptr(),
                     text_chunk.chunk_text_.length());
              chunk.chunk_text_.assign_ptr(buf, total_len);
            }
          }
          chunk_found = OB_SUCC(ret);
        }
      } else if (OB_FAIL(get_next_section(section_title_, section_content_))) {
        if (ret == OB_ITER_END) {
          is_done_ = true;
        } else {
          LOG_WARN("failed to read markdown section", K(ret));
        }
      } else if (!section_title_.empty() && is_empty_section(section_content_)) {
        const int64_t title_offset = section_title_.ptr() - content_.ptr();
        chunk.init(chunk_id_++, title_offset, section_title_.length(), section_title_);
        chunk_found = true;
      } else if (OB_FAIL(iterator_->open(section_content_, *allocator_, params_))) {
        LOG_WARN("failed to open markdown section iterator", K(ret));
      }
    }
  }
  return ret;
}

int ObMarkdownSplitIterator::close()
{
  int ret = OB_SUCCESS;
  is_inited_ = false;
  content_.reset();
  params_.reset();
  chunk_id_ = 0;
  section_start_offset_ = 0;
  section_title_.reset();
  section_content_.reset();
  is_done_ = false;
  if (OB_NOT_NULL(iterator_)) {
    iterator_->close();
    if (OB_NOT_NULL(allocator_)) {
      OB_DELETEx(ObTextSplitIterator, allocator_, iterator_);
    } else {
      iterator_ = nullptr;
    }
  }
  row_alloc_.clear();
  allocator_ = nullptr;
  return ret;
}

int ObMarkdownSplitIterator::get_next_section(ObString &section_title,
                                              ObString &section_content)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (section_start_offset_ >= content_.length()) {
    ret = OB_ITER_END;
  } else {
    section_title.reset();
    section_content.reset();
    bool has_title = false;
    bool section_end = false;
    int64_t content_start = section_start_offset_;
    int64_t content_end = content_start;
    int64_t line_start = section_start_offset_;
    int64_t line_end = line_start;
    while (OB_SUCC(ret) && line_start < content_.length() && !section_end) {
      if (OB_FAIL(get_next_line(line_start, line_end))) {
        LOG_WARN("failed to read markdown line", K(ret));
      } else if (is_title_line(
                   ObString(line_end - line_start, content_.ptr() + line_start),
                   DEFAULT_TITLE_LEVEL)) {
        if (!has_title) {
          has_title = true;
          const int64_t title_end = line_end < content_.length() ? line_end + 1
                                                                 : content_.length();
          section_title.assign_ptr(content_.ptr() + line_start, title_end - line_start);
          content_start = title_end;
        } else {
          section_end = true;
          content_end = line_start;
        }
      }
      line_start = line_end + 1;
    }
    if (OB_SUCC(ret)) {
      if (!section_end) {
        content_end = line_end < content_.length() ? line_end + 1 : line_end;
      }
      section_content.assign_ptr(content_.ptr() + content_start, content_end - content_start);
      section_start_offset_ = content_end;
    }
  }
  return ret;
}

int ObMarkdownSplitIterator::get_next_line(const int64_t line_start_offset,
                                           int64_t &line_end_offset)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (line_start_offset >= content_.length()) {
    ret = OB_ITER_END;
  } else {
    line_end_offset = line_start_offset;
    while (line_end_offset < content_.length() && content_[line_end_offset] != '\n') {
      ++line_end_offset;
    }
  }
  return ret;
}

bool ObMarkdownSplitIterator::is_empty_section(const ObString &section_content) const
{
  int64_t pos = 0;
  while (pos < section_content.length() && is_space(section_content[pos])) {
    ++pos;
  }
  return pos == section_content.length();
}

bool ObMarkdownSplitIterator::is_title_line(const ObString &line,
                                            const int64_t title_level) const
{
  int64_t pos = 0;
  while (pos < line.length() && line[pos] == '#') {
    ++pos;
  }
  return pos == title_level;
}

} // namespace common
} // namespace oceanbase
