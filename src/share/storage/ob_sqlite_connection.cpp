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

#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_macro_utils.h"  // For MEMCPY
#include "lib/utility/utility.h"  // For ob_usleep
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

int ObSQLiteBinder::bind_int(int32_t value)
{
  return bind_int(param_idx_++, value);
}

int ObSQLiteBinder::bind_int64(int64_t value)
{
  return bind_int64(param_idx_++, value);
}

int ObSQLiteBinder::bind_text(const char *value)
{
  if (OB_ISNULL(value)) {
    return bind_text(param_idx_++, value, 0);
  } else {
    return bind_text(param_idx_++, value, static_cast<int>(strlen(value)));
  }
}

int ObSQLiteBinder::bind_text(const char *value, int value_len)
{
  return bind_text(param_idx_++, value, value_len);
}

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

int ObSQLiteBinder::bind_text(int param_idx, const char *value)
{
  if (OB_ISNULL(value)) {
    return bind_text(param_idx, value, 0);
  } else {
    return bind_text(param_idx, value, static_cast<int>(strlen(value)));
  }
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
    int sqlite_ret = sqlite3_bind_text(stmt_, param_idx, value, value_len, SQLITE_TRANSIENT);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to bind text", K(ret), K(param_idx), KP(value), K(value_len));
    }
  }
  return ret;
}

int ObSQLiteBinder::bind_blob(const void *value, int value_len)
{
  return bind_blob(param_idx_++, value, value_len);
}

int ObSQLiteBinder::bind_blob(int param_idx, const void *value, int value_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid statement", K(ret));
  } else if (OB_ISNULL(value) && value_len > 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid value", K(ret), KP(value), K(value_len));
  } else {
    int sqlite_ret = sqlite3_bind_blob(stmt_, param_idx, value, value_len, SQLITE_TRANSIENT);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to bind blob", K(ret), K(param_idx), KP(value), K(value_len));
    }
  }
  return ret;
}

// ObSQLiteRowReader implementation
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

void *ObSQLiteRowReader::alloc_with_retry(int64_t size) const
{
  OB_ASSERT(OB_NOT_NULL(row_buffer_));
  void *buf = nullptr;
  while (OB_ISNULL(buf = row_buffer_->alloc(size))) {
    // Allocation failed, retry after 10ms
    ob_usleep(10000);  // 10ms = 10000 microseconds
  }
  return buf;
}

const char *ObSQLiteRowReader::get_text(int col_idx, int *len) const
{
  const char *ret = nullptr;
  int text_len = 0;

  if (OB_ISNULL(stmt_)) {
    if (nullptr != len) {
      *len = 0;
    }
  } else {
    // Read raw data from SQLite
    const char *text = reinterpret_cast<const char *>(sqlite3_column_text(stmt_, col_idx));
    text_len = sqlite3_column_bytes(stmt_, col_idx);

    if (nullptr == text || text_len <= 0) {
      if (nullptr != len) {
        *len = 0;
      }
    } else {
      // Copy data using retry allocator (never fails)
      // Allocate text_len + 1 bytes to include null terminator
      char *buf = static_cast<char *>(alloc_with_retry(text_len + 1));
      OB_ASSERT(OB_NOT_NULL(buf));
      MEMCPY(buf, text, text_len);
      buf[text_len] = '\0';  // Add null terminator
      ret = buf;
      if (nullptr != len) {
        *len = text_len;
      }
    }
  }

  return ret;
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

const void *ObSQLiteRowReader::get_blob(int *len)
{
  return get_blob(col_idx_++, len);
}

const void *ObSQLiteRowReader::get_blob(int col_idx, int *len) const
{
  const void *ret = nullptr;
  int blob_len = 0;

  if (OB_ISNULL(stmt_)) {
    if (nullptr != len) {
      *len = 0;
    }
  } else {
    // Read raw data from SQLite
    const void *blob = sqlite3_column_blob(stmt_, col_idx);
    blob_len = sqlite3_column_bytes(stmt_, col_idx);

    if (nullptr == blob || blob_len <= 0) {
      if (nullptr != len) {
        *len = 0;
      }
    } else {
      // Copy data using retry allocator (never fails)
      void *buf = alloc_with_retry(blob_len);
      OB_ASSERT(OB_NOT_NULL(buf));
      MEMCPY(buf, blob, blob_len);
      ret = buf;
      if (nullptr != len) {
        *len = blob_len;
      }
    }
  }

  return ret;
}

ObSQLiteConnection::ObSQLiteConnection()
  : db_(nullptr), row_buffer_("SQLiteRowBuffer")
{
}

ObSQLiteConnection::~ObSQLiteConnection()
{
  close();
}

int ObSQLiteConnection::configure_connection(struct sqlite3 *db)
{
  int ret = OB_SUCCESS;
  char *err_msg = nullptr;

  // Enable WAL mode for better concurrency (multiple readers, one writer)
  // If database is locked (e.g., another connection is setting WAL mode),
  // this is not a fatal error - we can continue with the current mode
  int sqlite_ret = sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, &err_msg);
  if (SQLITE_OK != sqlite_ret) {
    const char *err_str = err_msg ? err_msg : sqlite3_errmsg(db);
    if (nullptr != err_str && nullptr != strstr(err_str, "locked")) {
      LOG_INFO("database is locked when enabling WAL mode, will use current mode",
          "sqlite_err", err_str);
      ret = OB_SUCCESS;  // Not a fatal error, continue
    } else {
      ret = OB_ERROR;
      LOG_WARN("failed to enable WAL mode", K(ret), "sqlite_err", err_str);
    }
    if (err_msg) {
      sqlite3_free(err_msg);
      err_msg = nullptr;
    }
  }

  if (OB_SUCC(ret)) {
    // Set synchronous mode to NORMAL for better performance
    // If database is locked (e.g., another connection is setting synchronous mode),
    // this is not a fatal error - we can continue with the current mode
    sqlite_ret = sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, &err_msg);
    if (SQLITE_OK != sqlite_ret) {
      const char *err_str = err_msg ? err_msg : sqlite3_errmsg(db);
      if (nullptr != err_str && nullptr != strstr(err_str, "locked")) {
        LOG_INFO("database is locked when setting synchronous mode, will use current mode",
            "sqlite_err", err_str);
        // Not a fatal error, continue
      } else {
        LOG_WARN("failed to set synchronous mode", K(sqlite_ret), "sqlite_err", err_str);
      }
      if (err_msg) {
        sqlite3_free(err_msg);
        err_msg = nullptr;
      }
    }

    // Set busy timeout
    sqlite3_busy_timeout(db, 5000);  // 5 seconds

    // Increase cache size
    sqlite_ret = sqlite3_exec(db, "PRAGMA cache_size=-65536", nullptr, nullptr, &err_msg);
    if (SQLITE_OK != sqlite_ret) {
      LOG_WARN("failed to set cache size", K(sqlite_ret), "sqlite_err", err_msg ? err_msg : sqlite3_errmsg(db));
      if (err_msg) {
        sqlite3_free(err_msg);
      }
    }
  }

  return ret;
}

int ObSQLiteConnection::init(const char *db_path)
{
  int ret = OB_SUCCESS;
  if (nullptr != db_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("connection already inited", K(ret));
  } else if (OB_ISNULL(db_path) || strlen(db_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid db_path", K(ret), KP(db_path));
  } else {
    int sqlite_ret = sqlite3_open(db_path, &db_);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_ERROR("failed to open sqlite database", K(ret), K(db_path),
                "sqlite_err", sqlite3_errmsg(db_));
      if (nullptr != db_) {
        sqlite3_close(db_);
        db_ = nullptr;
      }
    } else {
      if (OB_FAIL(configure_connection(db_))) {
        LOG_WARN("failed to configure connection", K(ret));
        sqlite3_close(db_);
        db_ = nullptr;
      }
    }
  }
  return ret;
}


void ObSQLiteConnection::close()
{
  if (nullptr != db_) {
    // Rollback any pending transaction
    if (is_in_transaction()) {
      int ret = rollback();
      if (OB_SUCCESS != ret) {
        LOG_WARN("failed to rollback transaction on close", K(ret));
      }
    }
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

int ObSQLiteConnection::query(
    const char *sql,
    const std::function<int(ObSQLiteBinder &)> &binder,
    const std::function<int(ObSQLiteRowReader &)> &row_processor)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(sql)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sql", K(ret));
  } else {
    ObSQLiteStmt *stmt = nullptr;
    // Use prepare_query
    if (OB_FAIL(prepare_query(sql, binder, stmt))) {
      LOG_WARN("failed to prepare query", K(ret));
    } else if (OB_NOT_NULL(stmt)) {
      if (row_processor) {
        // Process rows using step_query
        bool has_row = false;
        ObSQLiteRowReader reader;  // Use default constructor
        while (OB_SUCC(ret)) {
          ret = step_query(stmt, reader);  // Internally calls reuse() and set_stmt()
          if (OB_SUCC(ret)) {
            has_row = true;
            ret = row_processor(reader);
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            }
          } else if (OB_ITER_END == ret) {
            if (!has_row) {
              ret = OB_ENTRY_NOT_EXIST;
            } else {
              ret = OB_SUCCESS;
            }
            break;
          } else {
            LOG_WARN("failed to step query", K(ret));
            break;
          }
        }
      } else {
        // No row processor, just execute once
        int sqlite_ret = sqlite3_step(stmt);
        if (SQLITE_DONE != sqlite_ret && SQLITE_ROW != sqlite_ret) {
          ret = OB_ERROR;
          LOG_WARN("failed to execute statement", K(ret), "sqlite_err", sqlite3_errmsg(db_));
        }
      }
      // Use finalize_query
      finalize_query(stmt);
    }
  }

  return ret;
}

int ObSQLiteConnection::prepare_query(
    const char *sql,
    const std::function<int(ObSQLiteBinder &)> &binder,
    ObSQLiteStmt *&stmt)
{
  int ret = OB_SUCCESS;
  stmt = nullptr;

  if (OB_ISNULL(sql)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sql", K(ret));
  } else if (OB_ISNULL(db_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection not initialized", K(ret));
  } else {
    int sqlite_ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to prepare statement", K(ret), K(sql), "sqlite_err", sqlite3_errmsg(db_));
    } else if (binder) {
      // Bind parameters if binder is provided
      ObSQLiteBinder sqlite_binder(stmt);
      if (OB_FAIL(binder(sqlite_binder))) {
        LOG_WARN("failed to bind parameters", K(ret));
        sqlite3_finalize(stmt);
        stmt = nullptr;
      }
    }
  }

  return ret;
}

int ObSQLiteConnection::step_query(ObSQLiteStmt *stmt, ObSQLiteRowReader &reader)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid statement", K(ret));
  } else if (OB_ISNULL(db_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection not initialized", K(ret));
  } else {
    int sqlite_ret = sqlite3_step(stmt);
    if (SQLITE_ROW == sqlite_ret) {
      row_buffer_.reuse();  // Clear previous row data, but keep memory pool
      reader.set_stmt(stmt, &row_buffer_);  // Set statement and shared allocator
      // Return OB_SUCCESS to indicate row available
    } else if (SQLITE_DONE == sqlite_ret) {
      ret = OB_ITER_END;
    } else {
      ret = OB_ERROR;
      LOG_WARN("failed to step statement", K(ret), "sqlite_err", sqlite3_errmsg(db_));
    }
  }

  return ret;
}

void ObSQLiteConnection::finalize_query(ObSQLiteStmt *stmt)
{
  if (nullptr != stmt) {
    sqlite3_finalize(stmt);
  }
}

int ObSQLiteConnection::execute(
    const char *sql,
    const std::function<int(ObSQLiteBinder &)> &binder,
    int64_t *affected_rows)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(sql)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sql", K(ret));
  } else {
    ObSQLiteStmt *stmt = nullptr;
    // Use prepare_execute
    if (OB_FAIL(prepare_execute(sql, stmt))) {
      LOG_WARN("failed to prepare execute", K(ret));
    } else if (OB_NOT_NULL(stmt)) {
      // Use step_execute
      ret = step_execute(stmt, binder, affected_rows);
      // Use finalize_execute
      finalize_execute(stmt);
    }
  }

  return ret;
}

bool ObSQLiteConnection::is_in_transaction() const
{
  // sqlite3_get_autocommit returns non-zero if NOT in transaction (autocommit mode)
  // returns zero if in transaction
  return db_ != nullptr && sqlite3_get_autocommit(db_) == 0;
}

int ObSQLiteConnection::begin_transaction()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(db_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection not initialized", K(ret));
  } else if (is_in_transaction()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("already in transaction", K(ret));
  } else {
    char *err_msg = nullptr;
    int sqlite_ret = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &err_msg);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to begin transaction", K(ret), "sqlite_err", err_msg ? err_msg : sqlite3_errmsg(db_));
      if (err_msg) {
        sqlite3_free(err_msg);
      }
    }
  }

  return ret;
}

int ObSQLiteConnection::commit()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(db_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection not initialized", K(ret));
  } else if (!is_in_transaction()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not in transaction", K(ret));
  } else {
    char *err_msg = nullptr;
    int sqlite_ret = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &err_msg);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to commit transaction", K(ret), "sqlite_err", err_msg ? err_msg : sqlite3_errmsg(db_));
      if (err_msg) {
        sqlite3_free(err_msg);
      }
    }
  }

  return ret;
}

int ObSQLiteConnection::rollback()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(db_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection not initialized", K(ret));
  } else if (!is_in_transaction()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not in transaction", K(ret));
  } else {
    char *err_msg = nullptr;
    int sqlite_ret = sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, &err_msg);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to rollback transaction", K(ret), "sqlite_err", err_msg ? err_msg : sqlite3_errmsg(db_));
      if (err_msg) {
        sqlite3_free(err_msg);
      }
    }
  }

  return ret;
}

int ObSQLiteConnection::prepare_execute(const char *sql, ObSQLiteStmt *&stmt)
{
  int ret = OB_SUCCESS;
  stmt = nullptr;

  if (OB_ISNULL(sql)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sql", K(ret));
  } else if (OB_ISNULL(db_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection not initialized", K(ret));
  } else {
    // Prepare statement (does NOT begin transaction automatically)
    int sqlite_ret = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (SQLITE_OK != sqlite_ret) {
      ret = OB_ERROR;
      LOG_WARN("failed to prepare statement", K(ret), K(sql), "sqlite_err", sqlite3_errmsg(db_));
    }
  }

  return ret;
}

int ObSQLiteConnection::step_execute(
    ObSQLiteStmt *stmt,
    const std::function<int(ObSQLiteBinder &)> &binder,
    int64_t *affected_rows)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid statement", K(ret));
  } else if (OB_ISNULL(db_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("connection not initialized", K(ret));
  } else {
    // Reset statement for next iteration
    sqlite3_reset(stmt);

    // Bind parameters if binder provided
    if (binder) {
      ObSQLiteBinder sqlite_binder(stmt);
      ret = binder(sqlite_binder);
      if (OB_ITER_END == ret) {
        return OB_ITER_END;
      } else if (OB_FAIL(ret)) {
        LOG_WARN("failed to bind parameters", K(ret));
        return ret;
      }
    }

    // Execute the statement
    if (OB_SUCC(ret)) {
      int sqlite_ret = sqlite3_step(stmt);
      if (SQLITE_DONE == sqlite_ret) {
        if (nullptr != affected_rows) {
          *affected_rows = sqlite3_changes(db_);
        }
      } else {
        ret = OB_ERROR;
        LOG_WARN("failed to execute statement", K(ret), "sqlite_err", sqlite3_errmsg(db_));
      }
    }
  }

  return ret;
}

void ObSQLiteConnection::finalize_execute(ObSQLiteStmt *stmt)
{
  if (nullptr != stmt) {
    sqlite3_finalize(stmt);
  }
}

} // namespace share
} // namespace oceanbase
