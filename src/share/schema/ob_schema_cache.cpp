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


#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_schema_cache.h"
#include "share/cache/ob_cache_name_define.h"
#include "share/ob_sql_client_decorator.h"
#include "share/ob_server_struct.h"
#include "lib/utility/ob_smart_call.h"
namespace oceanbase
{
using namespace common;

namespace share
{
namespace schema
{

ObSchemaCacheKey::ObSchemaCacheKey()
  : schema_type_(OB_MAX_SCHEMA),
    schema_id_(OB_INVALID_ID),
    schema_version_(OB_INVALID_VERSION)
{
}

ObSchemaCacheKey::ObSchemaCacheKey(const ObSchemaType schema_type,
                                   const uint64_t schema_id,
                                   const uint64_t schema_version)
  : schema_type_(schema_type),
    schema_id_(schema_id),
    schema_version_(schema_version)
{
}
// for calc resource


bool ObSchemaCacheKey::operator ==(const ObIKVCacheKey &other) const
{
  const ObSchemaCacheKey &other_key = reinterpret_cast<const ObSchemaCacheKey &>(other);
  return schema_type_ == other_key.schema_type_
         && schema_id_ == other_key.schema_id_
         && schema_version_ == other_key.schema_version_;
}

uint64_t ObSchemaCacheKey::hash() const
{
  uint64_t hash_code = 0;
  hash_code = murmurhash(&schema_type_, sizeof(schema_type_), hash_code);
  hash_code = murmurhash(&schema_id_, sizeof(schema_id_), hash_code);
  hash_code = murmurhash(&schema_version_, sizeof(schema_version_), hash_code);
  return hash_code;
}

int64_t ObSchemaCacheKey::size() const
{
  return sizeof(*this);
}

int ObSchemaCacheKey::deep_copy(char *buf,
                                const int64_t buf_len,
                                ObIKVCacheKey *&key) const
{
  int ret = OB_SUCCESS;
  ObSchemaCacheKey *pkey = NULL;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(buf_len), K(size()));
  } else {
    pkey = new (buf) ObSchemaCacheKey();
    *pkey = *this;
    key = pkey;
  }
  return ret;
}

ObSchemaCacheValue::ObSchemaCacheValue()
  : schema_type_(OB_MAX_SCHEMA),
    schema_(NULL)
{
}

ObSchemaCacheValue::ObSchemaCacheValue(ObSchemaType schema_type, const ObSchema *schema)
  : schema_type_(schema_type),
    schema_(schema)
{
}

int64_t ObSchemaCacheValue::size() const
{
  return sizeof(*this) +
      (NULL != schema_ ? schema_->get_convert_size() : 0) +
      sizeof(ObDataBuffer);
}

int ObSchemaCacheValue::deep_copy(char *buf,
                                  const int64_t buf_len,
                                  ObIKVCacheValue *&value) const
{
  #define DEEP_COPY_SCHEMA(schema)                     \
    pvalue = new (buf) ObSchemaCacheValue();           \
    const schema *old_var = static_cast<const schema *> (schema_); \
    schema *new_var = NULL;                            \
    if (OB_FAIL(ObSchemaUtils::deep_copy_schema(buf + sizeof(*this), \
        *old_var, new_var))) {                         \
      LOG_WARN("deep copy schema failed", K(ret));     \
    } else {                                           \
      pvalue->schema_type_ = schema_type_;             \
      pvalue->schema_ = new_var;                       \
    }

  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret), K(schema_));
  } else if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(buf_len), K(size()));
  } else {
    ObSchemaCacheValue *pvalue = NULL;
    switch (schema_type_) {
    case TENANT_SCHEMA: {
      DEEP_COPY_SCHEMA(ObTenantSchema);
      break;
    }
    case USER_SCHEMA: {
      DEEP_COPY_SCHEMA(ObUserInfo);
      break;
    }
    case DATABASE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObDatabaseSchema);
      break;
    }
    case TABLEGROUP_SCHEMA: {
      DEEP_COPY_SCHEMA(ObTablegroupSchema);
      break;
    }
    case TABLE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObTableSchema);
      break;
    }
    case TABLE_SIMPLE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObSimpleTableSchemaV2);
      break;
    }
    case OUTLINE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObOutlineInfo);
      break;
    }
    case ROUTINE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObRoutineInfo);
      break;
    }
    case PACKAGE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObPackageInfo);
      break;
    }
    case TRIGGER_SCHEMA: {
      DEEP_COPY_SCHEMA(ObTriggerInfo);
      break;
    }
    case UDF_SCHEMA: {
      DEEP_COPY_SCHEMA(ObUDF);
      break;
    }
    case SEQUENCE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObSequenceSchema);
      break;
    }
    case SYS_VARIABLE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObSysVariableSchema);
      break;
    }
    case MOCK_FK_PARENT_TABLE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObMockFKParentTableSchema);
      break;
    }
    case CCL_RULE_SCHEMA: {
      DEEP_COPY_SCHEMA(ObCCLRuleSchema);
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("should not reach here", K(ret), K(schema_type_));
      break;
    }
    }
    if (OB_SUCC(ret)) {
      value = pvalue;
    }
  }

  #undef DEEP_COPY_SCHEMA

  return ret;
}

ObSchemaHistoryCacheValue::ObSchemaHistoryCacheValue()
  : schema_version_(OB_INVALID_VERSION)
{
}

ObSchemaHistoryCacheValue::ObSchemaHistoryCacheValue(
  const int64_t schema_version)
  : schema_version_(schema_version)
{
}

int64_t ObSchemaHistoryCacheValue::size() const
{
  return sizeof(*this);
}

int ObSchemaHistoryCacheValue::deep_copy(
    char *buf,
    const int64_t buf_len,
    ObIKVCacheValue *&value) const
{
  int ret = OB_SUCCESS;
  ObSchemaHistoryCacheValue *schema_history_value = NULL;
  if (OB_ISNULL(buf) || buf_len < size()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invaild arg", KR(ret), KP(buf), K(buf_len));
  } else {
    schema_history_value = new (buf) ObSchemaHistoryCacheValue(schema_version_);
    value = schema_history_value;
  }
  return ret;
}

ObTabletCacheKey::ObTabletCacheKey()
  : tablet_id_(),
    schema_version_(OB_INVALID_VERSION)
{
}

ObTabletCacheKey::ObTabletCacheKey(const ObTabletID &tablet_id,
                                   const uint64_t schema_version)
  : tablet_id_(tablet_id),
    schema_version_(schema_version)
{
}

int ObTabletCacheKey::init(
    const ObTabletID &tablet_id,
    const uint64_t schema_version)
{
  int ret = OB_SUCCESS;
  tablet_id_ = tablet_id;
  schema_version_ = schema_version;
  return ret;
}

bool ObTabletCacheKey::is_valid() const
{
  return true
          && tablet_id_.is_valid_with_tenant()
          && schema_version_ > 0;
}



bool ObTabletCacheKey::operator ==(const ObIKVCacheKey &other) const
{
  const ObTabletCacheKey &other_key = reinterpret_cast<const ObTabletCacheKey &>(other);
  return true
          && tablet_id_ == other_key.tablet_id_
          && schema_version_ == other_key.schema_version_;
}

uint64_t ObTabletCacheKey::hash() const
{
  uint64_t hash_code = 0;
  hash_code = murmurhash(&tablet_id_, sizeof(ObTabletID), 0);
  hash_code = murmurhash(&schema_version_, sizeof(int64_t), hash_code);
  return hash_code;
}

int64_t ObTabletCacheKey::size() const
{
  return sizeof(*this);
}

int ObTabletCacheKey::deep_copy(char *buf,
                                const int64_t buf_len,
                                ObIKVCacheKey *&key) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(buf_len), K(size()));
  } else {
    ObTabletCacheKey *new_key = new (buf) ObTabletCacheKey();
    if (OB_ISNULL(new_key)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new key ptr is null", KR(ret), KPC(this));
    } else if (OB_FAIL(new_key->init(tablet_id_, schema_version_))) {
      LOG_WARN("fail to init tablet cache key", KR(ret), KPC(this));
    } else {
      key = new_key;
    }
  }
  return ret;
}

ObTabletCacheValue::ObTabletCacheValue()
  : table_id_(OB_INVALID_ID)
{
}

ObTabletCacheValue::ObTabletCacheValue(const uint64_t table_id)
  : table_id_(table_id)
{
}

int ObTabletCacheValue::init(const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  table_id_ = table_id;
  return ret;
}

int64_t ObTabletCacheValue::size() const
{
  return sizeof(*this);
}

int ObTabletCacheValue::deep_copy(char *buf,
                                  const int64_t buf_len,
                                  ObIKVCacheValue *&value) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(buf_len), K(size()));
  } else {
    ObTabletCacheValue *new_value = new (buf) ObTabletCacheValue();
    if (OB_ISNULL(new_value)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new value ptr is null", KR(ret), KPC(this));
    } else if (OB_FAIL(new_value->init(table_id_))) {
      LOG_WARN("fail to init value", KR(ret), KPC(this));
    } else {
      value = new_value;
    }
  }
  return ret;
}

ObSchemaCache::ObSchemaCache()
  : cache_(),
    history_cache_(),
    is_inited_(false)
{
}

ObSchemaCache::~ObSchemaCache()
{
  destroy();
}

void ObSchemaCache::destroy()
{
  tablet_cache_.destroy();
  cache_.destroy();
}

int ObSchemaCache::init_all_core_table()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObInnerTableSchema::all_core_table_schema(all_core_table_))) {
    LOG_WARN("all_core_table_schema failed", K(ret));
  }
  return ret;
}

const ObTableSchema *ObSchemaCache::get_all_core_table() const
{
  return &all_core_table_;
}

int ObSchemaCache::init()
{
  int ret = OB_SUCCESS;
  // TODO, configurable
  if (OB_FAIL(cache_.init(OB_SCHEMA_CACHE_NAME))) {
    LOG_WARN("init schema cache failed", KR(ret));
  } else if (OB_FAIL(history_cache_.init(OB_SCHEMA_HISTORY_CACHE_NAME))) {
    LOG_WARN("init schema history cache failed", K(ret));
  } else if (OB_FAIL(tablet_cache_.init(OB_TABLET_TABLE_CACHE_NAME))) {
    LOG_WARN("init tablet-table cache failed", KR(ret));
  } else if (OB_FAIL(init_all_core_table())) {
    LOG_WARN("init all_core_table cache failed", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

bool ObSchemaCache::check_inner_stat() const
{
  bool ret = true;

  if (!is_inited_) {
    ret = false;
    LOG_WARN("inner stat error", K(is_inited_));
  }

  return ret;
}

bool ObSchemaCache::is_valid_key(
     ObSchemaType schema_type,
     const uint64_t schema_id,
     const int64_t schema_version) const
{
  return OB_MAX_SCHEMA != schema_type
         && OB_INVALID_ID != schema_id
         && schema_version >= 0;
}

int ObSchemaCache::get_schema(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const int64_t schema_version,
    ObKVCacheHandle &handle,
    const ObSchema *&schema)
{
  int ret = OB_SUCCESS;
  handle.reset();
  schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (!is_valid_key(schema_type, schema_id, schema_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_type),
             K(schema_id), K(schema_version));
  } else {
    ObSchemaCacheKey cache_key(schema_type, schema_id, schema_version);
    const ObSchemaCacheValue *cache_value = NULL;
    if (OB_FAIL(cache_.get(cache_key, cache_value, handle))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("get value from cache failed", K(cache_key), K(ret));
      }
      EVENT_INC(ObStatEventIds::SCHEMA_CACHE_MISS);
    } else {
      LOG_DEBUG("get value from cache succeed", K(cache_key), K(ret));
      EVENT_INC(ObStatEventIds::SCHEMA_CACHE_HIT);
    }

    if (OB_SUCC(ret)) {
      if (OB_ISNULL(cache_value)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("cache_value is NULL", KP(cache_value), K(ret));
      } else {
        schema = cache_value->schema_;
      }
    }
  }

  return ret;
}

int ObSchemaCache::put_schema(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const int64_t schema_version,
    const ObSchema &schema)
{
  int ret = OB_SUCCESS;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!is_valid_key(schema_type, schema_id, schema_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type),
             K(schema_id), K(schema_version));
  } else {
    ObSchemaCacheKey cache_key(schema_type, schema_id, schema_version);
    ObSchemaCacheValue cache_value(schema_type, &schema);
    if (OB_FAIL(cache_.put(cache_key, cache_value))) {
      LOG_WARN("put value to schema cache failed",
               K(cache_key), K(cache_value), KR(ret));
    } else {
      LOG_DEBUG("put value to schema cache succeed", K(cache_key), K(cache_value));
    }
  }
  return ret;
}

int ObSchemaCache::put_and_fetch_schema(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const int64_t schema_version,
    const ObSchema &schema,
    ObKVCacheHandle &handle,
    const ObSchema *&new_schema)
{
  int ret = OB_SUCCESS;
  ObSchemaCacheKey cache_key(schema_type, schema_id, schema_version);
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!is_valid_key(schema_type, schema_id, schema_version)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type),
             K(schema_id), K(schema_version));
  } else {
    ObSchemaCacheValue cache_value(schema_type, &schema);
    const ObSchemaCacheValue *new_cache_value = NULL;
    if (OB_FAIL(cache_.put_and_fetch(cache_key, cache_value, new_cache_value, handle))) {
      LOG_WARN("put and fetch schema cache failed",
               K(cache_key), K(cache_value), KR(ret));
    } else if (OB_ISNULL(new_cache_value)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("new cache value is null", KR(ret), K(cache_key));
    } else {
      new_schema = new_cache_value->schema_;
      LOG_DEBUG("put and fetch schema cache succeed", K(cache_key), K(cache_value));
    }
  }
  return ret;
}

int ObSchemaCache::get_tablet_cache(
    const ObTabletCacheKey &key,
    uint64_t &table_id)
{
  int ret = OB_SUCCESS;
  ObKVCacheHandle handle;
  const ObTabletCacheValue *value = NULL;
  table_id = OB_INVALID_ID;
  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid cache key", KR(ret), K(key));
  } else if (OB_FAIL(tablet_cache_.get(key, value, handle))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("fail to get tablet-table pair from cache", KR(ret), K(key));
    }
  } else if (OB_ISNULL(value)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("value is null", KR(ret), K(key));
  } else {
    table_id = value->get_table_id();
  }
  LOG_TRACE("get tablet-table cache", KR(ret), K(key), K(table_id));
  return ret;
}

int ObSchemaCache::put_tablet_cache(
    const ObTabletCacheKey &key,
    const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObTabletCacheValue value;
  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid cache key", KR(ret), K(key));
  } else if (OB_FAIL(value.init(table_id))) {
    LOG_WARN("fail to init value", KR(ret), K(table_id));
  } else if (OB_FAIL(tablet_cache_.put(key, value))) {
    LOG_WARN("fail to put tablet cache", KR(ret), K(key), K(value));
  }
  LOG_TRACE("put tablet-table cache", KR(ret), K(key), K(value));
  return ret;
}

int ObSchemaCache::get_schema_history_cache(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const int64_t schema_version,
    int64_t &precise_schema_version)
{
  int ret = OB_SUCCESS;
  precise_schema_version = OB_INVALID_VERSION;
  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(!is_valid_key(schema_type, schema_id, schema_version))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type), K(schema_id), K(schema_version));
  } else {
    ObSchemaCacheKey cache_key(schema_type, schema_id, schema_version);
    const ObSchemaHistoryCacheValue *cache_value = NULL;
    ObKVCacheHandle handle;
    if (OB_FAIL(history_cache_.get(cache_key, cache_value, handle))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("fail to get schema history value", KR(ret), K(cache_key));
      }
      EVENT_INC(ObStatEventIds::SCHEMA_HISTORY_CACHE_MISS);
    } else if (OB_ISNULL(cache_value)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cache_value is null", KR(ret), KP(cache_value));
    } else {
      precise_schema_version = cache_value->schema_version_;
      EVENT_INC(ObStatEventIds::SCHEMA_HISTORY_CACHE_HIT);
      LOG_TRACE("get schema history cache succeed", KR(ret), K(cache_key));
    }
  }
  return ret;
}

int ObSchemaCache::put_schema_history_cache(
    const ObSchemaType schema_type,
    const uint64_t schema_id,
    const int64_t schema_version,
    const int64_t precise_schema_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!check_inner_stat())) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_UNLIKELY(
             !is_valid_key(schema_type, schema_id, schema_version)
             || precise_schema_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_type), K(schema_id),
             K(schema_version), K(precise_schema_version));
  } else {
    ObSchemaCacheKey cache_key(schema_type, schema_id, schema_version);
    ObSchemaHistoryCacheValue cache_value(precise_schema_version);
    if (OB_FAIL(history_cache_.put(cache_key, cache_value))) {
      LOG_WARN("put value to schema cache failed", KR(ret), K(cache_key), K(cache_value));
    } else {
      LOG_TRACE("put schema history cache succeed", KR(ret), K(cache_key), K(cache_value));
    }
  }
  return ret;
}

ObSchemaFetcher::ObSchemaFetcher()
  : schema_service_(NULL),
    sql_client_(NULL),
    is_inited_(false)
{
}

int ObSchemaFetcher::init(ObSchemaService *schema_service,
                          ObISQLClient *sql_client)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(schema_service) ||
      OB_ISNULL(sql_client)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_service),
             K(sql_client));
  } else {
    schema_service_ = schema_service;
    sql_client_ = sql_client;
    is_inited_ = true;
  }
  return ret;
}

bool ObSchemaFetcher::check_inner_stat() const
{
  bool ret = true;
  if (!is_inited_ ||
      NULL == schema_service_ ||
      NULL == sql_client_) {
    ret = false;
    LOG_WARN("inner stat error", K(is_inited_), K(schema_service_),
             K(sql_client_));
  }
  return ret;
}

int ObSchemaFetcher::fetch_schema(ObSchemaType schema_type,
                                  const ObRefreshSchemaStatus &schema_status,
                                  uint64_t schema_id,
                                  int64_t schema_version,
                                  common::ObIAllocator &allocator,
                                  ObSchema *&schema)
{
  int ret = OB_SUCCESS;

  LOG_TRACE("fetch schema", K(schema_type), K(schema_id), K(schema_version));

  bool retry = false;
  const int64_t RETRY_TIMES_MAX = 8;
  int64_t retry_times = 0;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else {
    do {
      if (INT64_MAX == schema_version) {
        // skip inspection while fetch latest schema
      } else if (OB_FAIL(schema_service_->can_read_schema_version(schema_status, schema_version))) {
        LOG_WARN("incremant schema is not readable now, waiting and retry", K(ret), K(retry_times), K(schema_version));
        if (OB_SCHEMA_EAGAIN == ret) {
          retry = (retry_times++ < RETRY_TIMES_MAX);
          if (retry) {
            ob_usleep(10000000);
            continue;
          } else {
            break;
          }
        } else {
          break;
        }
      } else {
        LOG_TRACE("schema version is readable", K(schema_type), K(schema_version), K(retry_times), K(schema_id));
      }
      schema = NULL;
      switch (schema_type) {
      case TENANT_SCHEMA: {
          ObTenantSchema *tenant_schema = NULL;
          if (OB_FAIL(fetch_tenant_schema(schema_version,
                                          allocator,
                                          tenant_schema))) {
            LOG_WARN("fetch tenant schema failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = tenant_schema;
          }
          break;
        }
      case SYS_VARIABLE_SCHEMA: {
          ObSysVariableSchema *sys_variable_schema = NULL;
          if (OB_FAIL(fetch_sys_variable_schema(schema_status,
                                                schema_version,
                                                allocator,
                                                sys_variable_schema))) {
            LOG_WARN("fetch sys variable schema failed", K(ret), K(schema_id), K(schema_version));
          } else {
            schema = sys_variable_schema;
          }
          break;
        }
      case USER_SCHEMA: {
          ObUserInfo *user_info = NULL;
          if (OB_FAIL(fetch_user_info(schema_status,
                                      schema_id,
                                      schema_version,
                                      allocator,
                                      user_info))) {
            LOG_WARN("fetch user info failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = user_info;
          }
          break;
        }
      case DATABASE_SCHEMA: {
          ObDatabaseSchema *db_schema = NULL;
          if (OB_FAIL(fetch_database_schema(schema_status,
                                            schema_id,
                                            schema_version,
                                            allocator,
                                            db_schema))) {
            LOG_WARN("fetch database schema failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = db_schema;
          }
          break;
        }
      case TABLEGROUP_SCHEMA: {
          ObTablegroupSchema *tg_schema = NULL;
          if (OB_FAIL(fetch_tablegroup_schema(schema_status,
                                              schema_id,
                                              schema_version,
                                              allocator,
                                              tg_schema))) {
            LOG_WARN("fetch tablegroup schema failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = tg_schema;
          }
          break;
        }
      case TABLE_SCHEMA: {
          ObTableSchema *table_schema = NULL;
          if (OB_FAIL(fetch_table_schema(schema_status,
                                         schema_id,
                                         schema_version,
                                         allocator,
                                         table_schema))) {
            LOG_WARN("fetch table schema failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = table_schema;
          }
          break;
        }
      case TABLE_SIMPLE_SCHEMA: {
          ObSimpleTableSchemaV2 *table_schema = NULL;
          if (OB_FAIL(fetch_table_schema(schema_status,
                                         schema_id,
                                         schema_version,
                                         allocator,
                                         table_schema))) {
            LOG_WARN("fetch table schema failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = table_schema;
          }
          break;
        }
      case OUTLINE_SCHEMA: {
          ObOutlineInfo *outline_info = NULL;
          if (OB_FAIL(fetch_outline_info(schema_status,
                                         schema_id,
                                         schema_version,
                                         allocator,
                                         outline_info))) {
            LOG_WARN("fetch outline info failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = outline_info;
          }
          break;
        }
      case ROUTINE_SCHEMA: {
          ObRoutineInfo *routine_info = NULL;
          if (OB_FAIL(fetch_routine_info(schema_status, schema_id, schema_version, allocator, routine_info))) {
            LOG_WARN("fetch procedure info failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = routine_info;
          }
          break;
        }
      case PACKAGE_SCHEMA: {
          ObPackageInfo *package_info = NULL;
          if (OB_FAIL(fetch_package_info(schema_status, schema_id, schema_version, allocator, package_info))) {
            LOG_WARN("fetch package info failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = package_info;
          }
          break;
        }
      case TRIGGER_SCHEMA: {
          ObTriggerInfo *trigger_info = NULL;
          if (OB_FAIL(fetch_trigger_info(schema_status, schema_id, schema_version, allocator, trigger_info))) {
            LOG_WARN("fetch trigger info failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = trigger_info;
          }
          break;
        }
      case UDF_SCHEMA: {
          ObUDF *udf_info = NULL;
          if (OB_FAIL(fetch_udf_info(schema_status, schema_id, schema_version, allocator, udf_info))) {
            LOG_WARN("fetch udf info failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = udf_info;
          }
          break;
        }
      case SEQUENCE_SCHEMA: {
          ObSequenceSchema *seq_schema = NULL;
          if (OB_FAIL(fetch_sequence_info(schema_status, schema_id, schema_version, allocator, seq_schema))) {
            LOG_WARN("fetch sequence schema failed", K(ret));
          } else {
            schema = seq_schema;
          }
          break;
        }
      case MOCK_FK_PARENT_TABLE_SCHEMA: {
        ObMockFKParentTableSchema *mock_fk_parent_table_schema = NULL;
          if (OB_FAIL(fetch_mock_fk_parent_table_info(schema_status, schema_id, schema_version, allocator, mock_fk_parent_table_schema))) {
            LOG_WARN("fetch mock_fk_parent_table_schema failed", K(ret), K(schema_status), K(schema_id), K(schema_version));
          } else {
            schema = mock_fk_parent_table_schema;
          }
          break;
        }
      case CCL_RULE_SCHEMA: {
        ObCCLRuleSchema *ccl_rule_schema = NULL;
        if (OB_FAIL(fetch_ccl_rule_info(
                schema_status, schema_id, schema_version, allocator,
                ccl_rule_schema))) {
          LOG_WARN("fetch ccl_rule_schema failed", K(ret),
                    K(schema_status), K(schema_id), K(schema_version));
        } else {
          schema = ccl_rule_schema;
        }
        break;
      }
      default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unknown schema type, should not reach here", K(ret), K(schema_type));
          break;
        }
      }
      retry = (OB_CONNECT_ERROR == ret) && retry_times++ < RETRY_TIMES_MAX;
      if (retry) {
        ob_usleep(10000000);
      }
    } while (retry);
  }
  return ret;
}

int ObSchemaFetcher::fetch_tenant_schema(int64_t schema_version,
                                         common::ObIAllocator &allocator,
                                         ObTenantSchema *&tenant_schema)
{
  int ret = OB_SUCCESS;
  tenant_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_version));
  } else {
    ObTenantSchema *tmp_tenant_schema = NULL;
    ObArray<ObTenantSchema> tenant_schema_array;
    if (OB_FAIL(schema_service_->get_batch_tenants(*sql_client_,
                                                   schema_version,
                                                   tenant_schema_array))) {
      LOG_WARN("get tenant schema failed", K(ret));
    } else if (1 != tenant_schema_array.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected schema count", K(tenant_schema_array.count()),
               K(schema_version), K(ret));
    } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator,
                                                   tenant_schema_array.at(0),
                                                   tmp_tenant_schema))) {
      LOG_WARN("alloc tenant schema failed", K(ret));
    } else if (OB_ISNULL(tmp_tenant_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", K(tmp_tenant_schema), K(schema_version), K(ret));
    } else {
      tenant_schema = tmp_tenant_schema;
      LOG_TRACE("fetch tenant schema succeed", K(schema_version),
                "tenant_name", tenant_schema->get_tenant_name_str());
    }
  }

  return ret;
}

int ObSchemaFetcher::fetch_sys_variable_schema(
    const ObRefreshSchemaStatus &schema_status,
    int64_t schema_version,
    common::ObIAllocator &allocator,
    ObSysVariableSchema *&sys_variable_schema)
{
  int ret = OB_SUCCESS;
  ObSysVariableSchema tmp_schema;
  
  tmp_schema.set_schema_version(schema_version);
  sys_variable_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_version));
  } else if (OB_FAIL(schema_service_->get_sys_variable_schema(
                     *sql_client_,
                     schema_status,
                     schema_version,
                     tmp_schema))) {
    LOG_WARN("get tenant schema failed", K(ret), K(schema_version), K(schema_status));
  } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator,
                                                 tmp_schema,
                                                 sys_variable_schema))) {
    LOG_WARN("alloc sys variable schema failed", K(ret), K(schema_version), K(schema_status));
  } else if (OB_ISNULL(sys_variable_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sys_variable_schema is null", K(ret), K(schema_version), K(schema_status));
  } else {
    LOG_TRACE("fetch sys variable schema succeed", K(schema_version), K(schema_status));
  }

  return ret;
}

int ObSchemaFetcher::fetch_database_schema(const ObRefreshSchemaStatus &schema_status,
                                           uint64_t database_id,
                                           int64_t schema_version,
                                           common::ObIAllocator &allocator,
                                           ObDatabaseSchema *&database_schema)
{
  int ret = OB_SUCCESS;
  database_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_INVALID_ID == database_id || schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(database_id), K(schema_version));
  } else {
    ObDatabaseSchema *tmp_db_schema = NULL;
    ObArray<uint64_t> db_ids;
    ObArray<ObDatabaseSchema> db_schema_array;
    if (OB_FAIL(db_ids.push_back(database_id))) {
      LOG_WARN("push back database id failed", K(ret), K(database_id));
    } else if (OB_FAIL(schema_service_->get_batch_databases(schema_status,
                                                            schema_version,
                                                            db_ids,
                                                            *sql_client_,
                                                            db_schema_array))) {
      LOG_WARN("get database schema failed", K(ret));
    } else if (1 != db_schema_array.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected schema count", K(db_schema_array.count()),
               K(database_id), K(schema_version), K(ret));
    } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator,
                                                   db_schema_array.at(0),
                                                   tmp_db_schema))) {
      LOG_WARN("alloc database schema failed", K(ret));
    } else if (OB_ISNULL(tmp_db_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", K(tmp_db_schema), K(database_id), K(schema_version), K(ret));
    } else {
      database_schema = tmp_db_schema;
      LOG_TRACE("fetch database schema succeed", K(database_id), K(schema_version),
                "database_name", database_schema->get_database_name_str());
    }
  }

  return ret;
}

int ObSchemaFetcher::fetch_tablegroup_schema(const ObRefreshSchemaStatus &schema_status,
                                             uint64_t tablegroup_id,
                                             int64_t schema_version,
                                             common::ObIAllocator &allocator,
                                             ObTablegroupSchema *&tablegroup_schema)
{
  int ret = OB_SUCCESS;
  tablegroup_schema = NULL;

  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_INVALID_ID == tablegroup_id || schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablegroup_id), K(schema_version));
  } else if (OB_FAIL(schema_service_->get_tablegroup_schema(schema_status,
                                                            tablegroup_id,
                                                            schema_version,
                                                            *sql_client_,
                                                            allocator,
                                                            tablegroup_schema))) {
    LOG_WARN("get tablegroup schema failed", K(ret));
  } else if (OB_ISNULL(tablegroup_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(tablegroup_schema), K(tablegroup_id), K(schema_version), K(ret));
  } else {
    LOG_TRACE("fetch tablegroup schema succeed", K(tablegroup_id), K(schema_version),
              "tablegroup_name", tablegroup_schema->get_tablegroup_name());
  }
  return ret;
}

int ObSchemaFetcher::fetch_table_schema(const ObRefreshSchemaStatus &schema_status,
                                        uint64_t table_id,
                                        int64_t schema_version,
                                        common::ObIAllocator &allocator,
                                        ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  table_schema = NULL;

  ObTableSchema *tmp_table_schema = NULL;
  // TODO, use old interface? get_batch_table_schema...
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K(ret));
  } else if (OB_INVALID_ID == table_id || schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id), K(schema_version));
  } else {
    ObSQLClientRetryWeak sql_client_retry_weak(sql_client_,
                                               false,
                                               schema_status.snapshot_timestamp_,
                                               false);
    ObISQLClient *schema_sql_client = is_schema_fetch_dependency_table(table_id)
                                      ? static_cast<ObISQLClient *>(&sql_client_retry_weak)
                                      : sql_client_;
    if (OB_FAIL(schema_service_->get_table_schema(schema_status,
                                                  table_id,
                                                  schema_version,
                                                  *schema_sql_client,
                                                  allocator,
                                                  tmp_table_schema))) {
      LOG_WARN("get table schema failed", K(ret), K(table_id), K(schema_version));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(tmp_table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", K(tmp_table_schema), K(table_id), K(schema_version), K(ret));
    } else {
      table_schema = tmp_table_schema;
      LOG_TRACE("fetch table schema succeed", K(table_id), K(schema_version),
                "table_name", table_schema->get_table_name_str());
    }
  }

  return ret;
}

int ObSchemaFetcher::fetch_table_schema(const ObRefreshSchemaStatus &schema_status,
                                        uint64_t table_id,
                                        int64_t schema_version,
                                        common::ObIAllocator &allocator,
                                        ObSimpleTableSchemaV2 *&table_schema)
{
  int ret = OB_SUCCESS;
  table_schema = NULL;

  SchemaKey table_schema_key;
  
  table_schema_key.table_id_ = table_id;
  ObArray<SchemaKey> schema_keys;
  ObArray<ObSimpleTableSchemaV2 *> schema_array;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (OB_INVALID_ID == table_id || schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(table_id), K(schema_version));
  } else if (OB_FAIL(schema_keys.push_back(table_schema_key))) {
    LOG_WARN("fail to push back schema key", KR(ret), K(table_id), K(schema_version));
  } else {
    ObSQLClientRetryWeak sql_client_retry_weak(sql_client_,
                                               false,
                                               schema_status.snapshot_timestamp_,
                                               false);
    ObISQLClient *schema_sql_client = is_schema_fetch_dependency_table(table_id)
                                      ? static_cast<ObISQLClient *>(&sql_client_retry_weak)
                                      : sql_client_;
    if (OB_FAIL(schema_service_->get_batch_tables(schema_status,
                                                  *schema_sql_client,
                                                  allocator,
                                                  schema_version,
                                                  schema_keys,
                                                  schema_array))) {
      LOG_WARN("get table schema failed", KR(ret), K(table_id), K(schema_version));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(1 != schema_array.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected schema count", KR(ret), K(table_id), K(schema_version));
    } else {
      table_schema = schema_array.at(0);
      LOG_TRACE("fetch table schema succeed", KR(ret), K(table_id), K(schema_version), KPC(table_schema));
    }
  }
  return ret;
}

#ifndef DEF_SCHEMA_INFO_FETCHER
#define DEF_SCHEMA_INFO_FETCHER(OBJECT_NAME, OBJECT_SCHEMA_TYPE) \
int ObSchemaFetcher::fetch_##OBJECT_NAME##_info(const ObRefreshSchemaStatus &schema_status,\
                                                uint64_t object_id, \
                                                int64_t schema_version, \
                                                common::ObIAllocator &allocator, \
                                                OBJECT_SCHEMA_TYPE *&object_schema) \
{ \
  int ret = OB_SUCCESS; \
  object_schema = NULL; \
  if (!check_inner_stat()) { \
    ret = OB_INNER_STAT_ERROR; \
    LOG_WARN("inner stat error", K(ret)); \
  } else if (OB_UNLIKELY(OB_INVALID_ID == object_id) \
             || OB_UNLIKELY(schema_version < 0)) { \
    ret = OB_INVALID_ARGUMENT; \
    LOG_WARN("invalid argument", K(ret), K(object_id), K(schema_version)); \
  } else { \
    OBJECT_SCHEMA_TYPE *tmp_object_schema = NULL; \
    ObArray<uint64_t> tenant_object_ids; \
    ObArray<OBJECT_SCHEMA_TYPE> object_schema_array; \
    if (OB_FAIL(tenant_object_ids.push_back(object_id))) { \
      LOG_WARN("fail to push back object_id for " #OBJECT_NAME, \
               K(object_id), K(ret)); \
    } else if (OB_FAIL(SMART_CALL_LARGE(schema_service_->get_batch_##OBJECT_NAME##s(schema_status, \
                                                                                    schema_version, \
                                                                                    tenant_object_ids, \
                                                                                    *sql_client_, \
                                                                                    object_schema_array)))) { \
      LOG_WARN("fail to get batch " #OBJECT_NAME, \
               K(tenant_object_ids), K(schema_version), K(ret)); \
    } else if (OB_UNLIKELY(1 != object_schema_array.count())) { \
      ret = OB_ERR_UNEXPECTED; \
      LOG_WARN("unexpected schema count", K(object_schema_array), \
               K(object_id), K(schema_version), K(ret)); \
    } else if (OB_FAIL(ObSchemaUtils::alloc_schema(allocator, \
                                                   object_schema_array.at(0), \
                                                   tmp_object_schema))) { \
      LOG_WARN("fail to alloc new var", K(ret)); \
    } else if (OB_ISNULL(tmp_object_schema)) { \
      ret = OB_ERR_UNEXPECTED; \
      LOG_WARN(#OBJECT_NAME "object schema is NULL", K(ret)); \
    } else { \
      object_schema = tmp_object_schema; \
      LOG_TRACE("fetch " #OBJECT_NAME " object info succeed", \
                K(object_id), K(schema_version), K(object_schema)); \
    } \
  } \
  return ret; \
}

  DEF_SCHEMA_INFO_FETCHER(user, ObUserInfo);
  DEF_SCHEMA_INFO_FETCHER(outline, ObOutlineInfo);
  DEF_SCHEMA_INFO_FETCHER(package, ObPackageInfo);
  DEF_SCHEMA_INFO_FETCHER(routine, ObRoutineInfo);
  DEF_SCHEMA_INFO_FETCHER(trigger, ObTriggerInfo);
  DEF_SCHEMA_INFO_FETCHER(udf, ObUDF);
  DEF_SCHEMA_INFO_FETCHER(sequence, ObSequenceSchema);
  DEF_SCHEMA_INFO_FETCHER(mock_fk_parent_table, ObMockFKParentTableSchema);
  DEF_SCHEMA_INFO_FETCHER(ccl_rule, ObCCLRuleSchema);
#undef DEF_SCHEMA_INFO_FETCHER
#endif
}      //end of namespace schema
}      //end of namespace share
}      //end of namespace oceanbase
