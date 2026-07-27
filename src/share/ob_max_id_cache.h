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

#ifndef OCEANBASE_SHARE_OB_MAX_ID_CACHE_H_
#define OCEANBASE_SHARE_OB_MAX_ID_CACHE_H_

#include "lib/hash/ob_hashmap.h"
#include "share/ob_max_id_fetcher.h"

namespace oceanbase
{
namespace share
{

class ObMaxIdCacheItem
{
public:
  ObMaxIdCacheItem(const ObMaxIdType &type);
  int fetch_max_id(const ObMaxIdType max_id_type, uint64_t &id,
      const uint64_t size, ObMySQLProxy *sql_proxy);
private:
  int fetch_ids_from_inner_table_(const uint64_t size, ObMySQLProxy *sql_proxy);
  int fetch_ids_by_cache_(const uint64_t size, uint64_t &id);
  bool cached_id_valid_();
private:
  static const uint64_t CACHE_SIZE = 1024;
private:
  // [min_id, min_id + size) is valid
  uint64_t min_id_;
  uint64_t size_;
  ObMaxIdType type_;
  common::ObLatch latch_;
};

class ObMaxIdCache
{
public:
  explicit ObMaxIdCache();
  int fetch_max_id(const ObMaxIdType max_id_type, uint64_t &id,
      const uint64_t size, ObMySQLProxy *sql_proxy);
private:
  ObMaxIdCacheItem object_id_cache_;
  ObMaxIdCacheItem normal_rowid_table_tablet_id_cache_;
};

class ObMaxIdCacheMgr
{
public:
  int init(ObMySQLProxy *sql_proxy);
  void reset();
  // return [id, id + size - 1)
  int fetch_max_id(const ObMaxIdType max_id_type, uint64_t &id,
      const uint64_t size, bool init_runtime_if_not_exist = true);
  ObMaxIdCacheMgr();
  ~ObMaxIdCacheMgr();
private:
  int add_runtime_cache_();
  int remove_cache_(ObMaxIdCache *cache);
private:
  ObMaxIdCache *runtime_cache_ = nullptr;
  ObMemAttr attr_;
  ObArenaAllocator allocator_;
  bool inited_;
  common::ObLatch latch_;
  ObMySQLProxy *sql_proxy_;
};

}
}


#endif // OCEANBASE_SHARE_OB_MAX_ID_CACHE_H_
