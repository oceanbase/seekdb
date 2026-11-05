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

/*
 * SeekDB Embedded C API Implementation
 *
 * Wraps ObLiteEmbed / ObLiteEmbedConn to provide a plain C interface.
 */
#define USING_LOG_PREFIX SERVER
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define be64toh(x) OSSwapBigToHostInt64(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#elif defined(__linux__)
#include <endian.h>
#endif
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include "observer/embed/c/seekdb.h"
#include "observer/embed/python/ob_embed_impl.h"
#include "observer/ob_server.h"
#include "observer/ob_inner_sql_result.h"
#include "observer/ob_server_options.h"
#include "lib/string/ob_string.h"
#include "common/ob_version_def.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "lib/charset/ob_charset.h"
#include "lib/allocator/ob_malloc.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "sql/ob_result_set.h"

using namespace oceanbase;
using namespace oceanbase::embed;
using namespace oceanbase::common;
using namespace oceanbase::observer;

/* Internal structures hidden behind opaque handles */

struct seekdb_t {
  std::string last_error;
};

struct seekdb_conn_t {
  std::shared_ptr<ObLiteEmbedConn> conn;
  seekdb_handle db;
};

struct seekdb_result_t {
  std::vector<std::string> column_names;
  std::vector<std::vector<std::string>> rows;
  std::vector<std::vector<bool>> null_flags;
  int affected_rows;
};

static int collect_result(ObLiteEmbedConn* embed_conn, seekdb_result_t* result)
{
  int ret = OB_SUCCESS;
  ObCommonSqlProxy::ReadResult* read_result = embed_conn->get_res();
  if (OB_ISNULL(read_result) || OB_ISNULL(read_result->get_result())) {
    // Non-SELECT statement, no result set
    return OB_SUCCESS;
  }

  sqlclient::ObMySQLResult* mysql_result = read_result->get_result();
  ObInnerSQLResult* inner_result = reinterpret_cast<ObInnerSQLResult*>(mysql_result);

  // If this is a command (e.g. SET, USE), no rows to fetch
  if (OB_NOT_NULL(inner_result->result_set().get_cmd())) {
    return OB_SUCCESS;
  }

  // Collect column names from field metadata
  const ColumnsFieldIArray* fields = inner_result->result_set().get_field_columns();
  int64_t column_count = mysql_result->get_column_count();
  if (OB_NOT_NULL(fields)) {
    for (int64_t i = 0; i < fields->count(); i++) {
      const ObField& field = fields->at(i);
      result->column_names.emplace_back(field.cname_.ptr(), field.cname_.length());
    }
  } else {
    // Fallback: use generic column names
    for (int64_t i = 0; i < column_count; i++) {
      result->column_names.push_back("col" + std::to_string(i));
    }
  }

  // Fetch all rows, converting every value to string
  while (OB_SUCC(ret)) {
    ret = mysql_result->next();
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
      break;
    }
    if (OB_FAIL(ret)) break;

    std::vector<std::string> row;
    std::vector<bool> nulls;
    for (int64_t i = 0; i < column_count; i++) {
      ObObj obj;
      if (OB_FAIL(mysql_result->get_obj(i, obj))) {
        row.emplace_back("");
        nulls.push_back(true);
      } else if (obj.is_null()) {
        row.emplace_back("");
        nulls.push_back(true);
      } else {
        // Convert to string representation
        char buf[OB_MAX_VARCHAR_LENGTH];
        int64_t pos = 0;
        if (OB_SUCC(obj.print_plain_str_literal(buf, sizeof(buf), pos))) {
          row.emplace_back(buf, pos);
        } else {
          // Fallback: use obj.print_sql_literal or raw
          ObString str_val;
          if (obj.is_string_type() && OB_SUCC(obj.get_string(str_val))) {
            row.emplace_back(str_val.ptr(), str_val.length());
          } else {
            row.emplace_back("?");
          }
        }
        nulls.push_back(false);
      }
    }
    result->rows.push_back(std::move(row));
    result->null_flags.push_back(std::move(nulls));
  }
  return ret;
}

/* C API implementation */

int seekdb_open(const char* db_dir, seekdb_handle* out)
{
  if (!db_dir || !out) return -1;
  seekdb_t* db = new (std::nothrow) seekdb_t();
  if (!db) return -2;
  try {
    ObLiteEmbed::open(db_dir);
    *out = db;
    return 0;
  } catch (const std::exception& e) {
    db->last_error = e.what();
    *out = db;
    return -3;
  }
}

int seekdb_open_with_service(const char* db_dir, int port, seekdb_handle* out)
{
  if (!db_dir || !out || port <= 0) return -1;
  seekdb_t* db = new (std::nothrow) seekdb_t();
  if (!db) return -2;
  try {
    ObLiteEmbed::open_with_service(db_dir, port);
    *out = db;
    return 0;
  } catch (const std::exception& e) {
    db->last_error = e.what();
    *out = db;
    return -3;
  }
}

void seekdb_close(seekdb_handle db)
{
  if (!db) return;
  // Do NOT call ObLiteEmbed::close() -- it calls _Exit(0) which would
  // kill the host process. For embedded use (Android, iOS), we just
  // clean up our handle and let the process continue.
  // The pid file cleanup and observer shutdown can be added later
  // when OceanBase supports graceful embedded shutdown.
  delete db;
}

int seekdb_connect_ex(seekdb_handle db, const char* db_name, int autocommit, seekdb_conn_handle* out)
{
  if (!db || !out) return -1;
  seekdb_conn_t* conn = new (std::nothrow) seekdb_conn_t();
  if (!conn) return -2;
  conn->db = db;
  try {
    conn->conn = ObLiteEmbed::connect(db_name ? db_name : "oceanbase", autocommit != 0);
    *out = conn;
    return 0;
  } catch (const std::exception& e) {
    db->last_error = e.what();
    delete conn;
    return -3;
  }
}

int seekdb_connect(seekdb_handle db, const char* db_name, seekdb_conn_handle* out)
{
  return seekdb_connect_ex(db, db_name, 0, out);
}

void seekdb_disconnect(seekdb_conn_handle conn)
{
  if (!conn) return;
  conn->conn.reset();
  delete conn;
}

int seekdb_execute(seekdb_conn_handle conn, const char* sql, seekdb_result_handle* out)
{
  if (!conn || !sql || !out) return -1;
  if (!conn->conn) return -2;

  seekdb_result_t* result = new (std::nothrow) seekdb_result_t();
  if (!result) return -3;
  result->affected_rows = 0;

  try {
    uint64_t affected = 0;
    int64_t result_seq = 0;
    std::string errmsg;
    int ret = conn->conn->execute(sql, affected, result_seq, errmsg);
    if (ret != OB_SUCCESS) {
      conn->db->last_error = errmsg.empty() ? "execute failed" : errmsg;
      delete result;
      return ret;
    }

    if (affected == UINT64_MAX) {
      // SELECT -- collect result set
      result->affected_rows = -1;
      ret = collect_result(conn->conn.get(), result);
      if (ret != OB_SUCCESS) {
        conn->db->last_error = "failed to collect results";
        delete result;
        return ret;
      }
    } else {
      result->affected_rows = static_cast<int>(affected);
    }

    // Autocommit if needed
    if (conn->conn->need_autocommit()) {
      conn->conn->commit();
    }

    *out = result;
    return 0;
  } catch (const std::exception& e) {
    conn->db->last_error = e.what();
    delete result;
    return -4;
  }
}

void seekdb_result_free(seekdb_result_handle result)
{
  delete result;
}

int seekdb_result_column_count(seekdb_result_handle result)
{
  if (!result) return 0;
  return static_cast<int>(result->column_names.size());
}

const char* seekdb_result_column_name(seekdb_result_handle result, int col)
{
  if (!result || col < 0 || col >= static_cast<int>(result->column_names.size())) return nullptr;
  return result->column_names[col].c_str();
}

int seekdb_result_row_count(seekdb_result_handle result)
{
  if (!result) return 0;
  return static_cast<int>(result->rows.size());
}

const char* seekdb_result_value(seekdb_result_handle result, int row, int col)
{
  if (!result) return nullptr;
  if (row < 0 || row >= static_cast<int>(result->rows.size())) return nullptr;
  if (col < 0 || col >= static_cast<int>(result->rows[row].size())) return nullptr;
  if (result->null_flags[row][col]) return nullptr;
  return result->rows[row][col].c_str();
}

int seekdb_result_affected_rows(seekdb_result_handle result)
{
  if (!result) return 0;
  return result->affected_rows;
}

const char* seekdb_error(seekdb_handle db)
{
  if (!db || db->last_error.empty()) return nullptr;
  return db->last_error.c_str();
}

/* Statement API implementation */

struct seekdb_stmt_t {
  seekdb_conn_handle conn;
  std::string sql;
  std::shared_ptr<ObLiteEmbedConn> conn_ptr;
  ObCommonSqlProxy::ReadResult* read_result;
  sqlclient::ObMySQLResult* mysql_result;
  std::vector<std::string> column_names;
  std::vector<ObObj> current_row;
  bool has_row;
  bool done;
  int param_count;  // number of ? placeholders
  std::string last_error;
  // True prepared statement support
  uint64_t stmt_id;
  bool prepared;
  common::ObArenaAllocator allocator_;
  common::ParamStore params_;
};

int seekdb_prepare(seekdb_conn_handle conn, const char* sql, seekdb_stmt_handle* out)
{
  if (!conn || !sql || !out) return -1;
  if (!conn->conn) return -2;

  seekdb_stmt_t* stmt = new (std::nothrow) seekdb_stmt_t();
  if (!stmt) return -3;

  stmt->conn = conn;
  stmt->sql = sql;
  stmt->conn_ptr = conn->conn;
  stmt->read_result = nullptr;
  stmt->mysql_result = nullptr;
  stmt->has_row = false;
  stmt->done = false;
  stmt->prepared = false;
  stmt->stmt_id = 0;
  // Initialize params_ with wrapper allocator
  new (&stmt->params_) common::ParamStore(common::ObWrapperAllocator(&stmt->allocator_));

  // Call real prepare_stmt via ObLiteEmbedConn
  int64_t param_count = 0;
  uint64_t stmt_id = 0;
  int ret = conn->conn->prepare_stmt(sql, stmt_id, param_count);
  LOG_WARN("[SEEKDB_DEBUG] prepare_stmt returned", K(ret), K(stmt_id), K(param_count));
  if (ret != OB_SUCCESS) {
    conn->db->last_error = "prepare_stmt failed";
    delete stmt;
    return ret;
  }

  stmt->stmt_id = stmt_id;
  stmt->param_count = static_cast<int>(param_count);
  stmt->prepared = true;

  // Initialize params_ - OceanBase ParamStore is 0-indexed
  // So params_[0] = first parameter, params_[1] = second parameter, etc.
  if (param_count > 0) {
    stmt->params_.reserve(param_count);
    for (int64_t i = 0; i < param_count; i++) {
      ObObjParam obj;
      obj.set_null();
      obj.set_param_meta();
      stmt->params_.push_back(obj);
    }
  }

  *out = stmt;
  return 0;
}

int seekdb_step(seekdb_stmt_handle stmt)
{
  if (!stmt) return -1;
  if (stmt->done) return SEEKDB_DONE;  // 101

  // First step: execute the prepared statement
  if (!stmt->has_row && stmt->mysql_result == nullptr) {
    uint64_t affected = 0;
    int64_t result_seq = 0;

    int ret = OB_SUCCESS;
    if (stmt->prepared) {
      ret = stmt->conn_ptr->execute_stmt(stmt->stmt_id, stmt->params_, affected, result_seq);
    } else {
      stmt->last_error = "statement not prepared";
      return -1;
    }

    if (ret != OB_SUCCESS) {
      stmt->last_error = "execute_stmt failed";
      LOG_WARN("execute_stmt failed", K(ret), K(stmt->stmt_id));
      return ret;
    }

    // For non-SELECT statements, we're done
    if (affected != UINT64_MAX) {
      stmt->done = true;
      return SEEKDB_DONE;  // 101
    }

    // SELECT statement - get result
    stmt->read_result = stmt->conn_ptr->get_res();
    if (stmt->read_result && stmt->read_result->get_result()) {
      stmt->mysql_result = stmt->read_result->get_result();
      ObInnerSQLResult* inner_result = reinterpret_cast<ObInnerSQLResult*>(stmt->mysql_result);

      // Get column names
      const ColumnsFieldIArray* fields = inner_result->result_set().get_field_columns();
      if (fields) {
        for (int64_t i = 0; i < fields->count(); i++) {
          const ObField& field = fields->at(i);
          stmt->column_names.emplace_back(field.cname_.ptr(), field.cname_.length());
        }
      }

      // Fetch first row
      ret = stmt->mysql_result->next();
      if (ret == OB_ITER_END) {
        stmt->done = true;
        return SEEKDB_DONE;  // 101
      }
      if (ret != OB_SUCCESS) {
        stmt->last_error = "failed to fetch row";
        return ret;
      }

      // Store current row values
      int64_t col_count = stmt->mysql_result->get_column_count();
      stmt->current_row.resize(col_count);
      for (int64_t i = 0; i < col_count; i++) {
        stmt->mysql_result->get_obj(i, stmt->current_row[i]);
      }
      stmt->has_row = true;
      return SEEKDB_ROW;  // 100
    }
  } else if (stmt->has_row) {
    // Fetch next row
    int ret = stmt->mysql_result->next();
    if (ret == OB_ITER_END) {
      stmt->done = true;
      return SEEKDB_DONE;  // 101
    }
    if (ret != OB_SUCCESS) {
      stmt->last_error = "failed to fetch row";
      return ret;
    }

    // Update current row values
    int64_t col_count = stmt->mysql_result->get_column_count();
    stmt->current_row.resize(col_count);
    for (int64_t i = 0; i < col_count; i++) {
      stmt->mysql_result->get_obj(i, stmt->current_row[i]);
    }
    return SEEKDB_ROW;  // 100
  }

  stmt->done = true;
  return SEEKDB_DONE;  // 101
}

int seekdb_reset(seekdb_stmt_handle stmt)
{
  if (!stmt) return -1;

  stmt->read_result = nullptr;
  stmt->mysql_result = nullptr;
  stmt->has_row = false;
  stmt->done = false;
  stmt->current_row.clear();
  stmt->column_names.clear();

  return 0;
}

int seekdb_finalize(seekdb_stmt_handle stmt)
{
  if (!stmt) return -1;

  // Autocommit if needed
  if (stmt->conn_ptr && stmt->conn_ptr->need_autocommit()) {
    stmt->conn_ptr->commit();
  }

  // Close prepared statement
  if (stmt->prepared && stmt->conn_ptr) {
    stmt->conn_ptr->close_stmt(stmt->stmt_id);
    stmt->prepared = false;
  }

  delete stmt;
  return 0;
}

/* Parameter binding - fill ParamStore for real prepared statements */
int seekdb_bind_int(seekdb_stmt_handle stmt, int col, int val)
{
  // SQLite is 1-indexed, but ParamStore is 0-indexed
  if (!stmt || col < 1 || col > stmt->param_count) return -1;
  ObObjParam& param = stmt->params_.at(col - 1);  // Convert to 0-indexed
  param.set_int(val);
  param.set_param_meta();
  return 0;
}

int seekdb_bind_int64(seekdb_stmt_handle stmt, int col, long long val)
{
  // SQLite is 1-indexed, but ParamStore is 0-indexed
  if (!stmt || col < 1 || col > stmt->param_count) return -1;
  ObObjParam& param = stmt->params_.at(col - 1);  // Convert to 0-indexed
  param.set_int(val);
  param.set_param_meta();
  return 0;
}

int seekdb_bind_double(seekdb_stmt_handle stmt, int col, double val)
{
  // SQLite is 1-indexed, but ParamStore is 0-indexed
  if (!stmt || col < 1 || col > stmt->param_count) return -1;
  ObObjParam& param = stmt->params_.at(col - 1);  // Convert to 0-indexed
  param.set_double(val);
  param.set_param_meta();
  return 0;
}

int seekdb_bind_text(seekdb_stmt_handle stmt, int col, const char* val)
{
  // SQLite is 1-indexed, but ParamStore is 0-indexed
  if (!stmt || col < 1 || col > stmt->param_count) return -1;
  ObObjParam& param = stmt->params_.at(col - 1);  // Convert to 0-indexed
  if (!val) {
    param.set_null();
  } else {
    // Deep copy string into allocator
    size_t len = strlen(val);
    char* buf = static_cast<char*>(stmt->allocator_.alloc(len));
    if (buf) {
      memcpy(buf, val, len);
      param.set_varchar(ObString(len, buf));
    } else {
      param.set_varchar(ObString(val));
    }
  }
  param.set_param_meta();
  return 0;
}

int seekdb_bind_null(seekdb_stmt_handle stmt, int col)
{
  // SQLite is 1-indexed, but ParamStore is 0-indexed
  if (!stmt || col < 1 || col > stmt->param_count) return -1;
  ObObjParam& param = stmt->params_.at(col - 1);  // Convert to 0-indexed
  param.set_null();
  param.set_param_meta();
  return 0;
}

int seekdb_bind_parameter_count(seekdb_stmt_handle stmt)
{
  if (!stmt) return 0;
  return stmt->param_count;
}

/* Result access */
int seekdb_column_count(seekdb_stmt_handle stmt)
{
  if (!stmt) return 0;
  return static_cast<int>(stmt->current_row.size());
}

const char* seekdb_column_name(seekdb_stmt_handle stmt, int col)
{
  if (!stmt || col < 0 || col >= static_cast<int>(stmt->column_names.size())) return nullptr;
  return stmt->column_names[col].c_str();
}

int seekdb_column_type(seekdb_stmt_handle stmt, int col)
{
  if (!stmt || col < 0 || col >= static_cast<int>(stmt->current_row.size())) return 0;
  const ObObj& obj = stmt->current_row[col];
  if (obj.is_null()) return 0;           // NULL
  if (obj.is_integer_type()) return 1;  // INTEGER
  if (obj.is_float()) return 2;         // FLOAT
  if (obj.is_string_type()) return 3;   // TEXT
  if (obj.is_blob()) return 4;          // BLOB
  return 3;  // default to TEXT
}

int seekdb_column_int(seekdb_stmt_handle stmt, int col)
{
  if (!stmt || col < 0 || col >= static_cast<int>(stmt->current_row.size())) return 0;
  return stmt->current_row[col].get_int();
}

long long seekdb_column_int64(seekdb_stmt_handle stmt, int col)
{
  if (!stmt || col < 0 || col >= static_cast<int>(stmt->current_row.size())) return 0;
  return stmt->current_row[col].get_int();
}

double seekdb_column_double(seekdb_stmt_handle stmt, int col)
{
  if (!stmt || col < 0 || col >= static_cast<int>(stmt->current_row.size())) return 0.0;
  return stmt->current_row[col].get_double();
}

const char* seekdb_column_text(seekdb_stmt_handle stmt, int col)
{
  if (!stmt || col < 0 || col >= static_cast<int>(stmt->current_row.size())) return nullptr;
  const ObObj& obj = stmt->current_row[col];
  if (obj.is_null()) return nullptr;

  static thread_local std::string buf;
  ObString str_val;
  if (obj.get_string(str_val) == OB_SUCCESS) {
    buf.assign(str_val.ptr(), str_val.length());
    return buf.c_str();
  }

  // Fallback: convert to string
  char tmp[OB_MAX_VARCHAR_LENGTH];
  int64_t pos = 0;
  if (obj.print_plain_str_literal(tmp, sizeof(tmp), pos) == OB_SUCCESS) {
    buf.assign(tmp, pos);
    return buf.c_str();
  }

  return nullptr;
}

int seekdb_column_bytes(seekdb_stmt_handle stmt, int col)
{
  if (!stmt || col < 0 || col >= static_cast<int>(stmt->current_row.size())) return 0;
  const ObObj& obj = stmt->current_row[col];
  ObString str_val;
  if (obj.get_string(str_val) == OB_SUCCESS) {
    return static_cast<int>(str_val.length());
  }
  return 0;
}

/* Utility */
const char* seekdb_errmsg(seekdb_conn_handle conn)
{
  if (!conn || !conn->db || conn->db->last_error.empty()) return "no error";
  return conn->db->last_error.c_str();
}

const char* seekdb_libversion(void)
{
  return "SeekDB-1.0.0";
}
