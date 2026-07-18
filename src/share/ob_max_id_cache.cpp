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

#define USING_LOG_PREFIX RS

#include "lib/alloc/alloc_struct.h"
#include "lib/utility/ob_mod_define.h"
#include "ob_max_id_cache.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace share
{

ObMaxIdCacheItem::ObMaxIdCacheItem(const ObMaxIdType &type) : 
  min_id_(OB_INVALID_ID), size_(OB_INVALID_SIZE), type_(type), latch_()
{
}

bool ObMaxIdCacheItem::cached_id_valid_()
{
  return min_id_ != OB_INVALID_ID && size_ != OB_INVALID_SIZE;
}

int ObMaxIdCacheItem::fetch_max_id(const ObMaxIdType max_id_type,
    uint64_t &id, const uint64_t size, ObMySQLProxy *sql_proxy)
{
  int ret = OB_SUCCESS;
  if (false || max_id_type != type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument not match", KR(ret), K(max_id_type), K(type_));
  } else if (!cached_id_valid_() || size_ < size) {
    const uint64_t fetch_size = common::max(CACHE_SIZE, size);
    if (OB_FAIL(fetch_ids_from_inner_table_(fetch_size, sql_proxy))) {
      LOG_WARN("failed to fetch ids from inner table", KR(ret), K(fetch_size),
          K(max_id_type), K(size));
    }
  }
  if (FAILEDx(fetch_ids_by_cache_(size, id))) {
    LOG_WARN("failed to fetch ids from cache", KR(ret), K(size), K(max_id_type));
  }
  return ret;
}

int ObMaxIdCacheItem::fetch_ids_from_inner_table_(const uint64_t size, ObMySQLProxy *sql_proxy)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(sql_proxy));
  } else {
    uint64_t id = OB_INVALID_ID;
    ObMaxIdFetcher id_fetcher(*sql_proxy);
    uint64_t old_min_id = ATOMIC_LOAD(&min_id_);
    if (OB_FAIL(id_fetcher.batch_fetch_new_max_id_from_inner_table( type_, id, size))) {
      LOG_WARN("failed to batch fetch new max id from inner table", KR(ret), K(type_), K(size));
    } else if (OB_INVALID_ID == id) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("id is invalid", KR(ret), K(id), K(type_), K(size));
    } else {
      ObLatchWGuard guard(latch_, ObLatchIds::MAX_ID_CACHE_LOCK);
      uint64_t new_min_id = id - size + 1;
      if (cached_id_valid_() && min_id_ + size_ == new_min_id) {
        size_ += size;
      } else if (old_min_id != min_id_) {
        ret = OB_EAGAIN;
        LOG_WARN("min_id_ changed, need try", KR(ret), K(old_min_id), K_(min_id));
      } else if ((OB_INVALID_ID != min_id_) && (min_id_ > new_min_id)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("min_id_ revert is not expected", KR(ret), K_(min_id), K(new_min_id));
      } else {
        LOG_INFO("max id cached renewed", KR(ret), K(type_),
            K(min_id_), K(size_), K(new_min_id), K(size));
        min_id_ = new_min_id;
        size_ = size;
      }
    }
  }
  return ret;
}

int ObMaxIdCacheItem::fetch_ids_by_cache_(const uint64_t size, uint64_t &id)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID == min_id_ || OB_INVALID_SIZE == size_ || size_ < size) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("size out of range", KR(ret), K(min_id_), K(size_), K(size));
  } else {
    ObLatchWGuard guard(latch_, ObLatchIds::MAX_ID_CACHE_LOCK);
    id = min_id_;
    min_id_ += size;
    size_ -= size;
  }
  return ret;
}

ObMaxIdCache::ObMaxIdCache() : object_id_cache_(OB_MAX_USED_OBJECT_ID_TYPE),
  normal_rowid_table_tablet_id_cache_(OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE),
  extended_rowid_table_tablet_id_cache_(OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE)
{
}

int ObMaxIdCache::fetch_max_id(const ObMaxIdType max_id_type,
    uint64_t &id, const uint64_t size, ObMySQLProxy *sql_proxy)
{
  int ret = OB_SUCCESS;
  ObMaxIdCacheItem *item = nullptr;
  if (OB_MAX_USED_OBJECT_ID_TYPE == max_id_type) {
    item = &object_id_cache_;
  } else if (OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE == max_id_type) {
    item = &normal_rowid_table_tablet_id_cache_;
  } else if (OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE == max_id_type) {
    item = &extended_rowid_table_tablet_id_cache_;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cache for max id type is not supported", KR(ret), K(max_id_type));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(item)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(item));
  } else if (OB_FAIL(item->fetch_max_id(max_id_type, id, size, sql_proxy))) {
    LOG_WARN("failed to fetch max id in item", KR(ret), K(max_id_type), K(size));
  }
  return ret;
}

ObMaxIdCacheMgr::ObMaxIdCacheMgr() : attr_(lib::ObLabel("MaxIdCache")), allocator_(attr_),
  inited_(false), sql_proxy_(nullptr)
{
}

ObMaxIdCacheMgr::~ObMaxIdCacheMgr()
{
  reset();
}

int ObMaxIdCacheMgr::init(ObMySQLProxy *sql_proxy)
{
  int ret = OB_SUCCESS;
  ObLatchWGuard guard(latch_, ObLatchIds::MAX_ID_CACHE_LOCK);
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(sql_proxy));
  } else {
    sql_proxy_ = sql_proxy;
    inited_ = true;
  }
  return ret;
}

void ObMaxIdCacheMgr::reset()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObLatchWGuard guard(latch_, ObLatchIds::MAX_ID_CACHE_LOCK);
  if (OB_NOT_NULL(tenant_cache_)) {
    if (OB_TMP_FAIL(remove_cache_(tenant_cache_))) {
      LOG_WARN("failed to remove cache", KR(tmp_ret), KP(tenant_cache_));
    }
    tenant_cache_ = nullptr;
  }
  allocator_.reset();
}


int ObMaxIdCacheMgr::remove_cache_(ObMaxIdCache *cache)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cache)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pointer is null", KR(ret), KP(cache));
  } else {
    cache->~ObMaxIdCache();
    allocator_.free(cache);
  }
  return ret;
}

// moved definition to the upper-layer owner cpp(real upper-layer symbol user, declaration remains in the header, transitional state) -> src/rootserver/ob_root_utils.cpp
// Note: master tenant-elim changed the original body(removed the tenant_id parameter, tenant_caches_->tenant_cache_), HOST(ob_root_utils.cpp) must be synced (see routing item)

int ObMaxIdCacheMgr::add_tenant_()
{
  int ret = OB_SUCCESS;
  ObMaxIdCache *cache = OB_NEWx(ObMaxIdCache, &allocator_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("max id cache mgr is not inited", KR(ret), K(inited_));
  } else if (OB_NOT_NULL(tenant_cache_)) {
    ret = OB_HASH_EXIST;
    LOG_WARN("tenant cache already exist", KR(ret));
  } else {
    tenant_cache_ = cache;
  }
  if (OB_FAIL(ret)) {
    cache->~ObMaxIdCache();
    allocator_.free(cache);
  }
  return ret;
}
}
}
