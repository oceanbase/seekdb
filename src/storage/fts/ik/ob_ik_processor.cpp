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
    : coll_type_(coll_type), fulltext_(fulltext), fulltext_len_(fulltext_len), cursor_(0),
      next_char_len_(0), handle_size_(0), is_smart_(is_smart), token_list_(allocator),
      result_list_(), result_head_(0)
{
}

int TokenizeContext::init()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(fulltext_) || fulltext_len_ <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(prepare_next_char())) {
    LOG_WARN("Failed to prepare next char", K(ret));
  }
  return ret;
}

int TokenizeContext::reset_resource()
{
  handle_size_ = 0;
  result_list_.reuse();
  result_head_ = 0;
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
  if (OB_FAIL(ObFTCharUtil::get_first_valid_char_and_type(coll_type_,
                                                          fulltext_ + cursor_,
                                                          fulltext_len_ - cursor_,
                                                          next_char_len_,
                                                          next_char_type_))) {
    LOG_WARN("failed to get first valid character and type", K(ret), K(cursor_), K(fulltext_len_));
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
  result_head_ = 0;
}

int TokenizeContext::get_next_token(const char *&word,
                                    int64_t &word_len,
                                    int64_t &offset,
                                    int64_t &char_cnt)
{
  int ret = OB_SUCCESS;

  if (result_head_ >= result_list_.count()) {
    ret = OB_ITER_END;
  } else {
    ObIKToken token = result_list_.at(result_head_);
    ++result_head_;

    if (result_head_ < result_list_.count()) {
      if (OB_FAIL(compound(token))) {
        LOG_WARN("failed to compound token", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      word = token.ptr_;
      word_len = token.length_;
      offset = token.offset_;
      char_cnt = token.char_cnt_;
    }
  }

  return ret;
}

int TokenizeContext::compound(ObIKToken &token)
{
  int ret = OB_SUCCESS;

  if (is_smart_ && result_head_ < result_list_.count()) {
    if (ObIKTokenType::IK_ARABIC_TOKEN == token.type_) {
      const ObIKToken &next = result_list_.at(result_head_);
      bool append = false;

      if (ObIKTokenType::IK_CNNUM_TOKEN == next.type_) {
        if (token.offset_ + token.length_ == next.offset_) {
          append = true;
          token.length_ += next.length_;
          token.char_cnt_ += next.char_cnt_;
          token.type_ = ObIKTokenType::IK_CNNUM_TOKEN;
        }
      } else if (ObIKTokenType::IK_COUNT_TOKEN == next.type_) {
        if (token.offset_ + token.length_ == next.offset_) {
          append = true;
          token.length_ += next.length_;
          token.char_cnt_ += next.char_cnt_;
          token.type_ = ObIKTokenType::IK_CNQUAN_TOKEN;
        }
      }

      if (append) {
        ++result_head_;
      }
    }

    if (result_head_ < result_list_.count()) {
      const ObIKToken &next = result_list_.at(result_head_);
      bool append = false;

      if (ObIKTokenType::IK_COUNT_TOKEN == next.type_
          && token.offset_ + token.length_ == next.offset_) {
        append = true;
        token.length_ += next.length_;
        token.type_ = ObIKTokenType::IK_CNQUAN_TOKEN;
      }

      if (append) {
        ++result_head_;
      }
    }
  }

  return ret;
}

} // namespace storage

} // namespace oceanbase
