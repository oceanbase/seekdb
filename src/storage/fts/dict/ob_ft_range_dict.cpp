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

#include "storage/fts/dict/ob_ik_dic.h"
#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/utility.h"
#include "ob_smart_var.h"
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
    LOG_WARN("Not supported dict type.", K(ret));
  }

  if (OB_SUCC(ret)) {
    ObIKDictIterator iter(raw_dict);
    if (OB_FAIL(iter.init())) {
      LOG_WARN("Failed to init iterator.", K(ret));
    } else if (OB_FAIL(ObFTRangeDict::build_ranges_concurrently_thread_pool(desc, iter, range_container))) {
      LOG_WARN("Failed to build ranges.", K(ret));
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
        container_(nullptr),
        error_code_(OB_SUCCESS)
  {}

  void set_tries(ObVector<ObFTTrie<void> *, ObArenaAllocator> *tries) { all_tries_ = tries; }
  void set_desc(const ObFTDictDesc *desc) { desc_ = desc; }
  void set_container(ObFTCacheRangeContainer *container) { container_ = container; }
  int64_t get_range_count() const { return all_tries_ ? all_tries_->size() : 0; }
  int get_error_code() const { return error_code_.load(); }

  void run1() override
  {
    int ret = OB_SUCCESS;
    int64_t idx = get_thread_idx();

    if (OB_ISNULL(all_tries_) || idx >= static_cast<int64_t>(all_tries_->size())) {
      return;
    }

    ObFTTrie<void> *trie = (*all_tries_)[idx];
    ObArenaAllocator dat_alloc(lib::ObMemAttr(OB_SERVER_TENANT_ID, "DATBuild"));
    ObFTDATBuilder<void> builder(dat_alloc);

    ObFTDAT *dat_buff = nullptr;
    size_t buffer_size = 0;
    ObFTCacheRangeHandle *info = nullptr;

    if (OB_FAIL(builder.init(*trie))) {
      LOG_WARN("Failed to init builder.", K(ret), K(idx));
    } else if (OB_FAIL(builder.build_from_trie(*trie))) {
      LOG_WARN("Failed to build datrie.", K(ret), K(idx));
    } else if (OB_FAIL(builder.get_mem_block(dat_buff, buffer_size))) {
      LOG_WARN("Failed to get mem block.", K(ret), K(idx));
    } else if (OB_FAIL(container_->fetch_info_for_dict(info))) {
      LOG_WARN("Failed to fetch info for dict.", K(ret), K(idx));
    } else if (OB_FAIL(ObFTCacheDict::make_and_fetch_cache_entry(*desc_,
                                                                  dat_buff,
                                                                  buffer_size,
                                                                  static_cast<int32_t>(idx),
                                                                  info->value_,
                                                                  info->handle_))) {
      LOG_WARN("Failed to put dict into kv cache", K(ret), K(idx));
    }

    if (OB_FAIL(ret)) {
      int expected = OB_SUCCESS;
      error_code_.compare_exchange_strong(expected, ret);
    }

    dat_alloc.reset();
  }

private:
  ObVector<ObFTTrie<void> *, ObArenaAllocator> *all_tries_;
  const ObFTDictDesc *desc_;
  ObFTCacheRangeContainer *container_;
  std::atomic<int> error_code_;
};

int ObFTRangeDict::build_ranges_concurrently_thread_pool(const ObFTDictDesc &desc,
                                                         ObIFTDictIterator &iter,
                                                         ObFTCacheRangeContainer &range_container)
{
  int ret = OB_SUCCESS;

  // Phase 1: Collect words into tries range by range
  ObArenaAllocator tmp_alloc(lib::ObMemAttr(MTL_ID(), "Tmp Allocator"));
  ObVector<ObFTTrie<void> *, ObArenaAllocator> all_tries(&tmp_alloc);

  bool build_next_range = true;
  while (OB_SUCC(ret) && build_next_range) {
    ObFTTrie<void> *trie = OB_NEWx(ObFTTrie<void>, &tmp_alloc, tmp_alloc, desc.coll_type_);
    if (OB_ISNULL(trie)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to allocate ObFTTrie", K(ret));
      break;
    }

    int count = 0;
    int64_t first_char_len = 0;
    ObFTSingleWord end_char;
    bool range_end = false;

    while (OB_SUCC(ret) && !range_end) {
      ObString key;
      if (OB_FAIL(iter.get_key(key))) {
        LOG_WARN("Failed to get key", K(ret));
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
          LOG_WARN("Failed to record first char.", K(ret));
        } else if (count > DEFAULT_KEY_PER_RANGE
                   && (end_char.get_word() != ObString(first_char_len, key.ptr()))) {
          range_end = true;
        } else {
          if (OB_FAIL(trie->insert(key, {}))) {
            LOG_WARN("Failed to insert key to trie", K(ret));
          } else if (OB_FAIL(iter.next()) && OB_ITER_END != ret) {
            LOG_WARN("Failed to step to next word entry.", K(ret));
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
        LOG_WARN("Failed to push back trie", K(ret));
      }
    }
  }

  // Phase 2: Build DATs concurrently using DATBuilderThreadPool
  if (OB_SUCC(ret) && all_tries.size() > 0) {
    DATBuilderThreadPool pool;
    pool.set_tries(&all_tries);
    pool.set_desc(&desc);
    pool.set_container(&range_container);
    pool.set_thread_count(static_cast<int64_t>(all_tries.size()));

    if (OB_FAIL(pool.start())) {
      LOG_WARN("Failed to start thread pool", K(ret));
    } else {
      pool.wait();
      ret = pool.get_error_code();
      if (OB_FAIL(ret)) {
        LOG_WARN("Thread pool encountered error", K(ret));
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

  ObArenaAllocator tmp_alloc(lib::ObMemAttr(MTL_ID(), "Temp trie"));

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
      LOG_WARN("Failed to get key", K(ret));
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
      LOG_WARN("Failed to record first char.", K(ret));
    } else if (count > DEFAULT_KEY_PER_RANGE
               && (end_char.get_word() != ObString(first_char_len, key.ptr()))) {
      // end of range, this key is not consumed.
      range_end = true;
    } else if (OB_FAIL(trie.insert(key, {}))) {
      LOG_WARN("Failed to insert key to trie", K(ret));
    } else if (OB_FAIL(iter.next()) && OB_ITER_END != ret) {
      LOG_WARN("Failed to step to next word entry.", K(ret));
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
    LOG_WARN("Failed to build dat.", K(ret));
  } else if (OB_FAIL(builder.build_from_trie(trie))) {
    LOG_WARN("Failed to build datrie.", K(ret));
  } else if (OB_FAIL(builder.get_mem_block(dat_buff, buffer_size))) {
    LOG_WARN("Failed to get mem block.", K(ret));
  } else if (OB_FAIL(container.fetch_info_for_dict(info))) {
    LOG_WARN("Failed to fetch info for dict.", K(ret));
  } else if (OB_FAIL(ObFTCacheDict::make_and_fetch_cache_entry(desc,
                                                               dat_buff,
                                                               buffer_size,
                                                               range_id,
                                                               info->value_,
                                                               info->handle_))) {
    LOG_WARN("Failed to put dict into kv cache");
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
      LOG_WARN("Failed to build dict from cache.", K(ret));
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
      LOG_WARN("fail to build range", K(ret));
    }
  }
  return ret;
}

int ObFTRangeDict::match(const ObString &single_word, ObDATrieHit &hit) const
{
  int ret = OB_SUCCESS;
  ObIFTDict *dict = nullptr;
  if (OB_FAIL(find_first_char_range(single_word, dict)) && OB_ENTRY_NOT_EXIST != ret) {
    LOG_WARN("Failed to find first char range.", K(ret));
  } else if (OB_ENTRY_NOT_EXIST == ret) {
    hit.set_unmatch();
    ret = OB_SUCCESS;
  } else {
    // do nothing
    hit.dict_ = dict; // set dict
    if (OB_FAIL(dict->match(single_word, hit))) {
      LOG_WARN("Failed to match.", K(ret));
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
    LOG_WARN("Failed to find first char", K(ret));
  } else if (OB_FAIL(find_first_char_range(ObString(char_len, words.ptr()), dict))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      is_match = false;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("Failed to find first char range.", K(ret));
    }
  } else if (OB_ISNULL(dict)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dict is null.", K(ret));
  } else if (OB_FAIL(dict->match(words, is_match))) {
    LOG_WARN("Failed to match.", K(ret));
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
  for (int i = 0; OB_SUCC(ret) && !found && i < range_dicts_.size(); ++i) {
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
      LOG_WARN("Failed to alloc memory.", K(ret));
    } else {
      ObFTRange range;
      range.start_ = dat->start_word_;
      range.end_ = dat->end_word_;
      range.dict_ = dict;
      if (OB_FAIL(range_dicts_.push_back(range))) {
        LOG_WARN("Failed to push back range dict.", K(ret));
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
    LOG_WARN("Not supported dict type.", K(ret));
  }

  if (OB_SUCC(ret)) {
    SMART_VAR(ObISQLClient::ReadResult, result)
    {
      ObFTDictTableIter iter_table(result);
      if (OB_FAIL(iter_table.init(table_name))) {
        LOG_WARN("Failed to init iterator.", K(ret));
      } else if (OB_FAIL(ObFTRangeDict::build_ranges(desc, iter_table, range_container))) {
        LOG_WARN("Failed to build ranges.", K(ret));
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
    ObDictCacheKey key(name, MTL_ID(), desc.type_, i);
    ObFTCacheRangeHandle *info = nullptr;
    if (OB_FAIL(range_container.fetch_info_for_dict(info))) {
      LOG_WARN("Failed to fetch info for dict.", K(ret));
    } else if (OB_FAIL(ObDictCache::get_instance().get_dict(key, info->value_, info->handle_))
               && OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("Failed to get dict from kv cache.", K(ret));
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
