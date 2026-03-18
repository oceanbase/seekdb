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

#ifndef OCEANBASE_SHARE_STORAGE_OB_SQLITE_CONNECTION_POOL_H_
#define OCEANBASE_SHARE_STORAGE_OB_SQLITE_CONNECTION_POOL_H_

#include "lib/ob_define.h"
#include "lib/container/ob_iarray.h"
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_string.h"
#include "share/storage/ob_sqlite_connection.h"
#include <functional>
#include <stdint.h>
#include <string.h>

namespace oceanbase
{
namespace share
{

// Forward declaration
class ObSQLiteConnectionPool;

// RAII wrapper for connection acquisition/release
class ObSQLiteConnectionGuard
{
public:
  // Default constructor (creates empty guard)
  ObSQLiteConnectionGuard() : pool_(nullptr), conn_(nullptr) {}

  // Constructor with pool (acquires connection)
  ObSQLiteConnectionGuard(ObSQLiteConnectionPool *pool);

  // Move constructor
  ObSQLiteConnectionGuard(ObSQLiteConnectionGuard &&other) noexcept
    : pool_(other.pool_), conn_(other.conn_)
  {
    // Clear source object to prevent it from releasing connection in destructor
    other.pool_ = nullptr;
    other.conn_ = nullptr;
  }

  // Move assignment operator
  ObSQLiteConnectionGuard &operator=(ObSQLiteConnectionGuard &&other) noexcept
  {
    if (this != &other) {
      // Release current connection first
      release();
      // Move resources
      pool_ = other.pool_;
      conn_ = other.conn_;
      // Clear source object
      other.pool_ = nullptr;
      other.conn_ = nullptr;
    }
    return *this;
  }

  ~ObSQLiteConnectionGuard();

  // Get the connection (nullptr if acquisition failed)
  ObSQLiteConnection *get_connection() const { return conn_; }

  // Check if connection is valid
  bool is_valid() const { return nullptr != conn_; }

  // Explicitly release connection early (normally done in destructor)
  void release();

  // Operator overloads for convenience
  // operator->() allows direct access to connection methods: guard->query(...)
  ObSQLiteConnection *operator->() const { return conn_; }

  // operator bool() allows if (guard) instead of if (guard.is_valid())
  explicit operator bool() const { return nullptr != conn_; }

  // Disable copy
  DISALLOW_COPY_AND_ASSIGN(ObSQLiteConnectionGuard);

private:
  ObSQLiteConnectionPool *pool_;
  ObSQLiteConnection *conn_;
};

// SQLite connection pool
// Manages connection acquisition/release, does not execute SQL directly
// First version: always creates a new connection (simple implementation)
// Future: can be extended to get connection from pool
class ObSQLiteConnectionPool
{
public:
  ObSQLiteConnectionPool();
  virtual ~ObSQLiteConnectionPool();

  // Initialize with database path
  int init(const char *db_path);

  void destroy();
  bool is_inited() const { return strlen(db_path_) > 0; }

  // Acquire a connection for executing operations
  // @param conn: output parameter for acquired connection
  // @return OB_SUCCESS on success
  int acquire_connection(ObSQLiteConnection *&conn);

  // Release a connection
  void release_connection(ObSQLiteConnection *conn);

private:
  char db_path_[OB_MAX_FILE_NAME_LENGTH];
  DISALLOW_COPY_AND_ASSIGN(ObSQLiteConnectionPool);

  friend class ObSQLiteConnectionGuard;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_SQLITE_CONNECTION_POOL_H_
