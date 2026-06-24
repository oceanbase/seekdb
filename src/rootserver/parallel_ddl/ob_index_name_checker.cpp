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

#define USING_LOG_PREFIX SHARE_SCHEMA

#include "rootserver/parallel_ddl/ob_index_name_checker.h"
#include "share/schema/ob_schema_service_sql_impl.h"
using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;

ObIndexNameCache::ObIndexNameCache(
  common::ObMySQLProxy &sql_proxy)
  : mutex_(common::ObLatchIds::IND_NAME_CACHE_LOCK),
    sql_proxy_(sql_proxy),
    allocator_(ObMemAttr("IndNameInfo", ObCtxIds::SCHEMA_SERVICE)),
    cache_(ModulePageAllocator(allocator_)),
    loaded_(false)
{
}

void ObIndexNameCache::reset_cache()
{
  lib::ObMutexGuard guard(mutex_);
  (void) inner_reset_cache_();
}

void ObIndexNameCache::inner_reset_cache_()
{
  cache_.destroy();
  allocator_.reset();
  loaded_ = false;
  FLOG_INFO("[INDEX NAME CACHE] reset index name map");
}

int ObIndexNameCache::check_index_name_exist(
    const uint64_t database_id,
    const ObString &index_name,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  if (OB_UNLIKELY(
      false
      || false
      || OB_INVALID_ID == database_id
      || index_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(database_id), K(index_name));
  } else {
    lib::ObMutexGuard guard(mutex_);
    ObString idx_name;
    uint64_t data_table_id = OB_INVALID_ID;
    if (OB_FAIL(try_load_cache_())) {
      LOG_WARN("fail to load index name cache", KR(ret));
    } else if (is_recyclebin_database_id(database_id)) {
      idx_name = index_name;
      data_table_id = OB_INVALID_ID;
    } else {
      uint64_t data_table_id = ObSimpleTableSchemaV2::extract_data_table_id_from_index_name(index_name);
      if (OB_INVALID_ID == data_table_id) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid index name", KR(ret), K(index_name));
      } else if (OB_FAIL(ObSimpleTableSchemaV2::get_index_name(index_name, idx_name))) {
        LOG_WARN("fail to get original index name", KR(ret), K(index_name));
      } else {
        // data_table_id stays as is in MySQL mode
      }
    }
    if (OB_SUCC(ret)) {
      ObIndexSchemaHashWrapper index_name_wrapper(
                               database_id,
                               data_table_id,
                               idx_name);
      ObIndexNameInfo *index_name_info = NULL;
      if (OB_FAIL(cache_.get_refactored(index_name_wrapper, index_name_info))) {
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("fail to get index name info", KR(ret), K(index_name_wrapper));
        }
      } else if (OB_ISNULL(index_name_info)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("index name info is null", KR(ret), K(index_name_wrapper));
      } else {
        is_exist = true;
        LOG_INFO("index name exist", KR(ret), KPC(index_name_info),
                 K(database_id), K(index_name), K(data_table_id), K(idx_name));
        // Before call check_index_name_exist(), index_name will be locked by trans first.
        // And add_index_name() will be called before trans commit.
        //
        // It may has garbage when trans commit failed after add_index_name() is called.
        // So, we need to double check if index name actually exists in inner table when confict occurs.
        ObSchemaService *schema_service_impl = NULL;
        uint64_t index_id = OB_INVALID_ID;
        if (OB_ISNULL(schema_service_impl = GSCHEMASERVICE.get_schema_service())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("schema service impl is null", KR(ret));
        } else if (OB_FAIL(schema_service_impl->get_index_id(
                   sql_proxy_, database_id,
                   index_name_info->get_index_name(), index_id))) {
          LOG_WARN("fail to get index id", KR(ret), KPC(index_name_info));
        } else if (OB_INVALID_ID != index_id) {
          is_exist = true;
        } else {
          is_exist = false;
          FLOG_INFO("garbage index name exist, should be erased", KPC(index_name_info),
                    K(database_id), K(index_name), K(data_table_id), K(idx_name));
          if (OB_FAIL(cache_.erase_refactored(index_name_wrapper))) {
            LOG_WARN("fail to erase key", KR(ret), K(index_name_wrapper));
            if (OB_HASH_NOT_EXIST != ret) {
              (void) inner_reset_cache_();
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObIndexNameCache::add_index_name(
    const share::schema::ObTableSchema &index_schema)
{
  int ret = OB_SUCCESS;
  
  const uint64_t database_id = index_schema.get_database_id();
  const ObString &index_name = index_schema.get_table_name_str();
  const ObTableType table_type = index_schema.get_table_type();
  uint64_t data_table_id = index_schema.get_data_table_id();
  if (OB_UNLIKELY(
      false
      || false
      || OB_INVALID_ID == database_id
      || index_name.empty()
      || !is_index_table(table_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret),
             K(database_id), K(index_name), K(table_type));
  } else if (OB_UNLIKELY(!is_recyclebin_database_id(database_id)
             && index_schema.get_origin_index_name_str().empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid index schema", KR(ret), K(index_schema));
  } else {
    lib::ObMutexGuard guard(mutex_);
    if (OB_FAIL(try_load_cache_())) {
      LOG_WARN("fail to load index name cache", KR(ret));
    } else {
      void *buf = NULL;
      ObIndexNameInfo *index_name_info = NULL;
      ObString idx_name;
      if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObIndexNameInfo)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc index name info", KR(ret));
      } else if (FALSE_IT(index_name_info = new (buf) ObIndexNameInfo())) {
      } else if (OB_FAIL(index_name_info->init(allocator_, index_schema))) {
        LOG_WARN("fail to init index name info", KR(ret), K(index_schema));
      } else if (is_recyclebin_database_id(database_id)) {
        data_table_id = OB_INVALID_ID;
        idx_name = index_name_info->get_index_name();
      } else {
        data_table_id = index_name_info->get_data_table_id();
        idx_name = index_name_info->get_original_index_name();
      }
      if (OB_SUCC(ret)) {
        int overwrite = 0;
        ObIndexSchemaHashWrapper index_name_wrapper(database_id,
                                                    data_table_id,
                                                    idx_name);
        if (OB_FAIL(cache_.set_refactored(index_name_wrapper, index_name_info, overwrite))) {
          LOG_WARN("fail to set refactored", KR(ret), KPC(index_name_info));
          if (OB_HASH_EXIST == ret) {
            ObIndexNameInfo **exist_index_info = cache_.get(index_name_wrapper);
            if (OB_NOT_NULL(exist_index_info) && OB_NOT_NULL(*exist_index_info)) {
              FLOG_ERROR("[INDEX NAME CACHE] duplicated index info exist",
                         KR(ret), KPC(index_name_info), KPC(*exist_index_info));
            }
          } else {
            (void) inner_reset_cache_();
          }
        } else {
          FLOG_INFO("[INDEX NAME CACHE] add index name to cache", KR(ret), KPC(index_name_info));
        }
      }
    }
  }
  return ret;
}

// need protect by mutex_
int ObIndexNameCache::try_load_cache_()
{
  int ret = OB_SUCCESS;
  if (loaded_) {
    // do nothing
  } else {
    (void) inner_reset_cache_();

    ObRefreshSchemaStatus schema_status;
    
    int64_t schema_version = OB_INVALID_VERSION;
    int64_t timeout_ts = OB_INVALID_TIMESTAMP;
    if (OB_FAIL(GSCHEMASERVICE.get_schema_version_in_inner_table(
        sql_proxy_, schema_status, schema_version))) {
      LOG_WARN("fail to get schema version", KR(ret), K(schema_status));
    } else if (!ObSchemaService::is_formal_version(schema_version)) {
      ret = OB_EAGAIN;
      LOG_WARN("schema version is informal, need retry", KR(ret), K(schema_status), K(schema_version));
    } else if (OB_FAIL(ObShareUtil::get_abs_timeout(GCONF.internal_sql_execute_timeout, timeout_ts))) {
      LOG_WARN("fail to get timeout", KR(ret));
    } else {
      int64_t original_timeout_ts = THIS_WORKER.get_timeout_ts();
      THIS_WORKER.set_timeout_ts(timeout_ts);

      ObSchemaGetterGuard guard;
      int64_t start_time = ObTimeUtility::current_time();
      if (OB_FAIL(GSCHEMASERVICE.async_refresh_schema(schema_version))) {
        LOG_WARN("fail to refresh schema", KR(ret), K(schema_version));
      } else if (OB_FAIL(GSCHEMASERVICE.get_tenant_schema_guard(guard))) {
        LOG_WARN("fail to get schema guard", KR(ret));
      } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
        LOG_WARN("fail to get schema version", KR(ret));
      } else if (OB_FAIL(guard.deep_copy_index_name_map(allocator_, cache_))) {
        LOG_WARN("fail to deep copy index name map", KR(ret));
      } else {
        loaded_ = true;
        FLOG_INFO("[INDEX NAME CACHE] load index name map", KR(ret),
                  K(schema_version), "cost", ObTimeUtility::current_time() - start_time);
      }

      if (OB_FAIL(ret)) {
        (void) inner_reset_cache_();
        LOG_WARN("load index name map failed", KR(ret),
                 K(schema_version), "cost", ObTimeUtility::current_time() - start_time);
      }

      THIS_WORKER.set_timeout_ts(original_timeout_ts);
    }
  }
  return ret;
}

ObIndexNameChecker::ObIndexNameChecker()
  : rwlock_(),
    allocator_(ObMemAttr("IndNameCache", ObCtxIds::SCHEMA_SERVICE)),
    index_name_cache_member_(NULL),
    sql_proxy_(NULL),
    inited_(false)
{
}

ObIndexNameChecker::~ObIndexNameChecker()
{
  destroy();
}

int ObIndexNameChecker::init(common::ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(rwlock_);
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    sql_proxy_ = &sql_proxy;
    inited_ = true;
  }
  return ret;
}

void ObIndexNameChecker::destroy()
{
  SpinWLockGuard guard(rwlock_);
  if (inited_) {
    if (OB_NOT_NULL(index_name_cache_member_)) {
      index_name_cache_member_->~ObIndexNameCache();
      index_name_cache_member_ = NULL;
    }
    allocator_.reset();
    sql_proxy_ = NULL;
    inited_ = false;
  }
}

void ObIndexNameChecker::reset_all_cache()
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(rwlock_);
  if (inited_) {
    if (OB_NOT_NULL(index_name_cache_member_)) {
      (void) index_name_cache_member_->reset_cache();
    }
  }
}

int ObIndexNameChecker::reset_cache()
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(rwlock_);
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ObIndexNameCache *cache = index_name_cache_member_;
    if (OB_ISNULL(cache)) {
      // tenant not in cache, just skip
    } else {
      (void) cache->reset_cache();
    }
  }
  return ret;
}

int ObIndexNameChecker::check_index_name_exist(
    const uint64_t database_id,
    const ObString &index_name,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  bool can_skip = false;
  is_exist = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(check_tenant_can_be_skipped_(can_skip))) {
    LOG_WARN("fail to check tenant", KR(ret));
  } else if (can_skip) {
    // do nothing
  } else if (OB_UNLIKELY(false
             || OB_INVALID_ID == database_id
             || index_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(database_id), K(index_name));
  } else if (OB_FAIL(try_init_index_name_cache_map_())) {
    LOG_WARN("fail to init index name cache", KR(ret));
  } else {
    SpinRLockGuard guard(rwlock_);
    ObIndexNameCache *cache = index_name_cache_member_;
    if (OB_ISNULL(cache)) {
      ret = OB_HASH_NOT_EXIST;
      LOG_WARN("fail to get refactored", KR(ret));
    } else if (OB_FAIL(cache->check_index_name_exist(
      database_id, index_name, is_exist))) {
      LOG_WARN("fail to check index name exist",
               KR(ret), K(database_id), K(index_name));
    }
  }
  return ret;
}

int ObIndexNameChecker::add_index_name(
    const share::schema::ObTableSchema &index_schema)
{
  int ret = OB_SUCCESS;
  
  bool can_skip = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(check_tenant_can_be_skipped_(can_skip))) {
    LOG_WARN("fail to check tenant", KR(ret));
  } else if (can_skip) {
    // do nothing
  } else if (OB_UNLIKELY(false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret));
  } else if (OB_FAIL(try_init_index_name_cache_map_())) {
    LOG_WARN("fail to init index name cache", KR(ret));
  } else {
    SpinRLockGuard guard(rwlock_);
    ObIndexNameCache *cache = index_name_cache_member_;
    if (OB_ISNULL(cache)) {
      ret = OB_HASH_NOT_EXIST;
      LOG_WARN("fail to get refactored", KR(ret));
    } else if (OB_FAIL(cache->add_index_name(index_schema))) {
      LOG_WARN("fail to add index name", KR(ret), K(index_schema));
    }
  }
  return ret;
}

// only cache oracle tenant's index name map
int ObIndexNameChecker::check_tenant_can_be_skipped_(bool &can_skip)
{
  int ret = OB_SUCCESS;
  can_skip = false;
  if (true) {
    can_skip = true;
  } else {
    can_skip = true; // always MySQL mode
  }
  return ret;
}

int ObIndexNameChecker::try_init_index_name_cache_map_()
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(rwlock_);
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql_proxy is null", KR(ret));
  } else {
    ObIndexNameCache *cache = index_name_cache_member_;
    if (OB_NOT_NULL(cache)) {
      // cache exist, just skip
    } else {
      cache = NULL;
      void *buf = NULL;
      if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObIndexNameCache)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc memory", KR(ret));
      } else if (FALSE_IT(cache = new (buf) ObIndexNameCache(*sql_proxy_))) {
      } else {
        index_name_cache_member_ = cache;
      }
    }
  }
  return ret;
}
