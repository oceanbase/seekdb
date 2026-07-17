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
#include "lib/atomic/ob_atomic.h"
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
      : generation_(0), range_count_(0)
  {
  }

public:
  uint64_t generation_;
  int32_t range_count_;
};

struct ObFTDictInfoKey
{
public:
  ObFTDictInfoKey()
      : tenant_id_(OB_INVALID_TENANT_ID), table_id_(OB_INVALID_ID)
  {
  }
  ObFTDictInfoKey(const uint64_t tenant_id, const uint64_t table_id)
      : tenant_id_(tenant_id), table_id_(table_id)
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
    hash = common::murmurhash(&tenant_id_, sizeof(tenant_id_), hash);
    hash = common::murmurhash(&table_id_, sizeof(table_id_), hash);
    return hash;
  }

  bool operator==(const ObFTDictInfoKey &other) const
  {
    return tenant_id_ == other.tenant_id_ && table_id_ == other.table_id_;
  }

  int compare(const ObFTDictInfoKey &other) const
  {
    int ret = 0;
    if (tenant_id_ < other.tenant_id_) {
      ret = -1;
    } else if (tenant_id_ > other.tenant_id_) {
      ret = 1;
    } else if (table_id_ < other.table_id_) {
      ret = -1;
    } else if (table_id_ > other.table_id_) {
      ret = 1;
    }
    return ret;
  }

private:
  uint64_t tenant_id_;
  uint64_t table_id_;
};

class ObFTCacheRangeContainer;
class ObFTDictHub
{
public:
  ObFTDictHub() : is_inited_(false), generation_epoch_(0), dict_map_(), rw_dict_lock_() {}
  ~ObFTDictHub() {}

  int init();

  int destroy();

  int build_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  int load_cache(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  int refresh(const ObFTDictDesc &desc, ObFTCacheRangeContainer &container);

  uint64_t get_generation_epoch() const { return ATOMIC_LOAD(&generation_epoch_); }

private:
  int build_cache_internal(const ObFTDictDesc &desc,
                           ObFTCacheRangeContainer &container,
                           const bool force_refresh);
  int get_dict_info(const ObFTDictInfoKey &key, ObFTDictInfo &info);

  int put_dict_info(const ObFTDictInfoKey &key, const ObFTDictInfo &info);


private:
  bool is_inited_;
  uint64_t generation_epoch_;
  // holds info of dict
  hash::ObHashMap<ObFTDictInfoKey, ObFTDictInfo> dict_map_;
  ObBucketLock rw_dict_lock_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_HUB_H_
