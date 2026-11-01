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

#ifndef _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_HUB_H_
#define _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_HUB_H_

#include "lib/charset/ob_charset.h"
#include "lib/allocator/page_arena.h"
#include "lib/lock/ob_mutex.h"
#include "lib/lock/ob_bucket_lock.h"
#include "storage/fts/dict/ob_ft_dict_def.h"

#include <atomic>

namespace oceanbase
{
namespace storage
{
// typedef uint64_t ObFTTableID;
// typedef ObFTTableID ObFTDictTableID;

class ObFTDictInfo
{
public:
  ObFTDictInfo()
      : name_(""),
        type_(ObFTDictType::DICT_TYPE_INVALID),
        charset_(CHARSET_INVALID),
        version_(0),
        range_count_(0)
  {
  }

public:
  char name_[2048]; // for now
  ObFTDictType type_;
  ObCharsetType charset_;
  int64_t version_; // in memory
  int32_t range_count_;
};

struct ObFTDictInfoKey
{
public:
  ObFTDictInfoKey()
      : type_(static_cast<uint64_t>(ObFTDictType::DICT_TYPE_INVALID))
  {
  } // default constructor
  ObFTDictInfoKey(const uint64_t type)
      : type_(type)
  {
  }
  int hash(uint64_t &hash_value) const
  {
    int ret = OB_SUCCESS;
    hash_value = hash();
    return ret;
  }

  uint64_t hash() const
  {
    uint64_t hash = 0;
    hash = common::murmurhash(&type_, sizeof(int64_t), hash);
    return hash;
  }

  bool operator==(const ObFTDictInfoKey &other) const
  {
    return type_ == other.type_;
  }

  int compare(const ObFTDictInfoKey &other) const
  {
    int ret = 0;
    if (0 == ret) {
      ret = type_ - other.type_;
    }
    return ret;
  }

private:
  uint64_t type_;
  // name
};

class ObFTCacheRangeContainer;
class ObFTRangeDict;
class ObIFTDict;

class ObFTDictHub
{
public:
  ObFTDictHub() : is_inited_(false), dict_map_(), rw_dict_lock_() {}
  ~ObFTDictHub() {}

  int init();

  int destroy();

  int build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  int load_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  // Get the dictionary of the given type. The dictionary is built on the
  // first call and then shared by all the callers. Building the dictionary
  // is expensive, so it must not be done once per parser instance.
  int get_dict(const ObFTDictDesc &desc, ObIFTDict *&dict);

private:
  int get_dict_info(const ObFTDictInfoKey &key, ObFTDictInfo &info);

  int put_dict_info(const ObFTDictInfoKey &key, const ObFTDictInfo &info);

  int build_shared_dict(const ObFTDictDesc &desc, int64_t slot_idx);

private:
  // A dictionary that is built once and shared by every parser instance.
  // The container keeps the kv cache entries pinned, so that the dictionary
  // stays valid for the whole life time of the hub.
  struct ObFTSharedDict
  {
    std::atomic<ObIFTDict *> dict_;
    ObFTCacheRangeContainer *container_;

    ObFTSharedDict() : dict_(nullptr), container_(nullptr) {}
  };

  static constexpr int64_t MAX_SHARED_DICT_CNT = 4;

private:
  bool is_inited_;
  // holds info of dict
  hash::ObHashMap<ObFTDictInfoKey, ObFTDictInfo> dict_map_;
  // holds the dictionaries shared by all the parser instances, indexed by the
  // dictionary type
  ObFTSharedDict shared_dicts_[MAX_SHARED_DICT_CNT];
  // serializes the dictionary construction
  lib::ObMutex dict_build_lock_;
  // holds the shared dictionaries and their cache containers
  ObArenaAllocator shared_alloc_;
  ObBucketLock rw_dict_lock_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_HUB_H_
