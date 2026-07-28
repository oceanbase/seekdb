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

#ifndef _OB_SQL_CLIENT_DECORATOR_H
#define _OB_SQL_CLIENT_DECORATOR_H 1
#include "common/mysqlclient/ob_isql_client.h"
namespace oceanbase
{
namespace common
{
// read will retry `retry_limit' times when failed
class ObSQLClientRetry: public ObISQLClient
{
public:
  ObSQLClientRetry(ObISQLClient *sql_client, int32_t retry_limit)
      :sql_client_(sql_client),
       retry_limit_(retry_limit)
  {}
  virtual ~ObSQLClientRetry() {}

  virtual int escape(const char *from, const int64_t from_size,
      char *to, const int64_t to_size, int64_t &out_size) override;
  virtual int read(ReadResult &res, const char *sql, const int32_t group_id) override;
  virtual int write(const char *sql, const int32_t group_id, int64_t &affected_rows) override;

  virtual sqlclient::ObISQLConnection *get_connection() override;
  virtual int acquire_connection(sqlclient::ObISQLConnectionGuard &conn,
                                 const int32_t group_id) override;
  using ObISQLClient::read;
  using ObISQLClient::write;

  void set_retry_limit(int32_t retry_limit) { retry_limit_ = retry_limit; }
  int32_t get_retry_limit() const { return retry_limit_; }
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObSQLClientRetry);
private:
  ObISQLClient *sql_client_;
  int32_t retry_limit_;
};

} // end namespace common
} // end namespace oceanbase

#endif /* _OB_SQL_CLIENT_DECORATOR_H */
