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
#pragma once

#include <string>
#include <thread>
#include "observer/ob_inner_sql_connection.h"
#include <pybind11/pybind11.h>


namespace oceanbase
{
namespace embed
{
extern char embed_version_str[oceanbase::common::OB_SERVER_VERSION_LENGTH];
class ObLiteEmbedCursor;
class ObLiteEmbedConn : public std::enable_shared_from_this<ObLiteEmbedConn>
{
public:
  ObLiteEmbedConn() : conn_(nullptr), result_seq_(0), result_(nullptr), session_(nullptr) {}
  ~ObLiteEmbedConn() { reset(); }
  void begin();
  void commit();
  void rollback();
  ObLiteEmbedCursor cursor();
  int execute(const char* sql, uint64_t &affected_rows, int64_t &result_seq, std::string &errmsg);
  void reset();
  void reset_result();
  int64_t get_result_seq() { return result_seq_; }
  observer::ObInnerSQLConnection *&get_conn() { return conn_; }
  sql::ObSQLSessionInfo *&get_session() { return session_; }
  common::ObCommonSqlProxy::ReadResult *get_res() { return result_; }
  bool need_autocommit();
private:
  observer::ObInnerSQLConnection *conn_;
  int64_t result_seq_;
  common::ObCommonSqlProxy::ReadResult *result_;
  sql::ObSQLSessionInfo* session_;
};

class ObLiteEmbedCursor
{
public:
  ObLiteEmbedCursor() : embed_conn_(), result_seq_(0) {}
  ~ObLiteEmbedCursor() { reset(); }
  uint64_t execute(const char* sql);
  pybind11::object fetchone();
  std::vector<pybind11::tuple> fetchall();
  void reset();
  void close() { reset(); }
  friend ObLiteEmbedCursor ObLiteEmbedConn::cursor();
private:
  std::shared_ptr<ObLiteEmbedConn> embed_conn_;
  int64_t result_seq_;
};

class ObLiteEmbed
{
public:
  ObLiteEmbed() {}
  ~ObLiteEmbed() {}
  static void open(const char* db_dir);
  static void close();
  static std::shared_ptr<ObLiteEmbedConn> connect(const char* db_name, const bool autocommit);
private:
  static int do_open_(const char* db_dir);
};

class ObLiteEmbedUtil
{
public:
  static int convert_result_to_pyobj(const int64_t col_idx, common::sqlclient::ObMySQLResult &result,ObObjMeta &type, pybind11::object &val);
};

} // end embed
} // end oceanbase
