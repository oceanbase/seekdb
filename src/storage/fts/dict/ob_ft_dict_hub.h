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
#include "lib/lock/ob_bucket_lock.h"
#include "storage/fts/dict/ob_ft_dict_def.h"

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
class ObIFTDict;

class ObFTDictHub
{
public:
  ObFTDictHub() : is_inited_(false), dict_map_(), rw_dict_lock_(), shared_dict_lock_()
  {
    for (int64_t i = 0; i < MAX_DICT_TYPE_NUM; ++i) {
      shared_dicts_[i] = nullptr;
    }
  }
  ~ObFTDictHub() {}

  int init();

  int destroy();

  int build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  int load_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  /**
   * @brief Get the dictionary shared by all the parsers of the given descriptor.
   *
   * Building a dictionary is expensive: the KV cache blocks holding the
   * double array tries have to be pinned and one range dictionary object is
   * created for every range of the dictionary. The content of a dictionary
   * never changes during the lifetime of the process, so the dictionary is
   * built once and then shared by every parser instead of being rebuilt for
   * every tokenize call and for every row of the fulltext index build.
   *
   * The returned dictionary is read-only and stays valid until destroy() is
   * called on the hub.
   */
  int get_shared_dict(const ObFTDictDesc &desc, ObIFTDict *&dict);

private:
  int get_dict_info(const ObFTDictInfoKey &key, ObFTDictInfo &info);

  int put_dict_info(const ObFTDictInfoKey &key, const ObFTDictInfo &info);

  // Holds one built dictionary together with the KV cache handles that keep
  // the dictionary blocks alive. The definition lives in the cpp file.
  class ObFTSharedDict;

private:
  static constexpr int64_t MAX_DICT_TYPE_NUM = 4;

  bool is_inited_;
  // holds info of dict
  hash::ObHashMap<ObFTDictInfoKey, ObFTDictInfo> dict_map_;
  ObBucketLock rw_dict_lock_;
  // Guards the shared dictionary table below. It is a separate lock from
  // rw_dict_lock_ because building a dictionary takes rw_dict_lock_ itself.
  ObBucketLock shared_dict_lock_;
  ObFTSharedDict *shared_dicts_[MAX_DICT_TYPE_NUM];
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_HUB_H_
