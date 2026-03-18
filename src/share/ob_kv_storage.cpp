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

#include "ob_kv_storage.h"
#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include "share/storage/ob_sqlite_connection.h"
#include "share/storage/ob_sqlite_table_schema.h"
#include <string.h>

namespace oceanbase
{
namespace share
{

ObKVStorage::ObKVStorage()
  : pool_(nullptr)
{
}

ObKVStorage::~ObKVStorage()
{
}

int ObKVStorage::init(ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_ = pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid pool", K(ret));
  } else if (OB_FAIL(create_table_if_not_exists())) {
    LOG_WARN("failed to create table", K(ret));
  }
  if (OB_FAIL(ret)) {
    pool_ = NULL;
  }
  return ret;
}

int ObKVStorage::create_table_if_not_exists()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("pool not set", K(ret));
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_KV_TABLE, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObKVStorage::get(const common::ObString &key, common::ObString &value)
{
  int ret = OB_SUCCESS;
  value.reset();

  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (key.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid key", K(ret));
  } else {
    const char *select_sql = "SELECT value FROM __all_kv_table WHERE key = ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      return b.bind_text(key.ptr(), key.length());
    };

    bool found = false;
    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      const char *value_str = reader.get_text();
      if (nullptr != value_str) {
        value.assign_ptr(value_str, static_cast<int32_t>(strlen(value_str)));
        found = true;
      }
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to query", K(ret), K(key));
      } else {
        ret = OB_ENTRY_NOT_EXIST;
        LOG_DEBUG("key not found", K(key));
      }
    } else if (!found) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_DEBUG("key not found", K(key));
    }
  }
  return ret;
}

int ObKVStorage::set(const common::ObString &key, const common::ObString &value)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (key.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid key", K(ret));
  } else {
    const char *upsert_sql =
      "INSERT OR REPLACE INTO __all_kv_table "
      "(key, value, gmt_create, gmt_modified) "
      "VALUES (?, ?, ?, ?);";

    int64_t current_time = ObTimeUtility::current_time();

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_text(key.ptr(), key.length());
      b.bind_text(value.ptr(), value.length());
      b.bind_int64(current_time);
      b.bind_int64(current_time);
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(upsert_sql, binder))) {
      LOG_WARN("failed to execute upsert", K(ret), K(key));
    } else {
      LOG_DEBUG("set kv success", K(key));
    }
  }
  return ret;
}

int ObKVStorage::del(const common::ObString &key)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (key.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid key", K(ret));
  } else {
    const char *delete_sql = "DELETE FROM __all_kv_table WHERE key = ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      return b.bind_text(key.ptr(), key.length());
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(delete_sql, binder))) {
      LOG_WARN("failed to execute delete", K(ret), K(key));
    } else {
      LOG_DEBUG("delete kv success", K(key));
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
