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
    void *buf = ob_malloc(sizeof(ObSQLiteConnection), "SQLiteConn");
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
