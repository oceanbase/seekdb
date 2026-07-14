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

#ifndef OCEANBASE_LIB_AI_SPLIT_DOCUMENT_OB_AI_SPLIT_DOCUMENT_H_
#define OCEANBASE_LIB_AI_SPLIT_DOCUMENT_OB_AI_SPLIT_DOCUMENT_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include <unicode/brkiter.h>
#include <unicode/utext.h>

namespace oceanbase
{
namespace common
{

enum class ObAiSplitContentType
{
  TEXT = 0,
  MARKDOWN,
  MAX_CONTENT_TYPE,
};

enum class ObAiSplitByUnit
{
  WORD = 0,
  SENTENCE,
  MAX_UNIT_TYPE,
};

struct ObAiSplitDocParams
{
  ObAiSplitDocParams()
    : type_(ObAiSplitContentType::MARKDOWN),
      by_(ObAiSplitByUnit::WORD),
      max_(256),
      overlap_(0)
  {}

  void reset()
  {
    type_ = ObAiSplitContentType::MARKDOWN;
    by_ = ObAiSplitByUnit::WORD;
    max_ = 256;
    overlap_ = 0;
  }
  void assign(const ObAiSplitDocParams &other)
  {
    type_ = other.type_;
    by_ = other.by_;
    max_ = other.max_;
    overlap_ = other.overlap_;
  }
  int init(const ObString &type_str, const ObString &by_str,
           const int64_t max, const int64_t overlap);
  int check_validity() const;

  ObAiSplitContentType type_;
  ObAiSplitByUnit by_;
  int64_t max_;
  int64_t overlap_;

  TO_STRING_KV(K_(type), K_(by), K_(max), K_(overlap));

private:
  int parse_type(const ObString &type_str);
  int parse_by(const ObString &by_str);
};

struct ObAiSplitDocChunk
{
  ObAiSplitDocChunk()
    : chunk_id_(0), chunk_offset_(0), chunk_length_(0), chunk_text_()
  {}

  void init(const int64_t id, const int64_t offset, const int64_t length,
            const ObString &text)
  {
    chunk_id_ = id;
    chunk_offset_ = offset;
    chunk_length_ = length;
    chunk_text_ = text;
  }
  void reset()
  {
    chunk_id_ = 0;
    chunk_offset_ = 0;
    chunk_length_ = 0;
    chunk_text_.reset();
  }

  int64_t chunk_id_;
  int64_t chunk_offset_;
  int64_t chunk_length_;
  ObString chunk_text_;

  TO_STRING_KV(K_(chunk_id), K_(chunk_offset), K_(chunk_length), K_(chunk_text));
};

struct ObAiSplitDocInput
{
  ObAiSplitDocInput() : content_(), params_() {}

  int init(const ObString &content, const ObAiSplitDocParams &params);
  void reset()
  {
    content_.reset();
    params_.reset();
  }

  ObString content_;
  ObAiSplitDocParams params_;

  TO_STRING_KV(K_(content), K_(params));
};

class ObDocSplitIterator
{
public:
  ObDocSplitIterator() {}
  virtual ~ObDocSplitIterator() {}
  virtual int open(const ObString &content, ObIAllocator &allocator,
                   const ObAiSplitDocParams &params) = 0;
  virtual int get_next_row(ObAiSplitDocChunk &chunk) = 0;
  virtual int close() = 0;
};

class ObTextSplitIterator : public ObDocSplitIterator
{
public:
  ObTextSplitIterator()
    : is_inited_(false),
      content_(),
      params_(),
      chunk_id_(0),
      chunk_start_offset_(0),
      next_chunk_start_(0),
      unit_since_window_start_(0),
      current_boundary_(0),
      is_done_(false),
      bi_(nullptr),
      utext_(nullptr)
  {}
  virtual ~ObTextSplitIterator() override { close(); }

  bool is_inited() const { return is_inited_; }
  virtual int open(const ObString &content, ObIAllocator &allocator,
                   const ObAiSplitDocParams &params) override;
  virtual int get_next_row(ObAiSplitDocChunk &chunk) override;
  virtual int close() override;

private:
  bool is_inited_;
  ObString content_;
  ObAiSplitDocParams params_;
  int64_t chunk_id_;
  int64_t chunk_start_offset_;
  int64_t next_chunk_start_;
  int64_t unit_since_window_start_;
  int64_t current_boundary_;
  bool is_done_;
  icu::BreakIterator *bi_;
  UText *utext_;
};

class ObMarkdownSplitIterator : public ObDocSplitIterator
{
public:
  ObMarkdownSplitIterator()
    : is_inited_(false),
      content_(),
      params_(),
      chunk_id_(0),
      section_start_offset_(0),
      section_title_(),
      section_content_(),
      is_done_(false),
      row_alloc_(),
      allocator_(nullptr),
      iterator_(nullptr)
  {}
  virtual ~ObMarkdownSplitIterator() override { close(); }

  virtual int open(const ObString &content, ObIAllocator &allocator,
                   const ObAiSplitDocParams &params) override;
  virtual int get_next_row(ObAiSplitDocChunk &chunk) override;
  virtual int close() override;

private:
  static const int64_t DEFAULT_TITLE_LEVEL = 1;
  int get_next_section(ObString &section_title, ObString &section_content);
  int get_next_line(const int64_t line_start_offset, int64_t &line_end_offset);
  bool is_empty_section(const ObString &section_content) const;
  bool is_title_line(const ObString &line, const int64_t title_level) const;

private:
  bool is_inited_;
  ObString content_;
  ObAiSplitDocParams params_;
  int64_t chunk_id_;
  int64_t section_start_offset_;
  ObString section_title_;
  ObString section_content_;
  bool is_done_;
  ObArenaAllocator row_alloc_;
  ObIAllocator *allocator_;
  ObTextSplitIterator *iterator_;
};

class ObAiSplitDocAdapter
{
public:
  ObAiSplitDocAdapter()
    : is_inited_(false), cur_chunk_(), cur_idx_(-1), allocator_(nullptr), iterator_(nullptr)
  {}
  ~ObAiSplitDocAdapter() { reset(); }

  int init(ObIAllocator &allocator, const ObString &content,
           const ObAiSplitDocParams &params);
  int get_next_row();
  bool is_inited() const { return is_inited_; }
  int64_t get_cur_idx() const { return cur_idx_; }
  const ObAiSplitDocChunk &get_cur_chunk() const { return cur_chunk_; }
  void reset();

private:
  bool is_inited_;
  ObAiSplitDocChunk cur_chunk_;
  int64_t cur_idx_;
  ObIAllocator *allocator_;
  ObDocSplitIterator *iterator_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_AI_SPLIT_DOCUMENT_OB_AI_SPLIT_DOCUMENT_H_
