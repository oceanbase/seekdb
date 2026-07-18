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
int ObIIKProcessor::process(TokenizeContext &ctx,
                            const char *ch,
                            const uint8_t char_len,
                            const ObFTCharUtil::CharType type)
{
  int ret = OB_SUCCESS;
  // FTS index hot-path optimization: the parser already fetched and decoded
  // the current character. Reuse it for all processors instead of reading the
  // same TokenizeContext fields twice per processor.
  if (OB_FAIL(do_process(ctx, ch, char_len, type))) {
    LOG_WARN("Failed to do process char", K(ret));
  }
  return ret;
}

TokenizeContext::TokenizeContext(ObCollationType coll_type,
                                 ObIAllocator &allocator,
                                 const char *fulltext,
                                 int64_t fulltext_len,
                                 bool is_smart)
    : coll_type_(coll_type),
      charset_info_(ObCharset::get_charset(coll_type)),
      well_formed_len_func_(nullptr == charset_info_ || nullptr == charset_info_->cset
                                ? nullptr
                                : charset_info_->cset->well_formed_len),
      fulltext_(fulltext), fulltext_len_(fulltext_len), cursor_(0),
      next_char_len_(0), handle_size_(0), is_smart_(is_smart), token_list_(allocator),
      results_(allocator), result_idx_(0), batch_start_cursor_(0)
{
}

int TokenizeContext::init()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(fulltext_) || fulltext_len_ <= 0 || OB_ISNULL(charset_info_)
      || OB_ISNULL(well_formed_len_func_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(prepare_next_char())) {
    LOG_WARN("Failed to prepare next char", K(ret));
  }
  return ret;
}

int TokenizeContext::reuse_context(const char *fulltext, int64_t fulltext_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(fulltext) || fulltext_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext for context reuse", K(ret), KP(fulltext), K(fulltext_len));
  } else {
    fulltext_ = fulltext;
    fulltext_len_ = fulltext_len;
    cursor_ = 0;
    batch_start_cursor_ = 0;
    if (OB_FAIL(reset_resource())) {
      LOG_WARN("failed to reset tokenize context", K(ret));
    } else if (OB_FAIL(prepare_next_char())) {
      LOG_WARN("failed to prepare reused context", K(ret));
    }
  }
  return ret;
}

int TokenizeContext::reset_resource()
{
  handle_size_ = 0;
  // FTS next-stage optimization (Op1): logical reuse keeps already allocated
  // blocks hot and removes per-token alloc/free traffic.
  results_.reuse();
  result_idx_ = 0;
  token_list_.reuse();
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

int TokenizeContext::current_char_and_type(const char *&ch,
                                           uint8_t &char_len,
                                           ObFTCharUtil::CharType &type)
{
  int ret = OB_SUCCESS;
  if (cursor_ >= fulltext_len_) {
    ret = OB_ITER_END;
  } else {
    ch = fulltext_ + cursor_;
    char_len = next_char_len_;
    type = next_char_type_;
  }
  return ret;
}

int TokenizeContext::prepare_next_char()
{
  int ret = OB_SUCCESS;
  int well_formed_error = 0;
  // FTS next-stage optimization (character decoding): call the cached charset
  // function directly; classification still decodes the code point only once.
  next_char_len_ = static_cast<int64_t>(well_formed_len_func_(charset_info_,
                                                              fulltext_ + cursor_,
                                                              fulltext_ + fulltext_len_,
                                                              1,
                                                              &well_formed_error));
  if (OB_UNLIKELY(0 != well_formed_error || next_char_len_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("failed to get first valid char", K(ret), K(well_formed_error), K(next_char_len_));
  } else if (OB_FAIL(ObFTCharUtil::classify_first_char(coll_type_,
                                                       fulltext_ + cursor_,
                                                       next_char_len_,
                                                       next_char_type_))) {
    LOG_WARN("Failed to classify first char", K(ret));
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

bool TokenizeContext::is_results_exhaust() const { return result_idx_ >= results_.count(); }

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
  results_.reset();
}

int TokenizeContext::get_next_token(const char *&word,
                                    int64_t &word_len,
                                    int64_t &offset,
                                    int64_t &char_cnt)
{
  int ret = OB_SUCCESS;
  if (result_idx_ < results_.count()) {
    ObIKToken &token = results_.at(result_idx_++);
    if (result_idx_ < results_.count()) {
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
  ObFastSegmentArray<ObIKToken> &list = results_;
  if (is_smart_) {
    if (result_idx_ < list.count()) {
      if (ObIKTokenType::IK_ARABIC_TOKEN == token.type_) {
        ObIKToken &next = list.at(result_idx_);
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
          ++result_idx_;
        }
      }
      // There may be another round of append
      if (OB_SUCC(ret) && result_idx_ < list.count()) {
        ObIKToken &next = list.at(result_idx_);
        bool append = false;
        if (ObIKTokenType::IK_COUNT_TOKEN == next.type_) {
          if (token.offset_ + token.length_ == next.offset_) {
            append = true;
            token.length_ += next.length_;
            token.type_ = ObIKTokenType::IK_CNQUAN_TOKEN;
          }
        }
        if (append) {
          ++result_idx_;
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
