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

#include "object/ob_object.h"
#define USING_LOG_PREFIX STORAGE_FTS

#include "share/rc/ob_server_runtime.h"
#include "storage/fts/ob_fts_stop_word.h"
#include "storage/fts/ob_fts_parser_helper.h"
#include "storage/fts/ob_fts_parser_property.h"

namespace oceanbase
{
namespace storage
{

////////////////////////////////////////////////////////////////////////////////
// class ObStopWordChecker
ObStopWordChecker::~ObStopWordChecker()
{
  destroy();
}

int ObStopWordChecker::init()
{
  int ret = OB_SUCCESS;
  
  

  if (inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(stopword_set_.create(DEFAULT_STOPWORD_BUCKET_NUM, "StopWordSet", "StopWordSet"))) {
  } else {
    ObObjMeta stop_meta;
    stop_meta.set_varchar();
    stop_meta.set_collation_type(ObCollationType::CS_TYPE_UTF8MB4_GENERAL_CI);

    stopword_type_.set_meta(stop_meta);
    const int64_t stopword_count = sizeof(ob_stop_word_list) / sizeof(ob_stop_word_list[0]);
    for (int64_t i = 0; OB_SUCC(ret) && i < stopword_count; ++i) {
      ObFTWord stopword(STRLEN(ob_stop_word_list[i]), ob_stop_word_list[i], stopword_type_);
      if (OB_FAIL(stopword_set_.set_refactored(stopword))) {
      }
    }

    if (OB_SUCC(ret)) {
      inited_ = true;
    }
  }
  return ret;
}

void ObStopWordChecker::destroy()
{
  if (inited_) {
    stopword_set_.destroy();
    inited_ = false;
  }
}

int ObStopWordChecker::exist_refactored(const ObFTWord &word, bool &is_stopword)
{
  int ret = OB_SUCCESS;
  ret = stopword_set_.exist_refactored(word);
  if (OB_HASH_NOT_EXIST == ret) {
    is_stopword = false;
    ret = OB_SUCCESS;
  } else if (OB_HASH_EXIST == ret) {
    is_stopword = true;
    ret = OB_SUCCESS;
  } else if (OB_SUCC(ret)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
  }
  return ret;
}

int ObStopWordChecker::check_stopword(const ObFTWord &word, bool &is_stopword)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(word.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const common::ObString &src_str = word.get_word().get_string();
    const common::ObCollationType src_coll = word.get_collation_type();
    const common::ObCollationType dst_coll = stopword_type_.get_collation_type();
    if (common::ObCharset::charset_type_by_coll(src_coll)
        == common::ObCharset::charset_type_by_coll(dst_coll)) {
      // Same charset type (e.g. utf8mb4 column vs utf8mb4 stopword list): the
      // conversion below is the identity mapping, so skip both the per-token
      // ObArenaAllocator construction and the copy. This is the hot path of
      // tokenize() and of every fulltext index build.
      ObFTWord converted(src_str.length(), src_str.ptr(), stopword_type_);
      ret = exist_refactored(converted, is_stopword);
    } else {
      common::ObString cmp_str;
      common::ObArenaAllocator allocator(lib::ObMemAttr("ChkStopWord"));
      if (OB_FAIL(common::ObCharset::charset_convert(
                                       allocator,
                                       src_str,
                                       src_coll,
                                       dst_coll,
                                       cmp_str))) {
      } else {
        ObFTWord converted(cmp_str.length(), cmp_str.ptr(), stopword_type_);
        ret = exist_refactored(converted, is_stopword);
      }
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// class ObAddWord
ObAddWord::ObAddWord(
    const ObFTParserProperty &property,
    const ObObjMeta &meta,
    const ObAddWordFlag &flag,
    common::ObIAllocator &allocator,
    ObFTWordMap &word_map)
  : word_meta_(meta),
    allocator_(allocator),
    word_map_(word_map),
    min_max_word_cnt_(0),
    non_stopword_cnt_(0),
    stopword_cnt_(0),
    min_token_size_(property.min_token_size_),
    max_token_size_(property.max_token_size_),
    flag_(flag),
    cs_info_(common::ObCharset::get_charset(meta.get_collation_type())),
    cased_buf_pos_(nullptr),
    cased_buf_end_(nullptr)
{
}

int ObAddWord::process_word(
    const char *word,
    const int64_t word_len,
    const int64_t char_cnt,
    const int64_t word_freq)
{
  int ret = OB_SUCCESS;
  bool is_stopword = false;
  ObFTWord src_word(word_len, word, word_meta_);
  ObFTWord dst_word;
  if (OB_ISNULL(word) || OB_UNLIKELY(0 >= word_len || 0 >= char_cnt || 0 >= word_freq)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_min_max_word(char_cnt)) {
    ++min_max_word_cnt_;
  } else if (OB_FAIL(casedown_word(src_word, dst_word))) {
  } else if (OB_FAIL(check_stopword(dst_word, is_stopword))) {
  } else if (OB_UNLIKELY(is_stopword)) {
    ++stopword_cnt_;
  } else if (OB_FAIL(groupby_word(dst_word, word_freq))) {
  } else {
    non_stopword_cnt_ += word_freq;
  }
  return ret;
}

bool ObAddWord::is_min_max_word(const int64_t c_len) const
{
  return flag_.min_max_word() && (c_len < min_token_size_ || c_len > max_token_size_);
}

int ObAddWord::get_cased_word_buf(const int64_t len, char *&buf)
{
  int ret = OB_SUCCESS;
  buf = nullptr;
  if (OB_UNLIKELY(len <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_NOT_NULL(cased_buf_pos_) && len <= cased_buf_end_ - cased_buf_pos_) {
    buf = cased_buf_pos_;
    cased_buf_pos_ += len;
  } else if (OB_UNLIKELY(len >= CASED_WORD_BUFFER_SIZE)) {
    // Rare: one word longer than a whole chunk, give it its own allocation.
    if (OB_ISNULL(buf = static_cast<char *>(allocator_.alloc(len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
  } else if (OB_ISNULL(cased_buf_pos_ = static_cast<char *>(allocator_.alloc(CASED_WORD_BUFFER_SIZE)))) {
    cased_buf_end_ = nullptr;
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    cased_buf_end_ = cased_buf_pos_ + CASED_WORD_BUFFER_SIZE;
    buf = cased_buf_pos_;
    cased_buf_pos_ += len;
  }
  return ret;
}

int ObAddWord::casedown_word(const ObFTWord &src, ObFTWord &dst)
{
  int ret = OB_SUCCESS;
  const ObString src_str = src.get_word().get_string();
  if (OB_UNLIKELY(src.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!flag_.casedown()) {
    dst = src;
  } else if (OB_ISNULL(cs_info_) || OB_UNLIKELY(0 == cs_info_->casedn_multiply)) {
    // Unknown collation (or degenerate conversion info): the generic path below
    // reproduces the exact previous behavior, including its error code.
    ObString dst_str;
    if (OB_FAIL(common::ObCharset::tolower(word_meta_.get_collation_type(), src_str, dst_str, allocator_))) {
    } else {
      dst = ObFTWord(dst_str.length(), dst_str.ptr(), word_meta_);
    }
  } else {
    // Same conversion as ObCharset::tolower(): convert into `allocator_` memory
    // and keep `size` bytes of it, except that the word buffer is carved out of
    // a bigger chunk so a document does not pay one arena allocation per word,
    // and the charset info is the one cached by the constructor instead of a
    // get_charset() call per word.
    const int64_t mult = static_cast<int64_t>(cs_info_->casedn_multiply);
    const int64_t dst_len = static_cast<int64_t>(src_str.length()) * mult;
    char *buf = nullptr;
    if (OB_FAIL(get_cased_word_buf(dst_len, buf))) {
    } else {
      char *cased_src = const_cast<char *>(src_str.ptr());
      if (1 == mult) {
        // Match ObCharset::tolower(): some handlers require in-place conversion,
        // and the binary handler does not write its destination at all.
        MEMCPY(buf, src_str.ptr(), src_str.length());
        cased_src = buf;
      }
      const size_t size = cs_info_->cset->casedn(cs_info_,
                                                 cased_src,
                                                 static_cast<size_t>(src_str.length()),
                                                 buf,
                                                 static_cast<size_t>(dst_len));
      dst = ObFTWord(static_cast<int64_t>(size), buf, word_meta_);
    }
  }
  return ret;
}

int ObAddWord::check_stopword(const ObFTWord &ft_word, bool &is_stopword)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ft_word.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (flag_.stopword()) {
    ObStopWordChecker *stop_word_checker = ObFTParseData::instance().stop_word_checker();
    if (OB_ISNULL(stop_word_checker)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(stop_word_checker->check_stopword(ft_word, is_stopword))) {
    }
  }
  return ret;
}

int ObAddWord::groupby_word(const ObFTWord &word, const int64_t word_freq)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(word.empty() || word_freq <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!flag_.groupby_word()) {
    if (OB_FAIL(word_map_.set_refactored(word, 1/*word count*/))) {
    }
  } else {
    // Preserve the last spelling of collation-equivalent words, as overwrite did.
    auto add_frequency = [&word, word_freq](common::hash::HashMapPair<ObFTWord, int64_t> &entry) {
      entry.first = word;
      entry.second += word_freq;
    };
    if (OB_FAIL(word_map_.set_or_update(word, 1/*initial word count*/, add_frequency))) {
    }
  }
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
