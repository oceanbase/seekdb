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

#ifndef OCEANBASE_DATA_PLANE_FTS_OB_FTS_PARSER_H_
#define OCEANBASE_DATA_PLANE_FTS_OB_FTS_PARSER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"

struct ObCharsetInfo;

namespace oceanbase
{
namespace storage
{
class ObAddWordFlag;

class ObFTIKParam final
{
public:
  enum class Mode : uint8_t
  {
    SMART = 0,
    MAX_WORD = 1,
  };

  explicit ObFTIKParam(Mode mode = Mode::SMART)
      : mode_(mode), main_dict_(""), quan_dict_(""), stopword_dict_("")
  {
  }

public:
  Mode mode_;
  common::ObString main_dict_;
  common::ObString quan_dict_;
  common::ObString stopword_dict_;
};

class ObFTParserParam final
{
public:
  static const int64_t NGRAM_TOKEN_SIZE = 2;

  ObFTParserParam()
      : cs_(nullptr),
        fulltext_(nullptr),
        ft_length_(0),
        parser_version_(0),
        allocator_(nullptr),
        ik_param_(),
        ngram_token_size_(NGRAM_TOKEN_SIZE),
        min_ngram_size_(NGRAM_TOKEN_SIZE),
        max_ngram_size_(NGRAM_TOKEN_SIZE)
  {
  }
  ~ObFTParserParam() = default;

  bool is_valid() const
  {
    return nullptr != cs_
        && nullptr != fulltext_
        && 0 < ft_length_
        && 0 <= parser_version_
        && nullptr != allocator_;
  }

  void reset()
  {
    cs_ = nullptr;
    fulltext_ = nullptr;
    ft_length_ = 0;
    parser_version_ = 0;
    allocator_ = nullptr;
    ik_param_ = ObFTIKParam();
    ngram_token_size_ = NGRAM_TOKEN_SIZE;
    min_ngram_size_ = NGRAM_TOKEN_SIZE;
    max_ngram_size_ = NGRAM_TOKEN_SIZE;
  }

  TO_STRING_KV(KP_(cs),
               K_(fulltext),
               K_(ft_length),
               K_(parser_version),
               KP_(allocator),
               K_(ngram_token_size),
               K_(min_ngram_size),
               K_(max_ngram_size));

public:
  const ObCharsetInfo *cs_;
  const char *fulltext_;
  int64_t ft_length_;
  int64_t parser_version_;
  common::ObIAllocator *allocator_;
  ObFTIKParam ik_param_;
  int64_t ngram_token_size_;
  int64_t min_ngram_size_;
  int64_t max_ngram_size_;
};

class ObITokenIterator
{
public:
  ObITokenIterator() = default;
  virtual ~ObITokenIterator() = default;
  virtual int get_next_token(
      const char *&word,
      int64_t &word_len,
      int64_t &char_cnt,
      int64_t &word_freq) = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;
};

class ObIFTParserDesc
{
public:
  ObIFTParserDesc() = default;
  virtual ~ObIFTParserDesc() = default;

  virtual int segment(ObFTParserParam *param, ObITokenIterator *&iter) const = 0;

  virtual void free_token_iter(ObFTParserParam *param, ObITokenIterator *&iter) const
  {
    if (OB_NOT_NULL(iter)) {
      iter->~ObITokenIterator();
    }
  }

  virtual int get_add_word_flag(ObAddWordFlag &flag) const = 0;
  virtual int check_if_charset_supported(const ObCharsetInfo *cs) const
  {
    UNUSED(cs);
    return OB_SUCCESS;
  }
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_FTS_OB_FTS_PARSER_H_
