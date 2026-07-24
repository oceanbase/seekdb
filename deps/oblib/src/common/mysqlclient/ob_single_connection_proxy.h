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

#ifndef _OB_SINGLE_CONNECTION_PROXY_H
#define _OB_SINGLE_CONNECTION_PROXY_H 1
#include "common/mysqlclient/ob_isql_client.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObISQLConnection;
} // end namespace sqlclient


// use one connection to execute multiple statements
// @note not thread safe
class ObSingleConnectionProxy : public ObISQLClient
{
public:
  ObSingleConnectionProxy();
  virtual ~ObSingleConnectionProxy();
public:
  virtual int escape(const char *from, const int64_t from_size,
                     char *to, const int64_t to_size, int64_t &out_size) override;
  // %res should be destructed before execute other sql
  virtual int read(ReadResult &res, const char *sql, const int32_t group_id) override;
  virtual int write(const char *sql, const int32_t group_id, int64_t &affected_rows) override;
  using ObISQLClient::read;
  using ObISQLClient::write;

  int connect(const int32_t group_id, ObISQLClient *sql_client);
  virtual sqlclient::ObISQLConnection *get_connection() override { return conn_; }
  virtual int acquire_connection(sqlclient::ObISQLConnection *&conn,
                                 ObISQLClient *client_addr,
                                 const int32_t group_id) override;
  virtual int release_connection(sqlclient::ObISQLConnection *conn,
                                 const bool success) override;
  virtual int on_client_inactive(ObISQLClient *client_addr) override;

  // in some situation, it allows continuation of SQL execution after failure in transaction,
  // and last_error should be reset.
  // 
  void reset_last_error() { errno_ = common::OB_SUCCESS; }

protected:
  void close();
  void set_errno(int err) { errno_ = err; }
  int get_errno() const { return errno_; }
public:
  bool check_inner_stat() const;
protected:
  int errno_;
  int64_t statement_count_;
  sqlclient::ObISQLConnection *conn_;
  ObISQLClient *sql_client_;
  DISALLOW_COPY_AND_ASSIGN(ObSingleConnectionProxy);
};

inline bool ObSingleConnectionProxy::check_inner_stat() const
{
  bool bret = (OB_SUCCESS == errno_ && NULL != sql_client_ && NULL != conn_);
  if (!bret) {
    COMMON_MYSQLP_LOG_RET(WARN, errno_, "invalid inner stat",
                          "errno", errno_, K_(sql_client), K_(conn));
  }
  return bret;
}


} // end namespace common
} // end namespace oceanbase

#endif /* _OB_SINGLE_CONNECTION_PROXY_H */
