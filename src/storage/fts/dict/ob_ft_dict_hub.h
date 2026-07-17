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
      : name_hash_(0), type_(static_cast<uint64_t>(ObFTDictType::DICT_TYPE_INVALID)), tenant_id_(0)
  {
  } // default constructor
  explicit ObFTDictInfoKey(const ObFTDictDesc &desc)
      : name_hash_(desc.name_.hash()), type_(static_cast<uint64_t>(desc.type_)),
        tenant_id_(desc.tenant_id_)
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
    hash = common::murmurhash(&name_hash_, sizeof(name_hash_), hash);
    hash = common::murmurhash(&type_, sizeof(int64_t), hash);
    hash = common::murmurhash(&tenant_id_, sizeof(tenant_id_), hash);
    return hash;
  }

  bool operator==(const ObFTDictInfoKey &other) const
  {
    return name_hash_ == other.name_hash_ && type_ == other.type_ && tenant_id_ == other.tenant_id_;
  }

  int compare(const ObFTDictInfoKey &other) const
  {
    int ret = 0;
    if (name_hash_ < other.name_hash_) {
      ret = -1;
    } else if (name_hash_ > other.name_hash_) {
      ret = 1;
    } else if (tenant_id_ < other.tenant_id_) {
      ret = -1;
    } else if (tenant_id_ > other.tenant_id_) {
      ret = 1;
    } else if (type_ < other.type_) {
      ret = -1;
    } else if (type_ > other.type_) {
      ret = 1;
    } else {
      ret = 0;
    }
    return ret;
  }

private:
  uint64_t name_hash_;
  uint64_t type_;
  uint64_t tenant_id_;
  // name
};

class ObFTCacheRangeContainer;
class ObFTDictHub
{
public:
  ObFTDictHub() : is_inited_(false), dict_map_(), rw_dict_lock_() {}
  ~ObFTDictHub() {}

  int init();

  int destroy();

  int build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  int load_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  int refresh_cache(const common::ObString &dict_table_name);

private:
  int get_dict_info(const ObFTDictInfoKey &key, ObFTDictInfo &info);

  int put_dict_info(const ObFTDictInfoKey &key, const ObFTDictInfo &info);


private:
  bool is_inited_;
  // holds info of dict
  hash::ObHashMap<ObFTDictInfoKey, ObFTDictInfo> dict_map_;
  ObBucketLock rw_dict_lock_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_HUB_H_
