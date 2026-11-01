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
  } else if (OB_FAIL(ctx.current_char(ch, char_len))) {
  } else if (OB_FAIL(do_process(ctx, ch, char_len, type))) {
  }
  return ret;
}

TokenizeContext::TokenizeContext(ObCollationType coll_type,
                                 ObIAllocator &allocator,
                                 const char *fulltext,
                                 int64_t fulltext_len,
                                 bool is_smart)
    : coll_type_(coll_type), fulltext_(fulltext), fulltext_len_(fulltext_len),
      allocator_(allocator), char_infos_(nullptr), char_info_cnt_(0), char_info_cap_(0),
      cursor_(0), next_char_len_(0), next_char_type_(ObFTCharUtil::CharType::USELESS),
      cs_(nullptr), cs_type_(CHARSET_INVALID), handle_size_(0), is_smart_(is_smart),
      token_list_(allocator), result_list_(allocator)
{
}

int TokenizeContext::init()
{
  return reinit(coll_type_, fulltext_, fulltext_len_, is_smart_);
}

int TokenizeContext::reinit(ObCollationType coll_type,
                            const char *fulltext,
                            int64_t fulltext_len,
                            bool is_smart)
{
  int ret = OB_SUCCESS;

  coll_type_ = coll_type;
  fulltext_ = fulltext;
  fulltext_len_ = fulltext_len;
  is_smart_ = is_smart;
  cursor_ = 0;
  next_char_len_ = 0;
  next_char_type_ = ObFTCharUtil::CharType::USELESS;
  handle_size_ = 0;
  char_info_cnt_ = 0;
  if (OB_ISNULL(fulltext_) || fulltext_len_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(coll_type_ <= CS_TYPE_INVALID || coll_type_ >= CS_TYPE_MAX)) {
    // same validation as ObCharset::first_valid_char(), resolved once here
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(cs_ = ObCharset::get_charset(coll_type_))) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_ISNULL(cs_->cset)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    cs_type_ = ObCharset::charset_type_by_coll(coll_type_);
    if (OB_FAIL(prepare_next_char())) {
    }
  }
  return ret;
}

void TokenizeContext::release_scratch()
{
  reset_resource();
  // drop the per-character cache table: the buffer lives in the per-call
  // scratch allocator which is recycled after this point, and a stale table
  // must never be visible to the next fulltext (see Task-2 char_infos_ note).
  if (OB_NOT_NULL(char_infos_)) {
    allocator_.free(char_infos_);
    char_infos_ = nullptr;
    char_info_cnt_ = 0;
    char_info_cap_ = 0;
  }
  fulltext_ = nullptr;
  fulltext_len_ = 0;
  cursor_ = 0;
  next_char_len_ = 0;
  next_char_type_ = ObFTCharUtil::CharType::USELESS;
  handle_size_ = 0;
}

int TokenizeContext::reset_resource()
{
  handle_size_ = 0;
  result_list_.reset();
  token_list_.reset();
  return OB_SUCCESS;
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
  const char *cur = fulltext_ + cursor_;
  const int64_t buf_size = fulltext_len_ - cursor_;
  // merged ObCharset::first_valid_char() + ObFTCharUtil::classify_first_char():
  // the charset info is cached in init() and the character is decoded once
  // instead of once per predicate. The well_formed_len behavior and the
  // classification predicate order are kept identical to the two-step path.
  if (buf_size <= 0) {
    // same as first_valid_char(): empty remainder yields a zero-length char
    next_char_len_ = 0;
  } else {
    int error = 0;
    int64_t len = cs_->cset->well_formed_len(cs_, cur, cur + buf_size, 1, &error);
    if (OB_LIKELY(0 == error)) {
      next_char_len_ = len;
    } else {
      ret = OB_INVALID_ARGUMENT;
      STORAGE_FTS_LOG(WARN, "invalid encoding found");
    }
  }
  if (OB_SUCC(ret)) {
    ob_wc_t unicode = 0;
    if (OB_FAIL(ObFTCharUtil::decode_unicode_by_charset(
                    cs_type_, cur, static_cast<uint8_t>(next_char_len_), unicode))) {
    } else if (OB_FAIL(ObFTCharUtil::classify_decoded_by_charset(
                           cs_type_, unicode, next_char_type_))) {
    }
  }
  if (OB_SUCC(ret)) {
    ret = record_char_info();
  }
  return ret;
}

int TokenizeContext::record_char_info()
{
  int ret = OB_SUCCESS;
  // append-only: the cursor only moves forward, so every character start is
  // prepared exactly once over the lifetime of this context. A re-prepare of
  // the current position (cannot happen today) overwrites in place.
  if (char_info_cnt_ > 0) {
    CharInfo &last = char_infos_[char_info_cnt_ - 1];
    if (cursor_ == last.offset_) {
      last.len_ = next_char_len_;
      last.type_ = next_char_type_;
      return ret;
    } else if (cursor_ < last.offset_) {
      // stale re-prepare, keep the monotonic table untouched
      return ret;
    }
  }
  if (char_info_cnt_ >= char_info_cap_) {
    const int64_t new_cap = (0 == char_info_cap_) ? 256 : char_info_cap_ * 2;
    CharInfo *new_buf = static_cast<CharInfo *>(allocator_.alloc(sizeof(CharInfo) * new_cap));
    if (OB_ISNULL(new_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_NOT_NULL(char_infos_)) {
      MEMCPY(new_buf, char_infos_, sizeof(CharInfo) * char_info_cnt_);
      allocator_.free(char_infos_);
      char_infos_ = new_buf;
      char_info_cap_ = new_cap;
    } else {
      char_infos_ = new_buf;
      char_info_cap_ = new_cap;
    }
  }
  if (OB_SUCC(ret)) {
    CharInfo &info = char_infos_[char_info_cnt_++];
    info.offset_ = cursor_;
    info.len_ = next_char_len_;
    info.type_ = next_char_type_;
  }
  return ret;
}

int TokenizeContext::lookup_char_info(int64_t byte_offset, int64_t &hint_idx, CharInfo &info) const
{
  int ret = OB_SUCCESS;
  bool found = false;
  while (hint_idx + 1 < char_info_cnt_ && char_infos_[hint_idx + 1].offset_ <= byte_offset) {
    ++hint_idx;
  }
  if (hint_idx < char_info_cnt_ && char_infos_[hint_idx].offset_ == byte_offset) {
    info = char_infos_[hint_idx];
    found = true;
  }
  if (!found) {
    // position beyond the prepared region (the arbitrator scans the whole
    // fulltext while only part of it has been segmented so far): fall back to
    // the original two-step classification, which yields identical results.
    if (OB_FAIL(ObCharset::first_valid_char(coll_type_,
                                            fulltext_ + byte_offset,
                                            fulltext_len_ - byte_offset,
                                            info.len_))) {
    } else if (OB_FAIL(ObFTCharUtil::classify_first_char(coll_type_,
                                                         fulltext_ + byte_offset,
                                                         static_cast<uint8_t>(info.len_),
                                                         info.type_))) {
    }
    info.offset_ = byte_offset;
  }
  return ret;
}

int TokenizeContext::get_char_info(int64_t byte_offset,
                                   int64_t &hint_idx,
                                   int64_t &char_len,
                                   ObFTCharUtil::CharType &type) const
{
  int ret = OB_SUCCESS;
  CharInfo info;
  info.offset_ = 0;
  info.len_ = 0;
  info.type_ = ObFTCharUtil::CharType::USELESS;
  if (OB_FAIL(lookup_char_info(byte_offset, hint_idx, info))) {
  } else {
    char_len = info.len_;
    type = info.type_;
  }
  return ret;
}

int TokenizeContext::get_char_len(int64_t byte_offset, int64_t &hint_idx, int64_t &char_len) const
{
  int ret = OB_SUCCESS;
  CharInfo info;
  info.offset_ = 0;
  info.len_ = 0;
  info.type_ = ObFTCharUtil::CharType::USELESS;
  if (OB_FAIL(lookup_char_info(byte_offset, hint_idx, info))) {
  } else {
    char_len = info.len_;
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
  } else {
    cursor_ += next_char_len_;
    handle_size_++;
    if (OB_FAIL(prepare_next_char())) {
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
  }
  return ret;
}

TokenizeContext::~TokenizeContext()
{
  if (OB_NOT_NULL(char_infos_)) {
    allocator_.free(char_infos_);
    char_infos_ = nullptr;
    char_info_cnt_ = 0;
    char_info_cap_ = 0;
  }
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
