/**
 * Copyright (c) 2024 OceanBase
 * SPDX-License-Identifier: Apache-2.0
 */

#include "storage/fts/dict/ob_ft_range_dict.h"

#include "lib/allocator/page_arena.h"
#include "lib/charset/ob_charset.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/container/ob_array.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_sql_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_smart_var.h"
#include "lib/utility/utility.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "storage/fts/dict/ob_ft_cache.h"
#include "storage/fts/dict/ob_ft_cache_container.h"
#include "storage/fts/dict/ob_ft_cache_dict.h"
#include "storage/fts/dict/ob_ft_dat_dict.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ik_dic.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_dict_iterator.h"
#include "storage/fts/dict/ob_ft_dict_table_iter.h"
#include "storage/fts/dict/ob_ft_trie.h"
#include "storage/tx/ob_trans_service.h"

#define USING_LOG_PREFIX STORAGE_FTS

namespace oceanbase
{
namespace storage
{
int ObFTRangeDict::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else {
    if (OB_FAIL(build_dict_from_cache(*range_container_))) {
      LOG_WARN("Failed to build dict from cache", K(ret));
    }
    is_inited_ = true;
  }
  return ret;
}

int ObFTRangeDict::match(const ObString &single_word, ObDATrieHit &hit) const
{
  int ret = OB_SUCCESS;
  ObIFTDict *dict = nullptr;
  if (OB_FAIL(find_first_char_range(single_word, dict)) && OB_ENTRY_NOT_EXIST != ret) {
    LOG_WARN("Failed to find first char range", K(ret));
  } else if (OB_ENTRY_NOT_EXIST == ret) {
    hit.set_unmatch();
    ret = OB_SUCCESS;
  } else {
    // do nothing
    hit.dict_ = dict; // set dict
    if (OB_FAIL(dict->match(single_word, hit))) {
      LOG_WARN("Failed to match", K(ret));
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
      LOG_WARN("Failed to find first char range", K(ret));
    }
  } else if (OB_UNLIKELY(OB_ISNULL(dict))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Dict is null", K(ret));
  } else if (OB_FAIL(dict->match(words, is_match))) {
    LOG_WARN("Failed to match", K(ret));
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
  int64_t left = 0;
  int64_t right = range_dicts_.size() - 1;
  int64_t candidate = -1;
  dict = nullptr;
  while (left <= right) {
    const int64_t mid = left + ((right - left) >> 1);
    const ObFTRange &mid_range = range_dicts_[mid];
    const int cmp_start_ret = ObCharset::strcmp(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                                mid_range.start_.get_token(),
                                                single_word);
    if (cmp_start_ret <= 0) {
      candidate = mid;
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }

  if (OB_LIKELY(candidate >= 0)) {
    const ObFTRange &range = range_dicts_[candidate];
    const int cmp_end_ret = ObCharset::strcmp(ObCollationType::CS_TYPE_UTF8MB4_BIN,
                                              range.end_.get_token(),
                                              single_word);
    if (cmp_end_ret >= 0) {
      dict = range.dict_;
    } else {
      ret = OB_ENTRY_NOT_EXIST;
    }
  } else {
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
    const ObFTDAT *dat = ptr->value_->dat_block_;
    if (OB_ISNULL(dict = OB_NEWx(ObFTCacheDict, &range_alloc_, ObCollationType::CS_TYPE_UTF8MB4_BIN, dat))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to alloc memory", K(ret));
    } else {
      ObFTRange range;
      range.start_ = dat->start_token_;
      range.end_ = dat->end_token_;
      range.dict_ = dict;
      if (OB_FAIL(range_dicts_.push_back(range))) {
        LOG_WARN("Failed to push back range dict", K(ret));
      }
    }
  }
  return ret;
}

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
  void set_handles(ObArray<ObFTCacheRangeHandle *> *handles) { handles_ = handles; }
  int get_error_code() const { return error_code_.load(); }
  void run1() override
  {
    int ret = OB_SUCCESS;
    int64_t idx = get_thread_idx();

    if (OB_ISNULL(all_tries_) || idx >= static_cast<int64_t>(all_tries_->size()) || OB_ISNULL(all_tries_->at(idx))) {
      ret = OB_ARRAY_OUT_OF_RANGE;
      LOG_WARN("all_tries_ is null or idx is out of range", K(idx), K(all_tries_->size()));
    } else if (OB_ISNULL(handles_) || idx >= static_cast<int64_t>(handles_->size()) || OB_ISNULL(handles_->at(idx))) {
      ret = OB_ARRAY_OUT_OF_RANGE;
      LOG_WARN("handles_ is null or idx is out of range", K(idx), K(handles_->size()));
    } else {
      ObFTTrie<void> *trie = (*all_tries_)[idx];
      ObArenaAllocator dat_alloc(lib::ObMemAttr("DATBuild"));
      ObFTDATBuilder<void> builder(dat_alloc);

      ObFTDAT *dat_buff = nullptr;
      ObFTCacheRangeHandle *info = handles_->at(idx);

      if (OB_FAIL(builder.init(*trie))) {
        LOG_WARN("Failed to init builder.", K(ret), K(idx));
      } else if (OB_FAIL(builder.build_from_trie(*trie))) {
        LOG_WARN("Failed to build datrie.", K(ret), K(idx));
      } else if (FALSE_IT(builder.get_mem_block(dat_buff))) {
      } else if (OB_ISNULL(dat_buff)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("dat_buff is null", K(ret), K(idx));
      } else if (OB_FAIL(ObFTCacheDict::make_and_fetch_cache_entry(*desc_,
                                                                    dat_buff,
                                                                    dat_buff->mem_block_size_,
                                                                    static_cast<int32_t>(idx),
                                                                    info->value_,
                                                                    info->handle_))) {
        LOG_WARN("Failed to put dict into kv cache", K(ret), K(idx));
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
    ObFTSingleToken end_char;
    bool range_end = false;

    while (OB_SUCC(ret) && !range_end) {
      ObString key;
      if (OB_FAIL(iter.get_key(key))) {
        LOG_WARN("Failed to get key", K(ret));
      } else {
        ++count;
        if (count >= DEFAULT_KEY_PER_RANGE
            && OB_FAIL(ObCharset::first_valid_char(desc.coll_type_,
                                                   key.ptr(), key.length(), first_char_len))) {
          LOG_WARN("First char is not valid.");
        } else if (DEFAULT_KEY_PER_RANGE == count
                   && OB_FAIL(end_char.set_token(key.ptr(), first_char_len))) {
          LOG_WARN("Failed to record first char.", K(ret));
        } else if (count > DEFAULT_KEY_PER_RANGE
                   && (end_char.get_token() != ObString(first_char_len, key.ptr()))) {
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

  if (OB_SUCC(ret) && all_tries.size() > 0) {
    ObArray<ObFTCacheRangeHandle *> handles;
    handles.set_attr(lib::ObMemAttr("DATBuild"));
    for (int64_t i = 0; OB_SUCC(ret) && i < all_tries.size(); i++) {
      ObFTCacheRangeHandle *handle = nullptr;
      if (OB_FAIL(range_container.fetch_info_for_dict(handle))) {
        LOG_WARN("Failed to fetch info for dict.", K(ret), K(i));
      } else if (OB_FAIL(handles.push_back(handle))) {
        LOG_WARN("Failed to push back handle", K(ret), K(i));
      }
    }
    if (OB_FAIL(ret)) {
      handles.reset();
    } else {
      DATBuilderThreadPool pool;
      pool.set_run_wrapper(MTL_CTX());
      pool.set_tries(&all_tries);
      pool.set_desc(&desc);
      pool.set_thread_count(static_cast<int64_t>(all_tries.size()));
      pool.set_handles(&handles);

      if (OB_FAIL(pool.start())) {
        LOG_WARN("Failed to start thread pool", K(ret));
      } else {
        pool.wait();
        ret = pool.get_error_code();
      }
    }
  }

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
  ObFTSingleToken end_char;

  ObFTDAT *dat_buff = nullptr;

  while (OB_SUCC(ret) && !range_end) {
    ObString key;
    if (OB_FAIL(iter.get_key(key))) {
      LOG_WARN("Failed to get key", K(ret));
    } else if (OB_FALSE_IT(++count)) {
    } else if (count >= DEFAULT_KEY_PER_RANGE
               && OB_FAIL(ObCharset::first_valid_char(desc.coll_type_,
                                                      key.ptr(), key.length(), first_char_len))) {
      LOG_WARN("First char is not valid.");
    } else if (DEFAULT_KEY_PER_RANGE == count
               && OB_FAIL(end_char.set_token(key.ptr(), first_char_len))) {
      LOG_WARN("Failed to record first char.", K(ret));
    } else if (count > DEFAULT_KEY_PER_RANGE
               && (end_char.get_token() != ObString(first_char_len, key.ptr()))) {
      range_end = true;
    } else if (OB_FAIL(trie.insert(key, {}))) {
      LOG_WARN("Failed to insert key to trie", K(ret));
    } else if (OB_FAIL(iter.next()) && OB_ITER_END != ret) {
      LOG_WARN("Failed to step to next word entry.", K(ret));
    }
  }

  if (OB_ITER_END == ret) {
    build_next_range = false;
    ret = OB_SUCCESS;
  }
  ObFTCacheRangeHandle *info = nullptr;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(builder.init(trie))) {
    LOG_WARN("Failed to build dat.", K(ret));
  } else if (OB_FAIL(builder.build_from_trie(trie))) {
    LOG_WARN("Failed to build datrie.", K(ret));
  } else if (FALSE_IT(builder.get_mem_block(dat_buff))) {
  } else if (OB_ISNULL(dat_buff)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dat_buff is null", K(ret));
  } else if (OB_FAIL(container.fetch_info_for_dict(info))) {
    LOG_WARN("Failed to fetch info for dict.", K(ret));
  } else if (OB_FAIL(ObFTCacheDict::make_and_fetch_cache_entry(desc, dat_buff, dat_buff->mem_block_size_,
                                                               range_id, info->value_, info->handle_))) {
    LOG_WARN("Failed to put dict into kv cache");
  }
  tmp_alloc.reset();
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
    if (OB_FAIL(ObFTRangeDict::build_one_range(desc, range_id++, iter, range_container, build_next_range))) {
      LOG_WARN("fail to build range", K(ret));
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
    ObDictCacheKey key(name, desc.type_, i);
    ObFTCacheRangeHandle *info = nullptr;
    if (OB_FAIL(range_container.fetch_info_for_dict(info))) {
      LOG_WARN("Failed to fetch info for dict.", K(ret));
    } else if (OB_FAIL(ObDictCache::get_instance().get_dict(key, info->value_, info->handle_))
               && OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("Failed to get dict from kv cache.", K(ret));
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      range_container.reset();
    } else if (FALSE_IT(info->type_ = desc.type_)) {
    }
  }
  if (OB_FAIL(ret)) {
    range_container.reset();
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
