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

#include "share/storage/ob_sqlite_connection_pool.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "lib/allocator/ob_malloc.h"
#include <sqlite/sqlite3.h>
#include <string.h>

namespace oceanbase
{
namespace share
{

// ObSQLiteBinder implementation
ObSQLiteBinder::ObSQLiteBinder(ObSQLiteStmt *stmt)
  : stmt_(stmt), param_idx_(1)
{
}

// Default: auto-increment
int ObSQLiteBinder::bind_int(int32_t value)
{
  return bind_int(param_idx_++, value);
}

int ObSQLiteBinder::bind_int64(int64_t value)
{
  return bind_int64(param_idx_++, value);
}

int ObSQLiteBinder::bind_text(const char *value, int value_len)
{
  return bind_text(param_idx_++, value, value_len);
}

int ObSQLiteBinder::bind_text(const ObString &value)
{
  return bind_text(param_idx_++, value);
}

// Explicit index
int ObSQLiteBinder::bind_int(int param_idx, int32_t value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid statement", K(ret));
  } else {
    int sqlite_ret = sqlite3_bind_int(stmt_, param_idx, value);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to bind int", K(ret), K(param_idx), K(value));
    }
  }
  return ret;
}

int ObSQLiteBinder::bind_int64(int param_idx, int64_t value)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid statement", K(ret));
  } else {
    int sqlite_ret = sqlite3_bind_int64(stmt_, param_idx, value);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to bind int64", K(ret), K(param_idx), K(value));
    }
  }
  return ret;
}

int ObSQLiteBinder::bind_text(int param_idx, const char *value, int value_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid statement", K(ret));
  } else if (OB_ISNULL(value) && value_len > 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid value", K(ret), KP(value), K(value_len));
  } else {
    int sqlite_ret = sqlite3_bind_text(stmt_, param_idx, value, value_len, SQLITE_STATIC);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to bind text", K(ret), K(param_idx), KP(value), K(value_len));
    }
  }
  return ret;
}

int ObSQLiteBinder::bind_text(int param_idx, const ObString &value)
{
  return bind_text(param_idx, value.ptr(), value.length());
}

// ObSQLiteRowReader implementation
ObSQLiteRowReader::ObSQLiteRowReader(ObSQLiteStmt *stmt)
  : stmt_(stmt), col_idx_(0)
{
}

// Default: auto-increment
int64_t ObSQLiteRowReader::get_int64()
{
  return get_int64(col_idx_++);
}

int32_t ObSQLiteRowReader::get_int()
{
  return get_int(col_idx_++);
}

const char *ObSQLiteRowReader::get_text(int *len)
{
  return get_text(col_idx_++, len);
}

ObString ObSQLiteRowReader::get_string()
{
  return get_string(col_idx_++);
}

// Explicit index
int64_t ObSQLiteRowReader::get_int64(int col_idx) const
{
  if (OB_ISNULL(stmt_)) {
    return 0;
  }
  return sqlite3_column_int64(stmt_, col_idx);
}

int32_t ObSQLiteRowReader::get_int(int col_idx) const
{
  if (OB_ISNULL(stmt_)) {
    return 0;
  }
  return sqlite3_column_int(stmt_, col_idx);
}

const char *ObSQLiteRowReader::get_text(int col_idx, int *len) const
{
  if (OB_ISNULL(stmt_)) {
    return nullptr;
  }
  const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt_, col_idx));
  if (nullptr != len) {
    *len = sqlite3_column_bytes(stmt_, col_idx);
  }
  return text;
}

ObString ObSQLiteRowReader::get_string(int col_idx) const
{
  if (OB_ISNULL(stmt_)) {
    return ObString();
  }
  const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt_, col_idx));
  int len = sqlite3_column_bytes(stmt_, col_idx);
  return ObString(len, text);
}

// ObSQLiteConnectionGuard implementation
ObSQLiteConnectionGuard::ObSQLiteConnectionGuard(ObSQLiteConnectionPool *pool)
  : pool_(pool), conn_(nullptr)
{
  if (nullptr != pool_) {
    pool_->acquire_connection(conn_);
  }
}

ObSQLiteConnectionGuard::~ObSQLiteConnectionGuard()
{
  release();
}

void ObSQLiteConnectionGuard::release()
{
  if (nullptr != pool_ && nullptr != conn_) {
    pool_->release_connection(conn_);
    conn_ = nullptr;
  }
}

ObSQLiteConnectionPool::ObSQLiteConnectionPool()
{
  db_path_[0] = '\0';
}

ObSQLiteConnectionPool::~ObSQLiteConnectionPool()
{
  destroy();
}

int ObSQLiteConnectionPool::init(const char *db_path)
{
  int ret = OB_SUCCESS;
  if (strlen(db_path_) > 0) {
    ret = OB_INIT_TWICE;
    LOG_WARN("sqlite table storage already inited", K(ret));
  } else if (OB_ISNULL(db_path) || strlen(db_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid db_path", K(ret), KP(db_path));
  } else {
    snprintf(db_path_, OB_MAX_FILE_NAME_LENGTH, "%s", db_path);
    // Tables are created by specific storage classes (ObConfigStorage, ObTabletMetaTableStorage)
    // This class only manages the database connection
    LOG_INFO("sqlite table storage init success", K(db_path));
  }
  return ret;
}

int ObSQLiteConnectionPool::acquire_connection(ObSQLiteConnection *&conn)
{
  int ret = OB_SUCCESS;
  conn = nullptr;

  if (strlen(db_path_) == 0) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // First version: always create a new connection
    // Future: can get from pool
    void *buf = ob_malloc(sizeof(ObSQLiteConnection), "SQLiteConnection");
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory for connection", K(ret));
    } else {
      conn = new(buf) ObSQLiteConnection();
      if (OB_FAIL(conn->init(db_path_))) {
        LOG_WARN("failed to init connection", K(ret));
        conn->~ObSQLiteConnection();
        ob_free(buf);
        conn = nullptr;
      }
    }
  }

  return ret;
}

void ObSQLiteConnectionPool::release_connection(ObSQLiteConnection *conn)
{
  if (nullptr != conn) {
    // First version: just close and free the connection
    // Future: return to pool
    conn->close();
    conn->~ObSQLiteConnection();
    ob_free(conn);
  }
}

void ObSQLiteConnectionPool::destroy()
{
  db_path_[0] = '\0';
}


} // namespace share
} // namespace oceanbase
