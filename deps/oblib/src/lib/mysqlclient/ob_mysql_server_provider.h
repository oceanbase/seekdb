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

#ifndef __OB_COMMON_SQLCLIENT_MYSQL_SERVER_PROVIDER__
#define __OB_COMMON_SQLCLIENT_MYSQL_SERVER_PROVIDER__

#include "lib/net/ob_addr.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObMySQLServerProvider
{
public:
  ObMySQLServerProvider() {};
  virtual ~ObMySQLServerProvider() {};
  virtual int get_server(const int64_t svr_idx, common::ObAddr &server) = 0;
  virtual int64_t get_server_count() const = 0;
  // should imply get_tenant_ids/get_tenant_servers
  // if using MySQLConnectionPool and MySQLConnectionPoolType is TENANT_POOL
  // MUST contains SYS_TENANT
  virtual int get_tenant_ids(ObIArray<uint64_t> &tenant_ids) = 0;
  virtual int get_tenant_servers(const uint64_t tenant_id, ObIArray<ObAddr> &tenant_servers) = 0;
  virtual int refresh_server_list(void) = 0;
  virtual int prepare_refresh() = 0;
  virtual int end_refresh() = 0;
  virtual bool need_refresh() { return true; }
private:
  ObMySQLServerProvider(const ObMySQLServerProvider &);
  ObMySQLServerProvider &operator=(const ObMySQLServerProvider &);
};
}
}
}

#endif
