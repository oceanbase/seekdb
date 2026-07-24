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

#ifndef OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_POOL_H_
#define OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_POOL_H_

#include "common/mysqlclient/ob_isql_connection_pool.h"
#include "ob_inner_sql_connection.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObISQLConnection;
}
}
namespace observer
{
class ObInnerSQLConnectionPool : public common::sqlclient::ObISQLConnectionPool
{
public:
  ObInnerSQLConnectionPool();
  virtual ~ObInnerSQLConnectionPool();

  int init(const bool is_ddl = false);

  virtual void stop() { stop_ = true; }

  // sql string escape
  virtual int escape(const char *from, const int64_t from_size,
      char *to, const int64_t to_size, int64_t &out_size);

  // acquired connection must be released
  virtual int acquire(common::sqlclient::ObISQLConnection *&conn, ObISQLClient *client_addr, const int32_t group_id) override;
  virtual int release(common::sqlclient::ObISQLConnection *conn, const bool success);

  virtual int on_client_inactive(common::ObISQLClient *client_addr) override;
  virtual common::sqlclient::ObSQLConnPoolType get_type() override { return common::sqlclient::INNER_POOL; }

private:
  bool inited_;
  volatile bool stop_;
  bool is_ddl_;

  DISALLOW_COPY_AND_ASSIGN(ObInnerSQLConnectionPool);
};

} // end namespace observer
} // end namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_POOL_H_
