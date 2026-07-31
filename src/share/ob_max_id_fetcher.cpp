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

#define USING_LOG_PREFIX SHARE

#include "share/ob_max_id_fetcher.h"

#include "share/ob_sql_client_decorator.h"
#include "share/ob_max_id_cache.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
namespace share
{
using namespace share::schema;

const char *ObMaxIdFetcher::max_id_name_info_[OB_MAX_ID_TYPE][2] = {
  { NULL, NULL },
  { NULL, NULL },
  { NULL, NULL },
  { NULL, NULL },
  { "ob_max_used_server_id", "max used server id"},
  { "ob_max_used_ddl_task_id", "max used ddl task id"},
  { NULL, NULL },
  { "ob_max_used_non_primary_key_table_tablet_id", "ob max used non primary key table tablet id"},
  { "ob_max_used_logstrema_id", "max used log stream id"},
  { "ob_max_used_logstrema_group_id", "max used log stream group id"},
  { "ob_max_used_sys_pl_object_id", "max used sys pl object id"},
  { "ob_max_used_object_id", "max used object id"},
  { "ob_max_used_lock_owner_id", "max used lock owner id"},
  /* Legacy object id types mapped to OB_MAX_USED_OBJECT_ID_TYPE. */
  { "ob_max_used_table_id", "max used table id"},
  { "ob_max_used_database_id", "max used database id"},
  { "ob_max_used_user_id", "max used user id"},
  { "ob_max_used_outline_id", "max used outline id"},
  { "ob_max_used_constraint_id", "max used constraint id"},
  { "ob_max_used_reserved_id", "reserved max id slot"},
  { "ob_max_used_udt_id", "max used udt id"},
  { "ob_max_used_routine_id", "max used routine id"},
  { "ob_max_used_package_id", "max used package id"},
  { "ob_max_used_trigger_id", "max used trigger id"},
  { "ob_max_used_partition_id", "max used partition_id" },
  /* the following ObMaxIdType will be persisted. */
  { "ob_max_used_ai_model_id", "max used ai model id"},
  { "ob_max_used_ai_model_endpoint_id", "max used ai model endpoint id"}
};

lib::ObMutex ObMaxIdFetcher::mutex_;

ObMaxIdFetcher::ObMaxIdFetcher(ObMySQLProxy &proxy)
  : proxy_(proxy),
    max_id_cache_(nullptr),
    group_id_(0)
{
}

ObMaxIdFetcher::ObMaxIdFetcher(ObMySQLProxy &proxy, ObIMaxIdCache *max_id_cache)
  : proxy_(proxy),
    max_id_cache_(max_id_cache),
    group_id_(0)
{
}

ObMaxIdFetcher::ObMaxIdFetcher(ObMySQLProxy &proxy, const int32_t group_id)
  : proxy_(proxy),
    max_id_cache_(nullptr),
    group_id_(group_id)
{
}

ObMaxIdFetcher::~ObMaxIdFetcher()
{
}

int ObMaxIdFetcher::convert_id_type(
    const ObMaxIdType &src,
    ObMaxIdType &dst)
{
  int ret = OB_SUCCESS;
  switch (src) {
    case OB_MAX_USED_SERVER_ID_TYPE:
    case OB_MAX_USED_DDL_TASK_ID_TYPE:
    case OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE:
    case OB_MAX_USED_SYS_PL_OBJECT_ID_TYPE:
    case OB_MAX_USED_OBJECT_ID_TYPE:
    case OB_MAX_USED_LOCK_OWNER_ID_TYPE:
    case OB_MAX_USED_AI_MODEL_ENDPOINT_ID_TYPE: {
      dst = src;
      break;
    }
    case OB_MAX_USED_TABLE_ID_TYPE:
    case OB_MAX_USED_DATABASE_ID_TYPE:
    case OB_MAX_USED_USER_ID_TYPE:
    case OB_MAX_USED_OUTLINE_ID_TYPE:
    case OB_MAX_USED_CONSTRAINT_ID_TYPE:
    case OB_MAX_USED_RESERVED_ID_TYPE:
    case OB_MAX_USED_UDT_ID_TYPE:
    case OB_MAX_USED_ROUTINE_ID_TYPE:
    case OB_MAX_USED_PACKAGE_ID_TYPE:
    case OB_MAX_USED_TRIGGER_ID_TYPE:
    case OB_MAX_USED_PARTITION_ID_TYPE:
    case OB_MAX_USED_AI_MODEL_ID_TYPE: {
      dst = OB_MAX_USED_OBJECT_ID_TYPE;
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported id type", KR(ret), K(src));
      break;
    }
  }
  return ret;
}

int ObMaxIdFetcher::fetch_max_id_from_cache_(ObMaxIdType id_type,
    uint64_t &max_id, const uint64_t size)
{
  int ret = OB_SUCCESS;
  uint64_t min_id = OB_INVALID_ID;
  bool use_cache = false;
  if (OB_ISNULL(max_id_cache_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(check_use_max_id_cache_(id_type, use_cache))) {
    LOG_WARN("failed to check use max id cache", KR(ret), K(id_type));
  } else if (OB_UNLIKELY(!use_cache)) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(max_id_cache_->fetch_max_id(id_type, min_id, size))) {
    LOG_WARN("failed to fetch max id", KR(ret), K(id_type), K(size));
  } else if (FALSE_IT(max_id = min_id + size - 1)) {
  } else if (max_id < min_id) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("id out of range", KR(ret), K(min_id), K(size), K(max_id));
  }
  if (OB_SUCC(ret) && OB_FAIL(check_id_valid(id_type, max_id))) {
    LOG_WARN("invalid max id", KR(ret), K(id_type), K(max_id));
  }
  return ret;
}

// Fetcher for tablet_id only
int ObMaxIdFetcher::fetch_new_max_ids(ObMaxIdType max_id_type,
    uint64_t &id, uint64_t size)
{
  int ret = OB_SUCCESS;
  uint64_t max_id = OB_INVALID_ID;
  if (OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE != max_id_type) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid schema type", K(ret), K(max_id_type));
  } else if (OB_SUCC(fetch_max_id_from_cache_( max_id_type, max_id, size))) {
    LOG_INFO("success to fetch max id from cache", KR(ret));
  } else {
    // ignore error from cache
    LOG_INFO("failed to fetch max id from cache, fetch from inner table instead", KR(ret));
    lib::ObMutexGuard guard(mutex_);
    if (OB_FAIL(fetch_new_max_id( max_id_type, max_id, UINT64_MAX, size))) {
      LOG_WARN("failed to fetch new max id", KR(ret), K(max_id_type), K(max_id), K(size));
    }
  }
  if (OB_SUCC(ret)) {
    id = max_id - size + 1;
  }
  return ret;
}

// Fetcher for object_id only
int ObMaxIdFetcher::fetch_new_max_id(const ObMaxIdType max_id_type,
                                     uint64_t &id,
                                     const uint64_t initial/* = UINT64_MAX */,
                                     const int64_t size/* = 1*/)
{
  int ret = OB_SUCCESS;
  ObMaxIdType fetch_max_id_type = OB_MAX_ID_TYPE;
  bool use_cache = false;
  if (!valid_max_id_type(max_id_type)
      || size < 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(max_id_type), K(size));
  } else if (OB_FAIL(convert_id_type(max_id_type, fetch_max_id_type))) {
    LOG_WARN("fail to convert id type", KR(ret), K(max_id_type));
  } else if (OB_FAIL(check_use_max_id_cache_(fetch_max_id_type, use_cache))) {
    LOG_WARN("failed to check use max id cache", KR(ret), K(fetch_max_id_type), K(max_id_type));
  } else {
    if (use_cache && OB_INVALID_ID == id && 
        OB_SUCC(fetch_max_id_from_cache_( fetch_max_id_type, id, size))) {
      LOG_INFO("succeed to fetch max id from cache", KR(ret), K(id), K(size), K(fetch_max_id_type));
      // ignore error code if fetch from cache failed
    } else if (OB_FAIL(fetch_new_max_id_from_inner_table_(max_id_type, id, initial, size))) {
      LOG_WARN("failed to fetch new max id from inner table", KR(ret), K(max_id_type), K(initial), K(size));
    }
  }
  return ret;
}

int ObMaxIdFetcher::fetch_new_max_id_from_inner_table_(const ObMaxIdType max_id_type, uint64_t &id, const uint64_t initial, const uint64_t size)
{
  int ret = OB_SUCCESS;
  uint64_t fetch_id = OB_INVALID_ID;
  bool need_update = false;
  ObMySQLTransaction trans;
  ObMaxIdType fetch_max_id_type = OB_MAX_ID_TYPE;
  if (!valid_max_id_type(max_id_type)
      || size < 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(max_id_type), K(size));
  } else if (OB_FAIL(convert_id_type(max_id_type, fetch_max_id_type))) {
    LOG_WARN("fail to convert id type", KR(ret), K(max_id_type));
  } else if (OB_FAIL(trans.start(&proxy_, false))) {
    LOG_WARN("fail to to start transaction", K(ret));
  } else if (OB_FAIL(fetch_max_id(trans, fetch_max_id_type, fetch_id))) {
    if (OB_ENTRY_NOT_EXIST == ret && UINT64_MAX != initial) {
      if (OB_FAIL(insert_initial_value(trans, fetch_max_id_type, initial))) {
        LOG_WARN("init initial value failed", K(ret), K(max_id_type), K(fetch_max_id_type), K(initial));
      } else if (OB_FAIL(fetch_max_id(trans, fetch_max_id_type, fetch_id))) {
        LOG_WARN("failed to get max id", K(ret), K(max_id_type), K(fetch_max_id_type));
      }
    } else {
      LOG_WARN("failed to get max id", K(ret), K(max_id_type), K(fetch_max_id_type));
    }
  }
  LOG_INFO("fetch_new_max_id", KR(ret), K(size), K(fetch_id),
           K(max_id_type), K(fetch_max_id_type), K(id), K(initial));

  if (OB_SUCC(ret)) {
    fetch_id += size;

    // update max_id when:
    //  - id is invalid
    //  - id is valid and id>=fetch_id
    if (OB_INVALID_ID == id) {
      id = fetch_id;
      need_update = true;
    } else if (id >= fetch_id) {
      need_update = true;
    }

    // check if new id valid

    if (FAILEDx(check_id_valid(max_id_type, id))) {
      LOG_WARN("failed to check id valid", KR(ret), K(max_id_type), K(id));
    }

    if (OB_FAIL(ret)) {
      //skip
    } else if (need_update) {
      if (OB_FAIL(update_max_id(trans, fetch_max_id_type, id))) {
        LOG_WARN("failed to update max id", K(ret), K(max_id_type), K(fetch_max_id_type), K(id));
      }
    }
  }

  if (trans.is_started()) {
    const bool is_commit = (OB_SUCC(ret));
    int temp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (temp_ret = trans.end(is_commit))) {
      LOG_WARN("failed to end trans", K(is_commit), K(temp_ret));
      ret = (OB_SUCCESS == ret) ? temp_ret : ret;
    }
  }  
  
  return ret;
}

int ObMaxIdFetcher::update_server_max_id(const uint64_t max_server_id, const uint64_t next_max_server_id)
{
  int ret = OB_SUCCESS;
  uint64_t fetched_max_server_id = OB_INVALID_ID;
  ObMySQLTransaction trans;
  if (OB_FAIL(trans.start(&proxy_, false))) {
    LOG_WARN("fail to to start transaction", KR(ret));
  } else if (OB_FAIL(fetch_max_id(trans, OB_MAX_USED_SERVER_ID_TYPE, fetched_max_server_id))) {
    LOG_WARN("failed to get max id", KR(ret));
  } else if (OB_UNLIKELY(max_server_id != fetched_max_server_id)) {
    ret = OB_NEED_RETRY;
    LOG_WARN("max_server_id has been increased, please retry", KR(ret), K(max_server_id), K(fetched_max_server_id));
  } else if (OB_FAIL(update_max_id(trans, OB_MAX_USED_SERVER_ID_TYPE, next_max_server_id))) {
    LOG_WARN("failed to update max id", KR(ret), K(next_max_server_id));
  }

  if (trans.is_started()) {
    const bool is_commit = (OB_SUCC(ret));
    int temp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (temp_ret = trans.end(is_commit))) {
      LOG_WARN("failed to end trans", K(is_commit), K(temp_ret));
      ret = (OB_SUCCESS == ret) ? temp_ret : ret;
    }
  }
  LOG_INFO("update server max id", KR(ret), K(fetched_max_server_id), K(max_server_id), K(next_max_server_id));
  return ret;
}

int ObMaxIdFetcher::check_use_max_id_cache_(const ObMaxIdType &max_id_type, bool &use_cache)
{
  int ret = OB_SUCCESS;
  ObMaxIdType real_type = OB_MAX_ID_TYPE;
  if (OB_FAIL(convert_id_type(max_id_type, real_type))) {
    LOG_WARN("failed to convert_id_type", KR(ret), K(max_id_type));
  } else if (max_id_type != OB_MAX_USED_OBJECT_ID_TYPE && OB_MAX_USED_OBJECT_ID_TYPE == real_type) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("this function should use real type", KR(ret), K(max_id_type));
  } else if (OB_MAX_USED_OBJECT_ID_TYPE == max_id_type 
      || OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE == max_id_type) {
    use_cache = true;
  } else {
    use_cache = false;
  }
  return ret;
}

int ObMaxIdFetcher::update_max_id(ObISQLClient &sql_client,
                                  ObMaxIdType max_id_type, const uint64_t max_id)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0L;
  const char *id_name = NULL;
  
  if (!valid_max_id_type(max_id_type)
      || OB_INVALID_ID == max_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(max_id_type), K(max_id));
  } else if (OB_ISNULL(id_name = get_max_id_name(max_id_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL name", K(ret));
  } else if (OB_FAIL(sql.append_fmt(
      "UPDATE %s SET VALUE = '%lu', gmt_modified = now(6) "
      "WHERE NAME = '%s'",
      OB_ALL_SYS_STAT_TNAME,
      ObSchemaUtils::get_extract_schema_id(max_id),
      id_name))) {
    LOG_WARN("sql_string append format string failed", K(ret));
  } else if (OB_FAIL(sql_client.write(sql.ptr(), group_id_, affected_rows))) {
    LOG_WARN("sql client write fail", K(sql), K(affected_rows), K(ret));
  } else if (!is_single_row(affected_rows)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("unexpected affected row", K(ret), K(affected_rows), K(sql));
  }
  return ret;
}

int ObMaxIdFetcher::fetch_max_id(ObISQLClient &sql_client,
                                 ObMaxIdType max_id_type, uint64_t &max_id)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  const char *id_name = NULL;
  
  bool no_max_id = false;
  if (!valid_max_id_type(max_id_type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(max_id_type));
  } else if (OB_ISNULL(id_name = get_max_id_name(max_id_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL name", K(ret));
  } else if (OB_FAIL(sql.append_fmt(
      "SELECT VALUE FROM %s WHERE NAME = '%s' "
      "FOR UPDATE", OB_ALL_SYS_STAT_TNAME, id_name))) {
    LOG_WARN("sql append format string failed", K(ret));
  } else {
    auto &sql_client_retry_weak = sql_client;
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = NULL;
      ObString id_str;
      if (OB_FAIL(sql_client_retry_weak.read(res, sql.ptr()))) {
        LOG_WARN("execute sql failed", K(sql), K(ret));
      } else if (NULL == (result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to execute sql", K(sql), K(ret));
      } else if (OB_SUCCESS == (ret = result->next())) {
        if (OB_FAIL(result->get_varchar(static_cast<int64_t>(0), id_str))) {
          LOG_WARN("fail to get id as int value.", K(ret));
          result->print_info();
        } else if (OB_FAIL(str_to_uint(id_str, max_id))) {
          LOG_WARN("str_to_uint failed", K(id_str), K(ret));
          // A stored fetch_id may be 0, which requires special treatment.
        } else if (OB_ITER_END != (ret = result->next())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("result is more than one row", K(ret));
        } else {
          ret = OB_SUCCESS;
        }
      } else {
        if (OB_ITER_END == ret) {
          no_max_id = true;
        } else {
          LOG_WARN("fail to get id", "name", id_name, K(ret));
        }
      }
    }
  }

  if (OB_ENTRY_NOT_EXIST == ret) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("4018 not caused by no max id", K(ret));
  } else if (OB_ITER_END == ret && no_max_id) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int ObMaxIdFetcher::insert_initial_value(common::ObISQLClient &sql_client,
      ObMaxIdType max_id_type, const uint64_t initial_value)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObObj obj;
  obj.set_int(static_cast<int64_t>(initial_value));
  int64_t affected_rows = 0;
  const char *name = get_max_id_name(max_id_type);
  const char *info = get_max_id_info(max_id_type);
  
  const uint64_t value = ObSchemaUtils::get_extract_schema_id(initial_value);
  if (!valid_max_id_type(max_id_type) || UINT64_MAX == initial_value) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(max_id_type), K(initial_value));
  } else if (OB_ISNULL(name) || OB_ISNULL(info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL name or info", K(ret), KP(name), KP(info));
  } else if (OB_FAIL(sql.assign_fmt("INSERT INTO %s "
      "(name, data_type, value, info) VALUES "
      "('%s', '%d', '%ld', '%s') ON DUPLICATE KEY UPDATE value = value",
      OB_ALL_SYS_STAT_TNAME,
      name, obj.get_type(),
      static_cast<int64_t>(value), info))) {
    LOG_WARN("sql string assign failed", K(ret));
  } else if (OB_FAIL(sql_client.write(sql.ptr(), group_id_, affected_rows))) {
    LOG_WARN("execute sql failed", K(ret));
  }
  return ret;
}

const char *ObMaxIdFetcher::get_max_id_name(const ObMaxIdType max_id_type)
{
  const char *name = NULL;
  if (max_id_type < 0 || max_id_type >= ARRAYSIZEOF(max_id_name_info_)) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid argument", K(max_id_type), "array size", ARRAYSIZEOF(max_id_name_info_));
  } else {
    name = max_id_name_info_[max_id_type][0];
  }
  return name;
}

const char *ObMaxIdFetcher::get_max_id_info(const ObMaxIdType max_id_type)
{
  const char *info = NULL;
  if (max_id_type < 0 || max_id_type >= ARRAYSIZEOF(max_id_name_info_)) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid argument", K(max_id_type), "array size", ARRAYSIZEOF(max_id_name_info_));
  } else {
    info = max_id_name_info_[max_id_type][1];
  }
  return info;
}

int ObMaxIdFetcher::str_to_uint(const ObString &str, uint64_t &value)
{
  int ret = OB_SUCCESS;
  char buf[2L<<10] = {'\0'};
  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(str));
  } else {
    int n = snprintf(buf, sizeof(buf), "%.*s", str.length(), str.ptr());
    if (n < 0 || n >= sizeof(buf)) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("id_buf is not long enough", K(ret), K(n));
    }
  }
  if (OB_SUCC(ret)) {
    const int64_t base = 10;
    char *endptr = NULL;
    errno = 0;
    unsigned long long ull_value = strtoull(buf, &endptr, base);
    if (errno == ERANGE || (endptr != NULL && *endptr != '\0')) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("convert str to int failed", K(buf), K(ret));
    } else {
      value = static_cast<uint64_t>(ull_value);
    }
  }
  return ret;
}

int ObMaxIdFetcher::check_id_valid(const ObMaxIdType &max_id_type, const uint64_t &id)
{
  int ret = OB_SUCCESS;
  // FIXME: Some columns of object_id are defined as `int`, so we restrict the max avaliable user object_id is INT64_MAX.
  if (id > INT64_MAX) {
    ret = OB_SIZE_OVERFLOW;
    LOG_ERROR("new object_id is reach limit", KR(ret), K(id), K(max_id_type));
  } else {
    switch (max_id_type) {
      case OB_MAX_USED_SERVER_ID_TYPE:
      case OB_MAX_USED_DDL_TASK_ID_TYPE:
      case OB_MAX_USED_LOCK_OWNER_ID_TYPE:
      case OB_MAX_USED_AI_MODEL_ID_TYPE:
      case OB_MAX_USED_AI_MODEL_ENDPOINT_ID_TYPE: {
        // won't check other id
        break;
      }
      case OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE: {
        if (!ObTabletID(id).is_user_normal_rowid_table_tablet()) {
          ret = OB_SIZE_OVERFLOW;
          LOG_ERROR("normal rowid table tablet id reach max", K(ret), K(id));
        }
        break;
      }
      case OB_MAX_USED_UDT_ID_TYPE:
      case OB_MAX_USED_ROUTINE_ID_TYPE:
      case OB_MAX_USED_PACKAGE_ID_TYPE:
      case OB_MAX_USED_TRIGGER_ID_TYPE: {
        //TODO:
        // PL will encode the object_id from schema module with "high 3 bits + low 8bits" to distinguish different objects.
        // To avoid confict, we restrict the available range for PL related object_ids. This logic may be removed in ver 4.1.
        //
        if (id >= OB_MAX_USER_PL_OBJECT_ID || is_inner_object_id(id)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_ERROR("new package/udt/routine/trigger id is invalid", KR(ret), K(id), K(max_id_type));
        }
        break;
      }
      case OB_MAX_USED_SYS_PL_OBJECT_ID_TYPE: {
        // For PL inner objects only
        if (!is_inner_pl_object_id(id)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_ERROR("inner pl object id is invalid", KR(ret), K(id), K(max_id_type));
        }
        break;
      }
      case OB_MAX_USED_TABLE_ID_TYPE: {
        if (is_inner_object_id(id) && !is_inner_table(id)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_ERROR("inner table_id is invalid", KR(ret), K(id), K(max_id_type));
        }
        break;
      }
      case OB_MAX_USED_USER_ID_TYPE: {
        if (is_inner_object_id(id) && !is_inner_user_or_role(id)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_ERROR("inner user_id/role_id is invalid", KR(ret), K(id), K(max_id_type));
        }
        break;
      }
      case OB_MAX_USED_DATABASE_ID_TYPE: {
        if (is_inner_object_id(id) && !is_inner_db(id)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_ERROR("inner database_id is invalid", KR(ret), K(id), K(max_id_type));
        }
        break;
      }
      default: {
        if (is_inner_object_id(id)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_ERROR("user object_id is invalid", KR(ret), K(id), K(max_id_type));
        }
        break;
      }
    }
  }
  return ret;
}

int ObMaxIdFetcher::batch_fetch_new_max_id_from_inner_table(ObMaxIdType id_type, uint64_t &max_id, const uint64_t size)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  uint64_t fetched_max_id = OB_INVALID_ID;
  if (OB_FAIL(trans.start(&proxy_))) {
    LOG_WARN("failed to start trans", KR(ret));
  } else if (OB_FAIL(fetch_max_id(trans, id_type, fetched_max_id))) {
    LOG_WARN("failed to fetch max id", KR(ret), K(id_type));
  } else if (FALSE_IT(max_id = fetched_max_id + size)) {
  } else if (OB_INVALID_ID == fetched_max_id || max_id < fetched_max_id) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("invalid max_id", KR(ret), K(max_id), K(size), K(fetch_max_id));
  } else if (OB_FAIL(update_max_id(trans, id_type, max_id))) {
    LOG_WARN("failed to update max id", KR(ret), K(id_type));
  }
  if (trans.is_started()) {
    const bool is_commit = (OB_SUCC(ret));
    int temp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (temp_ret = trans.end(is_commit))) {
      LOG_WARN("failed to end trans", K(is_commit), K(temp_ret));
      ret = (OB_SUCCESS == ret) ? temp_ret : ret;
    }
  }
  return ret;
}
}//end namespace share
}//end namespace oceanbase
