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

#ifndef OCEANBASE_SINGLE_MYSQL_CONNECTION_POOL_H_
#define OCEANBASE_SINGLE_MYSQL_CONNECTION_POOL_H_
#include <mysql.h>
#include "lib/container/ob_se_array.h"
#include "lib/list/ob_list.h"
#include "lib/allocator/ob_cached_allocator.h"
#include "lib/mysqlclient/ob_mysql_connection.h"
#include "lib/mysqlclient/ob_mysql_connection_pool.h"
#include "lib/mysqlclient/ob_mysql_server_provider.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObSingleMySQLServerProvider : public ObMySQLServerProvider
{
public:
  ObSingleMySQLServerProvider();
  void init(const ObAddr &server);
  virtual int get_server(const int64_t svr_idx, common::ObAddr &server);
  virtual int64_t get_server_count() const;
  virtual int get_tenant_ids(ObIArray<uint64_t> &tenant_ids);
  virtual int get_tenant_servers(const uint64_t tenant_id, ObIArray<ObAddr> &tenant_servers);
  int refresh_server_list(void);
  int prepare_refresh() override;
  int end_refresh() override;
private:
  ObAddr server_;
};

class ObSingleMySQLConnectionPool : public ObMySQLConnectionPool
{
public:
  ObSingleMySQLConnectionPool();
  ~ObSingleMySQLConnectionPool();
  int init(const ObAddr &server, const ObConnPoolConfigParam &config);
private:
  ObSingleMySQLServerProvider provider_;
};
}
}
}

#endif //OCEANBASE_SINGLE_MYSQL_CONNECTION_POOL_H_
