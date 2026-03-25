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

int seekdb_connect(seekdb_handle db, const char* db_name, seekdb_conn_handle* out)
{
  if (!db || !out) return -1;
  seekdb_conn_t* conn = new (std::nothrow) seekdb_conn_t();
  if (!conn) return -2;
  conn->db = db;
  try {
    conn->conn = ObLiteEmbed::connect(db_name ? db_name : "oceanbase", true);
    *out = conn;
    return 0;
  } catch (const std::exception& e) {
    db->last_error = e.what();
    delete conn;
    return -3;
  }
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
