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

int ObStopWordChecker::check_stopword(const ObFTWord &word, bool &is_stopword)
{
  int ret = OB_SUCCESS;
  
  
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(word.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (CHARSET_UTF8MB4
             == common::ObCharset::charset_type_by_coll(word.get_collation_type())) {
    // Same-charset fast path: with equal charsets charset_convert() aliases the
    // input bytes (no copy), so hashing the raw bytes under the stopword
    // collation yields exactly the same hash and comparison result as the
    // converted word. This keeps the per-token check allocation free while
    // preserving the case-insensitive semantics of the GENERAL_CI lookup.
    ObFTWord converted(word.get_word().get_string().length(),
                       word.get_word().get_string().ptr(),
                       stopword_type_);
    ret = stopword_set_.exist_refactored(converted);
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
  } else {
    common::ObArenaAllocator allocator(lib::ObMemAttr("ChkStopWord"));
    common::ObString cmp_str;
    // do nothing set out with in if type is the same.
    if (OB_FAIL(common::ObCharset::charset_convert(
                                       allocator,
                                       word.get_word().get_string(),
                                       word.get_collation_type(),
                                       stopword_type_.get_collation_type(),
                                       cmp_str))) {
    } else {
      ObFTWord converted(cmp_str.length(), cmp_str.ptr(), stopword_type_);
      ret = stopword_set_.exist_refactored(converted);
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
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////
// class ObAddWord


namespace
{
// set_or_update() callback: adds this occurrence's count to the existing map
// entry in place. It never touches the stored key, so the key bytes that were
// materialized into the parse allocator stay stable. ObHashMap::set_or_update
// hands the callback the whole (key, value) pair (the hashtable's default
// pre_proc is the identity on the pair), hence the pair parameter.
class WordCountUpdater
{
public:
  explicit WordCountUpdater(const int64_t word_freq) : word_freq_(word_freq)
  {}
  int operator()(common::hash::HashMapPair<ObFTWord, int64_t> &pair)
  {
    pair.second += word_freq_;
    return OB_SUCCESS;
  }
private:
  int64_t word_freq_;
};
}  // anonymous namespace

template <class MapType>
ObAddWordT<MapType>::ObAddWordT(
    const ObFTParserProperty &property,
    const ObObjMeta &meta,
    const ObAddWordFlag &flag,
    common::ObIAllocator &allocator,
    MapType &word_map)
  : word_meta_(meta),
    allocator_(allocator),
    word_map_(word_map),
    min_max_word_cnt_(0),
    non_stopword_cnt_(0),
    stopword_cnt_(0),
    min_token_size_(property.min_token_size_),
    max_token_size_(property.max_token_size_),
    flag_(flag)
{
}

template <class MapType>
int ObAddWordT<MapType>::process_word(
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

template <class MapType>
bool ObAddWordT<MapType>::is_min_max_word(const int64_t c_len) const
{
  return flag_.min_max_word() && (c_len < min_token_size_ || c_len > max_token_size_);
}

template <class MapType>
int ObAddWordT<MapType>::casedown_word(const ObFTWord &src, ObFTWord &dst)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(src.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (flag_.casedown()) {
    ObString dst_str;
    if (OB_FAIL(ObCharset::tolower(
                    word_meta_.get_collation_type(),
                    src.get_word().get_string(),
                    dst_str,
                    allocator_))) {
    } else {
      ObFTWord tmp(dst_str.length(), dst_str.ptr(), word_meta_);
      dst = tmp;
    }
  } else {
    dst = src;
  }
  return ret;
}

template <class MapType>
int ObAddWordT<MapType>::check_stopword(const ObFTWord &ft_word, bool &is_stopword)
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

template <class MapType>
int ObAddWordT<MapType>::groupby_word(const ObFTWord &word, const int64_t word_freq)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(word.empty() || word_freq <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!flag_.groupby_word()) {
    if (OB_FAIL(word_map_.set_refactored(word, 1/*word count*/))) {
    }
  } else {
    // Single map operation per token: set_or_update() hashes the key once and
    // either inserts (key, 1) for a distinct word or runs the in-place update
    // callback for a repeated one. The previous get-then-set flow hashed the
    // same key twice on every token (insert: get + set, update: get + get).
    //
    // The token bytes may live in a parser-owned buffer that is reused by the
    // next get_next_token() call (e.g. the beng normalizer buffer), so the key
    // is materialized into the parse allocator before it can be stored. With
    // the casedown flag on, `word` is already a fresh copy produced by
    // casedown_word() in this parse allocator (it can never alias the
    // transient token buffer), so it serves as the map key directly and the
    // per-word memcpy is skipped. Any other producer still goes through
    // dup_word(); on the update path the copied bytes are simply left unused
    // in the parse arena, which is reset wholesale at the end of the call.
    // set_or_update() never re-points the stored key on the update path, so
    // the aggregation result is identical to the previous flow and the map
    // insertion order (hence the TOKENIZE output order) is unchanged.
    ObFTWord stable_word;
    if (flag_.casedown()) {
      stable_word = word;
    } else if (OB_FAIL(dup_word(word, stable_word))) {
      // ret already carries the dup_word failure
    }
    if (OB_SUCC(ret)) {
      WordCountUpdater updater(word_freq);
      if (OB_FAIL(word_map_.set_or_update(stable_word, 1/*initial word count*/, updater))) {
      }
    }
  }
  return ret;
}

template <class MapType>
int ObAddWordT<MapType>::dup_word(const ObFTWord &src, ObFTWord &dst)
{
  int ret = OB_SUCCESS;
  const ObString src_str = src.get_word().get_string();
  char *buf = nullptr;
  if (OB_UNLIKELY(0 >= src_str.length())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator_.alloc(src_str.length())))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(buf, src_str.ptr(), src_str.length());
    dst = ObFTWord(src_str.length(), buf, word_meta_);
  }
  return ret;
}

template class ObAddWordT<ObFTWordMap>;
template class ObAddWordT<ObFTWordMapLocked>;

} // end namespace storage
} // end namespace oceanbase
