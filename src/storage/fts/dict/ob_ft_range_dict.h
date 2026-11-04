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

#ifndef _OCEANBASE_STORAGE_FTS_DICT_OB_FT_RANGE_DICT_H_
#define _OCEANBASE_STORAGE_FTS_DICT_OB_FT_RANGE_DICT_H_

#include "lib/alloc/alloc_struct.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_vector.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/fts/dict/ob_ft_cache_container.h"
#include "storage/fts/dict/ob_ft_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/dict/ob_ft_dict_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObFTRangeDict final : public ObIFTDict
{
public:
  ObFTRangeDict(ObIAllocator &alloc,
                ObFTCacheRangeContainer *range_container,
                const ObFTDictDesc &desc)
      : is_inited_(false), desc_(desc), range_alloc_(lib::ObMemAttr("Range Dict")),
        range_dicts_(&range_alloc_), range_container_(range_container),
        range_memo_()
  {
  }
  ~ObFTRangeDict() override{};

public:
  struct ObFTRange final
  {
    ObFTSingleWord start_;
    ObFTSingleWord end_;
    ObIFTDict *dict_; // a cache dict
  };

public:
  int init() override;
  int match(const ObString &single_word, ObDATrieHit &hit) const override;
  int match(const ObString &words, bool &is_match) const override;
  int match_with_hit(const ObString &single_word,
                     const ObDATrieHit &last_hit,
                     ObDATrieHit &hit) const override;

public:
  int build_dict_from_cache(const ObFTCacheRangeContainer &range_container);

  static int try_load_cache(const ObFTDictDesc &desc,
                            const uint32_t range_count,
                            ObFTCacheRangeContainer &range_container);
  static int build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &range_container);

  static int build_cache_from_ik_dict(const ObFTDictDesc &desc, ObFTCacheRangeContainer &range_container);

private:
  // build cache
  static int build_ranges(const ObFTDictDesc &desc,
                          ObIFTDictIterator &iter,
                          ObFTCacheRangeContainer &range_container);

  // build one range's cache
  static int build_one_range(const ObFTDictDesc &desc,
                             const int32_t range_id,
                             ObIFTDictIterator &iter,
                             ObFTCacheRangeContainer &container,
                             bool &build_next_range);

  static int build_ranges_concurrently_thread_pool(const ObFTDictDesc &desc,
                                                   ObIFTDictIterator &iter,
                                                   ObFTCacheRangeContainer &range_container);

private:
  void destroy()
  {
    for (int64_t i = 0; i < range_dicts_.size(); i++) {
      range_dicts_[i].dict_->~ObIFTDict(); // destroy dict
    }
    range_dicts_.reset();
    range_alloc_.reset();
  }

  int find_first_char_range(const ObString &single_word, ObIFTDict *&dict) const;

private:
  // Direct-mapped memo of find_first_char_range(): range_dicts_ is fixed
  // once init() has built it, so the (character -> range index) answer is
  // a pure function of the character bytes. The linear scan costs two
  // collation strcmp calls per range per dispatched character. Entries
  // are filled only after init() could have run (lookup starts as scan),
  // and this object belongs to one parser on one thread, so the
  // fixed-size mutable array needs no locking and is bounded.
  static constexpr int64_t RANGE_MEMO_SLOTS = 256; // power of two
  static constexpr int64_t RANGE_MEMO_MAX_BYTES = 4; // one utf8mb4 char
  struct RangeMemoEntry
  {
    char bytes[RANGE_MEMO_MAX_BYTES];
    uint8_t len; // key length in bytes
    uint8_t valid; // slot filled
    int32_t range_idx; // index into range_dicts_; -1 = no range
  };
  static int64_t range_memo_slot(const char *ptr, const int64_t len)
  {
    uint64_t h = 1469598103934665603ULL ^ static_cast<uint64_t>(len);
    for (int64_t i = 0; i < len; ++i) {
      h = (h ^ static_cast<unsigned char>(ptr[i])) * 1099511628211ULL;
    }
    return static_cast<int64_t>(h & (RANGE_MEMO_SLOTS - 1));
  }
  static constexpr int DEFAULT_KEY_PER_RANGE = 50000; // by estimated
  bool is_inited_;
  ObFTDictDesc desc_;
  ObArenaAllocator range_alloc_;
  ObVector<ObFTRange, ObArenaAllocator> range_dicts_;
  ObFTCacheRangeContainer *range_container_; // only used to read cache
  mutable RangeMemoEntry range_memo_[RANGE_MEMO_SLOTS];

private:
  DISALLOW_COPY_AND_ASSIGN(ObFTRangeDict);
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_RANGE_DICT_H_
