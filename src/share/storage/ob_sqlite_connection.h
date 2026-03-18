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

#ifndef OCEANBASE_SHARE_STORAGE_OB_SQLITE_CONNECTION_H_
#define OCEANBASE_SHARE_STORAGE_OB_SQLITE_CONNECTION_H_

#include "lib/ob_define.h"
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_string.h"
#include "lib/allocator/page_arena.h"
#include <functional>
#include <stdint.h>

// Forward declaration - hide SQLite types from user
struct sqlite3_stmt;
struct sqlite3;

namespace oceanbase
{
namespace common
{
class ObArenaAllocator;
} // namespace common

namespace share
{

// Type alias to hide SQLite types from user
using ObSQLiteStmt = sqlite3_stmt;

// Parameter binder - hides sqlite3_bind_* API
// Default behavior: auto-increment parameter index (starts from 1, SQLite convention)
// Can also specify explicit index if needed
class ObSQLiteBinder
{
public:
  ObSQLiteBinder(ObSQLiteStmt *stmt);

  // Default: auto-increment parameter index (starts from 1)
  int bind_int(int32_t value);
  int bind_int64(int64_t value);
  int bind_text(const char *value);  // Auto-length using strlen()
  int bind_text(const char *value, int value_len);
  int bind_blob(const void *value);  // Auto-length using strlen() - for null-terminated binary data
  int bind_blob(const void *value, int value_len);

  // Explicit parameter index (1-based, SQLite convention)
  int bind_int(int param_idx, int32_t value);
  int bind_int64(int param_idx, int64_t value);
  int bind_text(int param_idx, const char *value);  // Auto-length using strlen()
  int bind_text(int param_idx, const char *value, int value_len);
  int bind_blob(int param_idx, const void *value);  // Auto-length using strlen() - for null-terminated binary data
  int bind_blob(int param_idx, const void *value, int value_len);

  // Reset parameter index to 1
  void reset() { param_idx_ = 1; }

private:
  ObSQLiteStmt *stmt_;
  int param_idx_;
};

// Row reader - hides sqlite3_column_* API
// Default behavior: auto-increment column index (starts from 0, SQLite convention)
// Can also specify explicit index if needed
class ObSQLiteRowReader
{
public:
  // Default constructor (for use with step_query)
  ObSQLiteRowReader()
    : stmt_(nullptr), col_idx_(0), row_buffer_(nullptr) {}

  // Set statement and optional allocator (for use after default construction)
  void set_stmt(ObSQLiteStmt *stmt, common::ObArenaAllocator *row_buffer = nullptr)
  {
    stmt_ = stmt;
    col_idx_ = 0;
    row_buffer_ = row_buffer;
  }

  // Default: auto-increment column index (starts from 0)
  int64_t get_int64();
  int32_t get_int();
  const char *get_text(int *len = nullptr);
  ObString get_string();
  const void *get_blob(int *len = nullptr);

  // Explicit column index (0-based, SQLite convention)
  int64_t get_int64(int col_idx) const;
  int32_t get_int(int col_idx) const;
  const char *get_text(int col_idx, int *len = nullptr) const;
  ObString get_string(int col_idx) const;
  const void *get_blob(int col_idx, int *len = nullptr) const;

  // Reset column index to 0
  void reset() { col_idx_ = 0; }

private:
  // Private allocator wrapper that retries on failure
  void *alloc_with_retry(int64_t size) const;

private:
  ObSQLiteStmt *stmt_;
  int col_idx_;
  common::ObArenaAllocator *row_buffer_;  // Shared allocator for row data caching
};

// SQLite connection wrapper - encapsulates a single database connection
// Used to execute queries and operations on a specific connection
class ObSQLiteConnection
{
public:
  ObSQLiteConnection();
  ~ObSQLiteConnection();

  // Initialize connection with database path
  int init(const char *db_path);

  // Check if connection is valid
  bool is_valid() const { return nullptr != db_; }

  // Get underlying sqlite3 connection (for internal use)
  struct sqlite3 *get_db() const { return db_; }

  // Query data from database
  int query(
      const char *sql,
      const std::function<int(ObSQLiteBinder &)> &binder,
      const std::function<int(ObSQLiteRowReader &)> &row_processor);

  // Prepare a query for iterative execution (returns statement handle)
  // Caller is responsible for calling step_query() and finalize_query()
  int prepare_query(
      const char *sql,
      const std::function<int(ObSQLiteBinder &)> &binder,
      ObSQLiteStmt *&stmt);

  // Step a prepared query statement (returns OB_SUCCESS for row, OB_ITER_END when done)
  int step_query(ObSQLiteStmt *stmt, ObSQLiteRowReader &reader);

  // Finalize a prepared query statement
  void finalize_query(ObSQLiteStmt *stmt);

  // Execute SQL statement (for INSERT, UPDATE, DELETE)
  // If in a transaction, executes within the transaction; otherwise executes standalone
  int execute(
      const char *sql,
      const std::function<int(ObSQLiteBinder &)> &binder = nullptr,
      int64_t *affected_rows = nullptr);

  // Transaction management (for multi-table updates)
  int begin_transaction();
  int commit();
  int rollback();
  bool is_in_transaction() const;

  // Prepare a batch execute statement
  // Note: Does NOT begin transaction automatically. Use begin_transaction() explicitly if needed.
  // Caller is responsible for calling step_execute() and finalize_execute()
  int prepare_execute(const char *sql, ObSQLiteStmt *&stmt);

  // Step a prepared execute statement (returns OB_SUCCESS on success, OB_ITER_END when done)
  int step_execute(
      ObSQLiteStmt *stmt,
      const std::function<int(ObSQLiteBinder &)> &binder = nullptr,
      int64_t *affected_rows = nullptr);

  // Finalize a prepared execute statement (only finalizes the statement, does NOT commit/rollback)
  // Use commit() or rollback() explicitly to end transaction
  void finalize_execute(ObSQLiteStmt *stmt);

  // Close connection
  void close();

private:
  // Configure SQLite connection for better concurrency and performance
  int configure_connection(struct sqlite3 *db);

  struct sqlite3 *db_;
  common::ObArenaAllocator row_buffer_;  // Shared allocator for row data caching
  DISALLOW_COPY_AND_ASSIGN(ObSQLiteConnection);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_SQLITE_CONNECTION_H_
