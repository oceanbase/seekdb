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

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/ik/ob_ik_processor.h"

#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "storage/fts/ik/ob_ik_arbitrator.h"
#include "storage/fts/ik/ob_ik_char_util.h"
#include "storage/fts/ik/ob_ik_token.h"

namespace oceanbase
{
namespace storage
{
int ObIIKProcessor::process(TokenizeContext &ctx)
{
  int ret = OB_SUCCESS;

  ObFTCharUtil::CharType type;
  const char *ch = nullptr;
  uint8_t char_len = 0;

  if (OB_FAIL(ctx.current_char_type(type))) {
    LOG_WARN("fail to get current char type", K(ret));
  } else if (OB_FAIL(ctx.current_char(ch, char_len))) {
    LOG_WARN("Fail to get current char", K(ret));
  } else if (OB_FAIL(do_process(ctx, ch, char_len, type))) {
    LOG_WARN("Failed to do process char", K(ret));
  }
  return ret;
}

TokenizeContext::TokenizeContext(ObCollationType coll_type,
                                 ObIAllocator &allocator,
                                 const char *fulltext,
                                 int64_t fulltext_len,
                                 bool is_smart)
    : allocator_(allocator), coll_type_(coll_type), fulltext_(fulltext),
      fulltext_len_(fulltext_len), cursor_(0), next_char_len_(0), handle_size_(0),
      is_smart_(is_smart), char_len_cache_(nullptr), char_type_cache_(nullptr),
      char_cache_cap_(0), char_cache_len_(0), token_list_(allocator), result_list_(allocator)
{
}

int TokenizeContext::ensure_char_cache(int64_t len)
{
  int ret = OB_SUCCESS;
  if (len > char_cache_cap_) {
    // Grow the classification cache. Reused across documents on the same
    // (thread-local) parser, so this only reallocates when a longer document
    // than any seen before is encountered.
    uint8_t *new_len_cache = static_cast<uint8_t *>(allocator_.alloc(len));
    uint8_t *new_type_cache = static_cast<uint8_t *>(allocator_.alloc(len));
    if (OB_ISNULL(new_len_cache) || OB_ISNULL(new_type_cache)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to alloc char classification cache", K(ret), K(len));
    } else {
      char_len_cache_ = new_len_cache;
      char_type_cache_ = new_type_cache;
      char_cache_cap_ = len;
    }
  }
  if (OB_SUCC(ret)) {
    char_cache_len_ = len;
    // char_len == 0 marks "not classified"; only the length array needs reset.
    MEMSET(char_len_cache_, 0, len);
  }
  return ret;
}

int TokenizeContext::init()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(fulltext_) || fulltext_len_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ensure_char_cache(fulltext_len_))) {
    LOG_WARN("Failed to prepare char cache", K(ret));
  } else if (OB_FAIL(prepare_next_char())) {
    LOG_WARN("Failed to prepare next char", K(ret));
  }
  return ret;
}

int TokenizeContext::reset_resource()
{
  handle_size_ = 0;
  result_list_.reset();
  token_list_.reset();
  return OB_SUCCESS;
}

int TokenizeContext::set_text(const char *fulltext, int64_t fulltext_len,
                              ObCollationType coll_type, bool is_smart)
{
  int ret = OB_SUCCESS;
  coll_type_ = coll_type;
  fulltext_ = fulltext;
  fulltext_len_ = fulltext_len;
  cursor_ = 0;
  next_char_len_ = 0;
  handle_size_ = 0;
  is_smart_ = is_smart;
  reset_resource();
  if (OB_ISNULL(fulltext_) || fulltext_len_ <= 0) {
    // empty text — don't call prepare_next_char
    char_cache_len_ = 0;
  } else if (OB_FAIL(ensure_char_cache(fulltext_len_))) {
    LOG_WARN("Failed to prepare char cache for reuse", K(ret));
  } else if (OB_FAIL(prepare_next_char())) {
    LOG_WARN("Failed to prepare next char for reuse", K(ret));
  }
  return ret;
}

int TokenizeContext::current_char(const char *&ch, uint8_t &char_len)
{
  int ret = OB_SUCCESS;
  if (cursor_ >= fulltext_len_) {
    ret = OB_ITER_END;
  } else {
    ch = fulltext_ + cursor_;
    char_len = next_char_len_;
  }
  return ret;
}

int TokenizeContext::current_char_type(ObFTCharUtil::CharType &type)
{
  int ret = OB_SUCCESS;
  if (cursor_ >= fulltext_len_) {
    ret = OB_ITER_END;
  } else {
    type = next_char_type_;
  }
  return ret;
}

int TokenizeContext::prepare_next_char()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObFTCharUtil::classify_first_valid_char(coll_type_,
                                                       fulltext_ + cursor_,
                                                       fulltext_len_ - cursor_,
                                                       next_char_len_,
                                                       next_char_type_))) {
    LOG_WARN("Failed to classify first valid char", K(ret));
  } else if (cursor_ >= 0 && cursor_ < char_cache_len_ && next_char_len_ > 0) {
    // record classification so output_result can reuse it without re-decoding
    char_len_cache_[cursor_] = static_cast<uint8_t>(next_char_len_);
    char_type_cache_[cursor_] = static_cast<uint8_t>(next_char_type_);
  }
  return ret;
}

int TokenizeContext::step_next()
{
  int ret = OB_SUCCESS;
  if (cursor_ >= fulltext_len_) {
    ret = OB_ITER_END;
  } else if (cursor_ + next_char_len_ >= fulltext_len_) {
    cursor_ = fulltext_len_;
    next_char_len_ = 0;
    ret = OB_ITER_END;
  } else if (cursor_ < fulltext_len_ && 0 == next_char_len_) {
    // should not happen
    ret = OB_UNEXPECT_INTERNAL_ERROR;
    LOG_WARN("Unexpected error", K(ret));
  } else {
    cursor_ += next_char_len_;
    handle_size_++;
    if (OB_FAIL(prepare_next_char())) {
      LOG_WARN("Failed to prepare next char", K(ret));
    } else {
    }
  }
  return ret;
}

ObCollationType TokenizeContext::collation() const { return coll_type_; }

int64_t TokenizeContext::get_end_cursor() const { return cursor_ + next_char_len_; }

const char *TokenizeContext::fulltext() const { return fulltext_; }

int64_t TokenizeContext::fulltext_len() const { return fulltext_len_; }

int64_t TokenizeContext::get_cursor() const { return cursor_; }

bool TokenizeContext::is_last() const { return cursor_ + next_char_len_ >= fulltext_len_; }

bool TokenizeContext::iter_end() const { return cursor_ >= fulltext_len_; }

bool TokenizeContext::is_smart() const { return is_smart_; }

int TokenizeContext::add_token(const char *fulltext,
                               int64_t offset,
                               int64_t length,
                               int64_t char_cnt,
                               ObIKTokenType type)
{
  int ret = OB_SUCCESS;
  ObIKToken token;
  token.ptr_ = fulltext;
  token.length_ = length;
  token.offset_ = offset;
  token.char_cnt_ = char_cnt;
  token.type_ = type;
  if (OB_FAIL(token_list_.add_token(token))) {
    LOG_WARN("Failed to add token to result list", K(ret));
  }
  return ret;
}

TokenizeContext::~TokenizeContext()
{
  token_list_.reset();
  result_list_.reset();
}

int TokenizeContext::get_next_token(const char *&word,
                                    int64_t &word_len,
                                    int64_t &offset,
                                    int64_t &char_cnt)
{
  int ret = OB_SUCCESS;
  if (!result_list_.empty()) {
    ObIKToken &token = result_list_.get_first();
    result_list_.pop_front();
    if (!result_list_.empty()) {
      if (OB_FAIL(compound(token))) {
        LOG_WARN("Failed to compound", K(ret));
      } else {
        // pass
      }
    }
    if (OB_SUCC(ret)) {
      word = token.ptr_;
      word_len = token.length_;
      offset = token.offset_;
      char_cnt = token.char_cnt_;
    }
  } else {
    ret = OB_ITER_END;
  }
  return ret;
}

int TokenizeContext::compound(ObIKToken &token)
{
  int ret = OB_SUCCESS;
  ObList<ObIKToken, ObIAllocator> &list = result_list_;
  if (is_smart_) {
    if (!list.empty()) {
      if (ObIKTokenType::IK_ARABIC_TOKEN == token.type_) {
        ObIKToken &next = list.get_first();
        bool append = false;

        if (ObIKTokenType::IK_CNNUM_TOKEN == next.type_) {
          // handle eng num + chn num
          if (token.offset_ + token.length_ == next.offset_) {
            append = true;
            token.length_ += next.length_;
            token.char_cnt_ += next.char_cnt_;
            token.type_ = ObIKTokenType::IK_CNNUM_TOKEN;
          }
        } else if (ObIKTokenType::IK_COUNT_TOKEN == next.type_) {
          // handle eng num + chn count
          if (token.offset_ + token.length_ == next.offset_) {
            append = true;
            token.length_ += next.length_;
            token.char_cnt_ += next.char_cnt_;
            token.type_ = ObIKTokenType::IK_CNQUAN_TOKEN;
          }
        } else {
          // pass
        }
        if (append) {
          list.pop_front();
        }
      }
      // There may be another round of append
      if (OB_SUCC(ret)) {
        ObIKToken next = list.get_first();
        bool append = false;
        if (ObIKTokenType::IK_COUNT_TOKEN == next.type_) {
          if (token.offset_ + token.length_ == next.offset_) {
            append = true;
            token.length_ += next.length_;
            token.type_ = ObIKTokenType::IK_CNQUAN_TOKEN;
          }
        }
        if (append) {
          list.pop_front();
        }
      }
    }
  } else {
    // nothing todo, just return
  }
  return ret;
}

} // namespace storage

} // namespace oceanbase
