/**
* Copyright (c) 2021 OceanBase
* OceanBase CE is licensed under Mulan PubL v2.
* You can use this software according to the terms and conditions of the Mulan PubL v2.
* You may obtain a copy of Mulan PubL v2 at:
*          http://license.coscl.org.cn/MulanPubL-2.0
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PubL v2 for more details.
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
class ObLiteEmbedCursor;
class ObLiteEmbedConn : public std::enable_shared_from_this<ObLiteEmbedConn>
{
public:
  ObLiteEmbedConn() : conn_(nullptr), result_seq_(0), result_(nullptr) {}
  ~ObLiteEmbedConn() { reset(); }
  void begin();
  void commit();
  void rollback();
  ObLiteEmbedCursor cursor();
  int execute(const char* sql, int64_t &affected_rows, int64_t &result_seq);
  void reset();
  void reset_result();
  int64_t get_result_seq() { return result_seq_; }
  observer::ObInnerSQLConnection *&get_conn() { return conn_; }
  common::ObCommonSqlProxy::ReadResult *get_res() { return result_; }
private:
  observer::ObInnerSQLConnection *conn_;
  int64_t result_seq_;
  common::ObCommonSqlProxy::ReadResult *result_;
};

class ObLiteEmbedCursor
{
public:
  ObLiteEmbedCursor() : embed_conn_(), result_seq_(0) {}
  ~ObLiteEmbedCursor() {
    embed_conn_.reset();
    result_seq_ = 0;
  }
  int execute(const char* sql);
  std::vector<pybind11::object> fetchone();
  std::vector<std::vector<pybind11::object>> fetchall();
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
  static int bootstrap_();
  static void notify_bootstrap_();
private:
  static std::string rs_list_;
  static std::string opts_;
  static std::string data_abs_dir_;
  static std::thread th_;
};

class ObLiteEmbedUtil
{
public:
  static int convert_result_to_pyobj(const int64_t col_idx, common::sqlclient::ObMySQLResult &result,ObObjMeta &type, pybind11::object &val);
};

} // end embed
} // end oceanbase
