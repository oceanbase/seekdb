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

#ifndef OCEANBASE_MYSQLCLIENT_OB_ISQL_CONNECTION_POOL_H_
#define OCEANBASE_MYSQLCLIENT_OB_ISQL_CONNECTION_POOL_H_

#include <stdint.h>
#include "lib/list/ob_list.h"
#include "lib/container/ob_se_array.h"
#include "lib/allocator/ob_cached_allocator.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
class ObAddr;
class ObString;

struct ObConnPoolConfigParam
{
  ObConnPoolConfigParam() { reset(); }
  ~ObConnPoolConfigParam() { }
  void reset() { memset(this,0, sizeof(ObConnPoolConfigParam)); }

  int64_t sqlclient_wait_timeout_;      // s
  int64_t connection_refresh_interval_; // us
  int64_t connection_pool_warn_time_;   // us
  int64_t long_query_timeout_;          // us
  int64_t sqlclient_per_observer_conn_limit_;
};
namespace sqlclient
{

class ObISQLConnection;

enum ObSQLConnPoolType
{
  UNKNOWN_POOL,
  MYSQL_POOL,
  INNER_POOL,
};

enum DblinkDriverProto{
  DBLINK_UNKNOWN = -1,
  DBLINK_DRV_OB = 0,
  DBLINK_DRV_OCI,
};

class ObISQLConnectionPool
{
public:
  ObISQLConnectionPool() {};
  virtual ~ObISQLConnectionPool() {};

  // sql string escape
  virtual int escape(const char *from, const int64_t from_size,
      char *to, const int64_t to_size, int64_t &out_size) = 0;

  // acquired connection must be released
  virtual int acquire(ObISQLConnection *&conn, ObISQLClient *client_addr)
  {
    return this->acquire(OB_INVALID_TENANT_ID, conn, client_addr, 0);
  }
  virtual int acquire(const uint64_t tenant_id, ObISQLConnection *&conn, ObISQLClient *client_addr, const int32_t group_id) = 0;
  virtual int release(ObISQLConnection *conn, const bool success) = 0;
  virtual int on_client_inactive(ObISQLClient *client_addr) = 0;
  virtual ObSQLConnPoolType get_type() = 0;
};

} // end namespace sqlclient
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_MYSQLCLIENT_OB_ISQL_CONNECTION_POOL_H_
