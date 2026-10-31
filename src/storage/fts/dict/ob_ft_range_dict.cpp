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

#include "storage/fts/dict/ob_ft_range_dict.h"
#include "share/rc/ob_server_runtime.h"

#include "storage/fts/dict/ob_ik_dic.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/utility.h"
#include "lib/utility/ob_smart_var.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "storage/fts/dict/ob_ft_cache.h"
#include "storage/fts/dict/ob_ft_cache_container.h"
#include "storage/fts/dict/ob_ft_cache_dict.h"
#include "storage/fts/dict/ob_ft_dat_dict.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_dict_iterator.h"
#include "storage/fts/dict/ob_ft_dict_table_iter.h"
#include "storage/fts/dict/ob_ft_trie.h"

#define USING_LOG_PREFIX STORAGE_FTS

namespace oceanbase
{
namespace storage
{

namespace
{
class ObFTCharMatchCache
{
public:
  static constexpr int64_t CACHE_SIZE = 256;
  static constexpr int64_t MAX_KEY_LEN = 4;

  bool get(const ObFTRangeDict *range_dict,
           const ObString &word,
           ObDATrieHit &hit) const
  {
    if (!cacheable(word)) {
      return false;
    }
    const Entry &entry = entries_[hash(range_dict, word)];
    if (!entry.valid_ || entry.range_dict_ != range_dict
        || entry.len_ != word.length()
        || 0 != MEMCMP(entry.word_, word.ptr(), entry.len_)) {
      return false;
    }
    const int64_t start_pos = hit.start_pos_;
    hit.dict_ = entry.hit_.dict_;
    hit.char_cnt_ = entry.hit_.char_cnt_;
    hit.start_pos_ = start_pos;
    hit.end_pos_ = start_pos + entry.hit_.end_pos_;
    hit.base_idx_ = entry.hit_.base_idx_;
    hit.current_check_ = entry.hit_.current_check_;
    hit.is_match_ = entry.hit_.is_match_;
    hit.is_prefix_ = entry.hit_.is_prefix_;
    return true;
  }

  void put(const ObFTRangeDict *range_dict,
           const ObString &word,
           const ObDATrieHit &hit)
  {
    if (!cacheable(word)) {
      return;
    }
    Entry &entry = entries_[hash(range_dict, word)];
    entry.range_dict_ = range_dict;
    entry.len_ = static_cast<uint8_t>(word.length());
    MEMCPY(entry.word_, word.ptr(), word.length());
    entry.valid_ = true;
    entry.hit_.dict_ = hit.dict_;
    entry.hit_.char_cnt_ = hit.char_cnt_;
    entry.hit_.start_pos_ = 0;
    entry.hit_.end_pos_ = hit.end_pos_ - hit.start_pos_;
    entry.hit_.base_idx_ = hit.base_idx_;
    entry.hit_.current_check_ = hit.current_check_;
    entry.hit_.is_match_ = hit.is_match_;
    entry.hit_.is_prefix_ = hit.is_prefix_;
  }

private:
  static bool cacheable(const ObString &word)
  {
    return word.length() > 0 && word.length() <= MAX_KEY_LEN;
  }

  static uint64_t hash(const ObFTRangeDict *range_dict, const ObString &word)
  {
    uint64_t hash_val = reinterpret_cast<uintptr_t>(range_dict) >> 4;
    for (int64_t i = 0; i < word.length(); ++i) {
      hash_val = hash_val * 131 + static_cast<uint8_t>(word.ptr()[i]);
    }
    return hash_val & (CACHE_SIZE - 1);
  }

  struct Entry
  {
    Entry() : range_dict_(nullptr), len_(0), valid_(false), word_(), hit_(nullptr, 0) {}

    const ObFTRangeDict *range_dict_;
    uint8_t len_;
    bool valid_;
    char word_[MAX_KEY_LEN];
    ObDATrieHit hit_;
  };

  Entry entries_[CACHE_SIZE];
};

ObFTCharMatchCache &char_match_cache()
{
  static thread_local ObFTCharMatchCache cache;
  return cache;
}
} // namespace

int ObFTRangeDict::build_cache_from_ik_dict(const ObFTDictDesc &desc, ObFTCacheRangeContainer &range_container)
{
  int ret = OB_SUCCESS;

  ObIKDictLoader::RawDict raw_dict;
  switch (desc.type_) {
  case ObFTDictType::DICT_IK_MAIN: {
    raw_dict = ObIKDictLoader::dict_text();
  } break;
  case ObFTDictType::DICT_IK_QUAN: {
    raw_dict = ObIKDictLoader::dict_quen_text();
  } break;
  case ObFTDictType::DICT_IK_STOP: {
    raw_dict = ObIKDictLoader::dict_stop();
  } break;
  default:
    ret = OB_NOT_SUPPORTED;
  }

  if (OB_SUCC(ret)) {
    ObIKDictIterator iter(raw_dict);
    if (OB_FAIL(iter.init())) {
    } else if (OB_FAIL(ObFTRangeDict::build_ranges_concurrently_thread_pool(desc, iter, range_container))) {
    }
  }

  return ret;
}

// Thread pool for building DATs concurrently
class DATBuilderThreadPool : public lib::Threads
{
public:
  DATBuilderThreadPool()
      : all_tries_(nullptr),
        desc_(nullptr),
        error_code_(OB_SUCCESS),
        handles_(nullptr)
  {}

  void set_tries(ObVector<ObFTTrie<void> *, ObArenaAllocator> *tries) { all_tries_ = tries; }
  void set_desc(const ObFTDictDesc *desc) { desc_ = desc; }
  int64_t get_range_count() const { return all_tries_ ? all_tries_->size() : 0; }
  int get_error_code() const { return error_code_.load(); }
  void set_handles(ObArray<ObFTCacheRangeHandle *> *handles) { handles_ = handles; }
  void run1() override
  {
    int ret = OB_SUCCESS;
    int64_t idx = get_thread_idx();

    if (OB_ISNULL(all_tries_) || idx >= static_cast<int64_t>(all_tries_->size()) || OB_ISNULL(all_tries_->at(idx))) {
      ret = OB_ARRAY_OUT_OF_RANGE;
    } else if (OB_ISNULL(handles_) || idx >= static_cast<int64_t>(handles_->size()) || OB_ISNULL(handles_->at(idx))) {
      ret = OB_ARRAY_OUT_OF_RANGE;
    } else {

      ObFTTrie<void> *trie = (*all_tries_)[idx];
      ObArenaAllocator dat_alloc(lib::ObMemAttr("DATBuild"));
      ObFTDATBuilder<void> builder(dat_alloc);

      ObFTDAT *dat_buff = nullptr;
      size_t buffer_size = 0;
      ObFTCacheRangeHandle *info = handles_->at(idx);

      if (OB_FAIL(builder.init(*trie))) {
      } else if (OB_FAIL(builder.build_from_trie(*trie))) {
      } else if (OB_FAIL(builder.get_mem_block(dat_buff, buffer_size))) {
      } else if (OB_FAIL(ObFTCacheDict::make_and_fetch_cache_entry(*desc_,
                                                                    dat_buff,
                                                                    buffer_size,
                                                                    static_cast<int32_t>(idx),
                                                                    info->value_,
                                                                    info->handle_))) {
      }
      dat_alloc.reset();
    }

    if (OB_FAIL(ret)) {
      int expected = OB_SUCCESS;
      error_code_.compare_exchange_strong(expected, ret);
    }
  }

private:
  ObVector<ObFTTrie<void> *, ObArenaAllocator> *all_tries_;
  const ObFTDictDesc *desc_;
  std::atomic<int> error_code_;
  ObArray<ObFTCacheRangeHandle *> *handles_;
};

int ObFTRangeDict::build_ranges_concurrently_thread_pool(const ObFTDictDesc &desc,
                                                         ObIFTDictIterator &iter,
                                                         ObFTCacheRangeContainer &range_container)
{
  int ret = OB_SUCCESS;

  // Phase 1: Collect words into tries range by range
  ObArenaAllocator tmp_alloc(lib::ObMemAttr("Tmp Allocator"));
  ObVector<ObFTTrie<void> *, ObArenaAllocator> all_tries(&tmp_alloc);

  bool build_next_range = true;
  while (OB_SUCC(ret) && build_next_range) {
    ObFTTrie<void> *trie = OB_NEWx(ObFTTrie<void>, &tmp_alloc, tmp_alloc, desc.coll_type_);
    if (OB_ISNULL(trie)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      break;
    }

    int count = 0;
    int64_t first_char_len = 0;
    ObFTSingleWord end_char;
    bool range_end = false;

    while (OB_SUCC(ret) && !range_end) {
      ObString key;
      if (OB_FAIL(iter.get_key(key))) {
      } else {
        ++count;

        if (count >= DEFAULT_KEY_PER_RANGE
            && OB_FAIL(ObCharset::first_valid_char(desc.coll_type_,
                                                   key.ptr(),
                                                   key.length(),
                                                   first_char_len))) {
          LOG_WARN("First char is not valid.");
        } else if (DEFAULT_KEY_PER_RANGE == count
                   && OB_FAIL(end_char.set_word(key.ptr(), first_char_len))) {
        } else if (count > DEFAULT_KEY_PER_RANGE
                   && (end_char.get_word() != ObString(first_char_len, key.ptr()))) {
          range_end = true;
        } else {
          if (OB_FAIL(trie->insert(key, {}))) {
          } else if (OB_FAIL(iter.next()) && OB_ITER_END != ret) {
          }
        }
      }
    }

    if (OB_ITER_END == ret) {
      build_next_range = false;
      ret = OB_SUCCESS;
    }

    if (OB_SUCC(ret) && trie->node_num() > 0) {
      if (OB_FAIL(all_tries.push_back(trie))) {
      }
    }
  }

  // Phase 2: Build DATs concurrently using DATBuilderThreadPool
  if (OB_SUCC(ret) && all_tries.size() > 0) {
    ObArray<ObFTCacheRangeHandle *> handles;
    handles.set_attr(lib::ObMemAttr("DATBuild"));
    for (int64_t i = 0; OB_SUCC(ret) && i < all_tries.size(); i++) {
      ObFTCacheRangeHandle *handle = nullptr;
      if (OB_FAIL(range_container.fetch_info_for_dict(handle))) {
      } else if (OB_FAIL(handles.push_back(handle))) {
      }
    }
    if (OB_FAIL(ret)) {
      handles.reset();
    } else {
      DATBuilderThreadPool pool;
      pool.set_run_wrapper(share::server_runtime());
      pool.set_tries(&all_tries);
      pool.set_desc(&desc);
      pool.set_thread_count(static_cast<int64_t>(all_tries.size()));
      pool.set_handles(&handles);

      if (OB_FAIL(pool.start())) {
      } else {
        pool.wait();
        ret = pool.get_error_code();
        if (OB_FAIL(ret)) {
        }
      }
    }
  }

  LOG_INFO("build_ranges_concurrently_thread_pool completed", K(ret), K(all_tries.size()));
  return ret;
}

int ObFTRangeDict::build_one_range(const ObFTDictDesc &desc,
                                   const int32_t range_id,
                                   ObIFTDictIterator &iter,
                                   ObFTCacheRangeContainer &container,
                                   bool &build_next_range)
{
  int ret = OB_SUCCESS;
  build_next_range = true;

  ObArenaAllocator tmp_alloc(lib::ObMemAttr("Temp trie"));

  ObFTDATBuilder<void> builder(tmp_alloc);
  storage::ObFTTrie<void> trie(tmp_alloc, desc.coll_type_);

  int count = 0;
  bool range_end = false;

  int64_t first_char_len = 0;
  ObFTSingleWord end_char;
  ObFTSingleWord start_char;

  ObFTDAT *dat_buff = nullptr;
  size_t buffer_size = 0;

  while (OB_SUCC(ret) && !range_end) {
    ObString key;
    if (OB_FAIL(iter.get_key(key))) {
    } else if (OB_FALSE_IT(++count)) {
      // do nothing
    } else if (count >= DEFAULT_KEY_PER_RANGE
               && OB_FAIL(ObCharset::first_valid_char(desc.coll_type_,
                                                      key.ptr(),
                                                      key.length(),
                                                      first_char_len))) {
      LOG_WARN("First char is not valid.");
    } else if (DEFAULT_KEY_PER_RANGE == count
               && OB_FAIL(end_char.set_word(key.ptr(), first_char_len))) {
    } else if (count > DEFAULT_KEY_PER_RANGE
               && (end_char.get_word() != ObString(first_char_len, key.ptr()))) {
      // end of range, this key is not consumed.
      range_end = true;
    } else if (OB_FAIL(trie.insert(key, {}))) {
    } else if (OB_FAIL(iter.next()) && OB_ITER_END != ret) {
    }
  }

  if (OB_ITER_END == ret) {
    build_next_range = false; // no more data
    ret = OB_SUCCESS;
  }
  ObFTCacheRangeHandle *info = nullptr;

  if (OB_FAIL(ret)) {
    // to do clean up
  } else if (OB_FAIL(builder.init(trie))) {
  } else if (OB_FAIL(builder.build_from_trie(trie))) {
  } else if (OB_FAIL(builder.get_mem_block(dat_buff, buffer_size))) {
  } else if (OB_FAIL(container.fetch_info_for_dict(info))) {
  } else if (OB_FAIL(ObFTCacheDict::make_and_fetch_cache_entry(desc,
                                                               dat_buff,
                                                               buffer_size,
                                                               range_id,
                                                               info->value_,
                                                               info->handle_))) {
  } else {
    // okay
  }
  tmp_alloc.reset();

  return ret;
}

int ObFTRangeDict::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else {
    if (OB_FAIL(build_dict_from_cache(*range_container_))) {
    }
    is_inited_ = true;
  }
  return ret;
}
int ObFTRangeDict::build_ranges(const ObFTDictDesc &desc,
                                ObIFTDictIterator &iter,
                                ObFTCacheRangeContainer &range_container)
{
  int ret = OB_SUCCESS;

  bool build_next_range = true;

  int32_t range_id = 0;
  while (OB_SUCC(ret) && build_next_range) {
    if (OB_FAIL(ObFTRangeDict::build_one_range(desc,
                                               range_id++,
                                               iter,
                                               range_container,
                                               build_next_range))) {
    }
  }
  return ret;
}

int ObFTRangeDict::match(const ObString &single_word, ObDATrieHit &hit) const
{
  int ret = OB_SUCCESS;
  ObIFTDict *dict = nullptr;
  if (char_match_cache().get(this, single_word, hit)) {
  } else if (OB_FAIL(find_first_char_range(single_word, dict)) && OB_ENTRY_NOT_EXIST != ret) {
  } else if (OB_ENTRY_NOT_EXIST == ret) {
    hit.set_unmatch();
    char_match_cache().put(this, single_word, hit);
    ret = OB_SUCCESS;
  } else {
    // do nothing
    hit.dict_ = dict; // set dict
    if (OB_FAIL(dict->match(single_word, hit))) {
    } else {
      char_match_cache().put(this, single_word, hit);
    }
  }
  return ret;
}

int ObFTRangeDict::match(const ObString &words, bool &is_match) const
{
  // find first char range and find dict
  int ret = OB_SUCCESS;
  ObIFTDict *dict = nullptr;

  int64_t char_len;

  if (OB_FAIL(
          ObCharset::first_valid_char(desc_.coll_type_, words.ptr(), words.length(), char_len))) {
  } else if (OB_FAIL(find_first_char_range(ObString(char_len, words.ptr()), dict))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      is_match = false;
      ret = OB_SUCCESS;
    } else {
    }
  } else if (OB_ISNULL(dict)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(dict->match(words, is_match))) {
  }
  return ret;
}

int ObFTRangeDict::match_with_hit(const ObString &single_word,
                                  const ObDATrieHit &last_hit,
                                  ObDATrieHit &hit) const
{
  return last_hit.dict_->match_with_hit(single_word, last_hit, hit);
}

int ObFTRangeDict::find_first_char_range(const ObString &single_word, ObIFTDict *&dict) const
{
  int ret = OB_SUCCESS;
  bool found = false;
  const int64_t range_cnt = range_dicts_.size();
  if (is_sorted_ && range_cnt > 0) {
    // The ranges are sorted and do not overlap, so the range holding the word
    // can be found with a binary search instead of scanning all the ranges.
    // The lookup happens for every single character of the document, so the
    // saved comparisons are worth the extra bookkeeping done at build time.
    int64_t low = 0;
    int64_t high = range_cnt - 1;
    while (low <= high) {
      const int64_t mid = low + (high - low) / 2;
      if (ObCharset::strcmp(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                            range_dicts_[mid].start_.get_word(),
                            single_word)
          > 0) {
        // The word is smaller than the start of this range, and than the
        // start of every following range.
        high = mid - 1;
      } else if (ObCharset::strcmp(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                   range_dicts_[mid].end_.get_word(),
                                   single_word)
                 < 0) {
        // The word is bigger than the end of this range, and than the end of
        // every preceding range.
        low = mid + 1;
      } else {
        dict = range_dicts_[mid].dict_;
        found = true;
        break;
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && !found && i < range_cnt; ++i) {
      if (ObCharset::strcmp(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                            range_dicts_[i].start_.get_word(),
                            single_word)
              <= 0
          && ObCharset::strcmp(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                               range_dicts_[i].end_.get_word(),
                               single_word)
                 >= 0) {
        dict = range_dicts_[i].dict_;
        found = true;
      }
    }
  }
  if (!found) {
    // not found, dis match
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int ObFTRangeDict::build_dict_from_cache(const ObFTCacheRangeContainer &range_container)
{
  int ret = OB_SUCCESS;
  for (ObList<ObFTCacheRangeHandle *, ObIAllocator>::const_iterator iter = range_container.get_handles().begin();
       OB_SUCC(ret) && iter != range_container.get_handles().end();
       iter++) {
    ObFTCacheRangeHandle *ptr = *iter;
    ObFTCacheDict *dict = nullptr;
    ObFTDAT *dat = ptr->value_->dat_block_;
    if (OB_ISNULL(dict = OB_NEWx(ObFTCacheDict, &range_alloc_, ObCollationType::CS_TYPE_UTF8MB4_BIN, dat))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      ObFTRange range;
      range.start_ = dat->start_word_;
      range.end_ = dat->end_word_;
      range.dict_ = dict;
      if (OB_FAIL(range_dicts_.push_back(range))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    // Check whether the ranges are sorted and do not overlap. Only then the
    // range of a word can be found with a binary search.
    is_sorted_ = true;
    for (int64_t i = 1; i < range_dicts_.size(); ++i) {
      if (ObCharset::strcmp(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                            range_dicts_[i - 1].start_.get_word(),
                            range_dicts_[i].start_.get_word())
              > 0
          || ObCharset::strcmp(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                               range_dicts_[i - 1].end_.get_word(),
                               range_dicts_[i].start_.get_word())
                 >= 0) {
        is_sorted_ = false;
        break;
      }
    }
  }
  return ret;
}

int ObFTRangeDict::build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &range_container)
{
  int ret = OB_SUCCESS;

  ObString table_name;
  switch (desc.type_) {
  case ObFTDictType::DICT_IK_MAIN: {
    table_name = ObString(share::OB_FT_DICT_IK_UTF8_TNAME);
  } break;
  case ObFTDictType::DICT_IK_QUAN: {
    table_name = ObString(share::OB_FT_QUANTIFIER_IK_UTF8_TNAME);
  } break;
  case ObFTDictType::DICT_IK_STOP: {
    table_name = ObString(share::OB_FT_STOPWORD_IK_UTF8_TNAME);
  } break;
  default:
    ret = OB_NOT_SUPPORTED;
  }

  if (OB_SUCC(ret)) {
    SMART_VAR(ObISQLClient::ReadResult, result)
    {
      ObFTDictTableIter iter_table(result);
      if (OB_FAIL(iter_table.init(table_name))) {
      } else if (OB_FAIL(ObFTRangeDict::build_ranges(desc, iter_table, range_container))) {
      }
    }
  }

  return ret;
}

int ObFTRangeDict::try_load_cache(const ObFTDictDesc &desc,
                                  const uint32_t range_count,
                                  ObFTCacheRangeContainer &range_container)
{
  int ret = OB_SUCCESS;
  uint64_t name = static_cast<uint64_t>(desc.type_);

  for (int64_t i = 0; OB_SUCC(ret) && i < range_count; ++i) {
    ObDictCacheKey key(name, desc.type_, i);
    ObFTCacheRangeHandle *info = nullptr;
    if (OB_FAIL(range_container.fetch_info_for_dict(info))) {
    } else if (OB_FAIL(ObDictCache::get_instance().get_dict(key, info->value_, info->handle_))
               && OB_ENTRY_NOT_EXIST != ret) {
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      range_container.reset();
      // not found, build cache outthere
    } else if (FALSE_IT(info->type_ = desc.type_)) {
      // impossible
    }
  }

  if (OB_FAIL(ret)) {
    range_container.reset();
  }

  return ret;
}

} //  namespace storage
} //  namespace oceanbase
