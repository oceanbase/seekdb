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
// set_or_update() callback: folds this occurrence's count into the existing map
// entry. When the stored key already carries this occurrence's bytes -- the
// common case, a word repeated verbatim -- the count is updated in place and
// the stored key is left alone (no copy, no re-point). When the bytes differ
// even though the keys compare equal under the collation (e.g. two different
// emoji under utf8mb4_general_ci) the pair has to be replaced afterwards so
// that the key bytes follow the last occurrence, which is what the upstream
// set_refactored(word, count, 1) overwrite did. The replacement cannot run
// inside this callback because the callback holds the bucket write lock.
// ObHashMap::set_or_update hands the callback the whole (key, value) pair (the
// hashtable's default pre_proc is the identity on the pair), hence the pair
// parameter and the direct access to pair.first.
class WordCountUpdater
{
public:
  WordCountUpdater(const ObFTWord &word, const int64_t word_freq)
    : word_(word),
      word_freq_(word_freq),
      need_replace_(false),
      updated_count_(0)
  {}
  int operator()(common::hash::HashMapPair<ObFTWord, int64_t> &pair)
  {
    const ObString stored = pair.first.get_word().get_string();
    const ObString incoming = word_.get_word().get_string();
    if (stored.compare_equal(incoming)) {
      pair.second += word_freq_;
    } else {
      need_replace_ = true;
      updated_count_ = pair.second + word_freq_;
    }
    return OB_SUCCESS;
  }
  bool need_replace() const { return need_replace_; }
  int64_t get_updated_count() const { return updated_count_; }
private:
  // the key that would be stored if this occurrence has to replace the entry
  const ObFTWord &word_;
  int64_t word_freq_;
  bool need_replace_;
  int64_t updated_count_;
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
    // Single map operation per token on the common path: set_or_update()
    // hashes the key once and either inserts (key, 1) for a distinct word or
    // runs the in-place update callback for a repeated one. The original
    // get-then-set flow hashed the same key twice on every token.
    //
    // The token bytes may live in a parser-owned buffer that is reused by the
    // next get_next_token() call (e.g. the beng normalizer buffer), so the key
    // is materialized into the parse allocator before it can be stored. With
    // the casedown flag on, `word` is already a fresh copy produced by
    // casedown_word() in this parse allocator (it can never alias the
    // transient token buffer), so it serves as the map key directly and the
    // per-word memcpy is skipped. Any other producer still goes through
    // dup_word(); on the update path the copied bytes are only used when the
    // key has to be replaced, and are otherwise left unused in the parse
    // arena, which is reset wholesale at the end of the call.
    //
    // Key bytes follow the last collation-equal occurrence, which is the
    // upstream behavior. Upstream (5bce9e06d) copied every beng token into the
    // parse arena inside ObBEngFTParser::get_next_token() and then called
    // set_refactored(word, count, 1/*overwrite*/) on every occurrence; the
    // overwrite replaces the whole (key, value) pair, so the surviving key is
    // a stable copy of the bytes of the last occurrence. That is deterministic
    // last-wins, not a read of recycled memory: each token had its own arena
    // copy that lived until the call returned. a320797ef dropped the
    // per-token copy and materialized the key only on the first insertion,
    // which turned the semantics into first-wins; d3cff803c (set_or_update)
    // kept first-wins. The difference was observable only for keys that compare
    // equal under the collation but differ in bytes (e.g. two different emoji
    // under utf8mb4_general_ci): 14 of the 476 special-character corpus cases,
    // none of the 52 frozen TOKENIZE snapshot rows, the official fts_index
    // cases or the benchmark texts.
    //
    // This implementation restores last-wins. A repeated word whose bytes match
    // the stored key is counted in place, without copying or re-pointing the
    // key; a repeated word whose bytes differ replaces the (key, value) pair
    // with the materialized bytes of this occurrence through the same
    // set_refactored(..., 1/*overwrite*/) pair replacement upstream used, so
    // the bucket position, the chain order and therefore the iteration order
    // stay the same. The replacement runs after set_or_update() returns
    // because the callback holds the bucket lock.
    ObFTWord stable_word;
    if (flag_.casedown()) {
      stable_word = word;
    } else if (OB_FAIL(dup_word(word, stable_word))) {
      // ret already carries the dup_word failure
    }
    if (OB_SUCC(ret)) {
      WordCountUpdater updater(stable_word, word_freq);
      if (OB_FAIL(word_map_.set_or_update(stable_word, 1/*initial word count*/, updater))) {
      } else if (updater.need_replace()
                 && OB_FAIL(word_map_.set_refactored(stable_word,
                                                     updater.get_updated_count(),
                                                     1/*overwrite*/))) {
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
