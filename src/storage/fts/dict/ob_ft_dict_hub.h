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

#include <atomic>

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
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
        cache_id_(0),
        version_(0),
        range_count_(0)
  {
  }

public:
  char name_[2048]; // for now
  ObFTDictType type_;
  ObCharsetType charset_;
  uint64_t cache_id_;
  int64_t version_; // in memory
  int32_t range_count_;
};

struct ObFTDictInfoKey
{
public:
  static const int64_t MAX_DICT_NAME_LENGTH = 2048;
  ObFTDictInfoKey()
      : type_(static_cast<uint64_t>(ObFTDictType::DICT_TYPE_INVALID)), name_len_(0), name_()
  {
  } // default constructor
  int set(const ObFTDictType type, const ObString &name);
  int hash(uint64_t &hash_value) const
  {
    int ret = OB_SUCCESS;
    hash_value = hash();
    return ret;
  }

  uint64_t hash() const
  {
    uint64_t hash = 0;
    hash = common::murmurhash(&type_, sizeof(type_), hash);
    hash = common::murmurhash(name_, name_len_, hash);
    return hash;
  }

  bool operator==(const ObFTDictInfoKey &other) const
  {
    return type_ == other.type_
           && name_len_ == other.name_len_
           && 0 == MEMCMP(name_, other.name_, name_len_);
  }

  int compare(const ObFTDictInfoKey &other) const
  {
    int ret = 0;
    if (type_ < other.type_) {
      ret = -1;
    } else if (type_ > other.type_) {
      ret = 1;
    } else {
      const int32_t cmp_len = MIN(name_len_, other.name_len_);
      ret = MEMCMP(name_, other.name_, cmp_len);
      if (0 == ret) {
        ret = name_len_ < other.name_len_ ? -1 : (name_len_ > other.name_len_ ? 1 : 0);
      }
    }
    return ret;
  }

private:
  uint64_t type_;
  int32_t name_len_;
  char name_[MAX_DICT_NAME_LENGTH];
};

class ObFTCacheRangeContainer;
class ObFTDictHub
{
public:
  ObFTDictHub()
      : is_inited_(false), dict_map_(), rw_dict_lock_(), next_cache_id_(1024), next_version_(1)
  {}
  ~ObFTDictHub() {}

  int init();

  int destroy();

  int build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  int load_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  int refresh_user_dict(const ObFTDictDesc &desc,
                        common::ObISQLClient &sql_client,
                        const ObString &database_name,
                        const ObString &table_name);

private:
  int get_dict_info(const ObFTDictInfoKey &key, ObFTDictInfo &info);

  int put_dict_info(const ObFTDictInfoKey &key, const ObFTDictInfo &info);


private:
  bool is_inited_;
  // holds info of dict
  hash::ObHashMap<ObFTDictInfoKey, ObFTDictInfo> dict_map_;
  ObBucketLock rw_dict_lock_;
  std::atomic<uint64_t> next_cache_id_;
  std::atomic<int64_t> next_version_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_HUB_H_
