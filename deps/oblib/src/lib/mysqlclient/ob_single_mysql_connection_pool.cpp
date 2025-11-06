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

#define USING_LOG_PREFIX LIB_MYSQLC
#include "lib/mysqlclient/ob_isql_connection_pool.h"
#include "lib/mysqlclient/ob_single_mysql_connection_pool.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
ObSingleMySQLServerProvider::ObSingleMySQLServerProvider()
{
}

void ObSingleMySQLServerProvider::init(const ObAddr &server)
{
  server_ = server;
}

int ObSingleMySQLServerProvider::get_server(
    const int64_t svr_idx,
    common::ObAddr &server)
{
  int ret = OB_SUCCESS;
  if (svr_idx != 0) {
    ret = OB_ARRAY_OUT_OF_RANGE;
    LOG_WARN("server index out of range", K(ret));
  } else if (server_.is_valid()) {
    server = server_;
  } else {
    ret = OB_NOT_INIT;
  }
  return ret;
}

int64_t ObSingleMySQLServerProvider::get_server_count() const
{
  return (server_.is_valid()) ? 1 : 0;
}

int ObSingleMySQLServerProvider::get_tenant_ids(ObIArray<uint64_t> &tenant_ids)
{
  tenant_ids.reset();
  return OB_SUCCESS;
}

int ObSingleMySQLServerProvider::get_tenant_servers(const uint64_t tenant_id, ObIArray<ObAddr> &tenant_servers)
{
  tenant_servers.reset();
  return OB_SUCCESS;
}

int ObSingleMySQLServerProvider::refresh_server_list(void)
{
  return OB_SUCCESS;
}

int ObSingleMySQLServerProvider::prepare_refresh()
{
  return OB_SUCCESS;
}

int ObSingleMySQLServerProvider::end_refresh()
{
  return OB_SUCCESS;
}

ObSingleMySQLConnectionPool::ObSingleMySQLConnectionPool()
{
}

ObSingleMySQLConnectionPool::~ObSingleMySQLConnectionPool()
{
}

int ObSingleMySQLConnectionPool::init(const ObAddr &server, const ObConnPoolConfigParam &config)
{
  provider_.init(server);
  set_server_provider(&provider_);//just fake
  update_config(config);
  //init the single server connection
  int64_t cluster_id = OB_INVALID_ID;
  int ret = create_server_connection_pool(server);
  return ret;
}

} // end namespace sqlclient
} // end namespace common
} // end namespace oceanbase
