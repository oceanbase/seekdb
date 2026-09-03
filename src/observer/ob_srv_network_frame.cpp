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

#define USING_LOG_PREFIX SERVER
#include "observer/ob_srv_network_frame.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include "observer/mysql/obsm_conn_callback.h"


using namespace oceanbase::rpc::frame;
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::share;
using namespace oceanbase::obmysql;

ObSrvNetworkFrame::ObSrvNetworkFrame(oceanbase::share::ObGlobalContext &gctx)
    : gctx_(gctx),
      xlator_(gctx),
      request_qhandler_(xlator_),
      deliver_(request_qhandler_)
{}

ObSrvNetworkFrame::~ObSrvNetworkFrame()
{
  // empty
}

static int update_tcp_keepalive_parameters_for_sql_nio_server(int tcp_keepalive_enabled, int64_t tcp_keepidle, int64_t tcp_keepintvl, int64_t tcp_keepcnt)
{
  int ret = OB_SUCCESS;
  tcp_keepidle = max(tcp_keepidle / static_cast<int64_t>(1000000), static_cast<int64_t>(1));
  tcp_keepintvl = max(tcp_keepintvl / static_cast<int64_t>(1000000), static_cast<int64_t>(1));
  if (tcp_keepidle > static_cast<int64_t>(UINT32_MAX)
      || tcp_keepintvl > static_cast<int64_t>(UINT32_MAX)
      || tcp_keepcnt <= 0
      || tcp_keepcnt > static_cast<int64_t>(UINT32_MAX)) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("TCP keepalive configuration exceeds the SQL-NIO ABI range",
             K(ret), K(tcp_keepidle), K(tcp_keepintvl), K(tcp_keepcnt));
  } else if (NULL != global_sql_nio_server) {
    global_sql_nio_server->update_tcp_keepalive_params(
        tcp_keepalive_enabled, static_cast<uint32_t>(tcp_keepidle),
        static_cast<uint32_t>(tcp_keepintvl),
        static_cast<uint32_t>(tcp_keepcnt));
  }
  return ret;
}

int ObSrvNetworkFrame::init()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(request_qhandler_.init())) {
  } else {
    LOG_INFO("init network frame successfully");
  }
  return ret;
}

void ObSrvNetworkFrame::destroy()
{
  if (NULL != obmysql::global_sql_nio_server) {
    obmysql::global_sql_nio_server->destroy();
  }
}

int ObSrvNetworkFrame::start()
{
  int ret = OB_SUCCESS;
  const int mysql_port = static_cast<int>(GCONF.mysql_port);
  gctx_.set_effective_mysql_port(0);
  obmysql::global_sql_nio_server =
      OB_NEW(obmysql::ObSqlNioServer, "SqlNio",
              obmysql::global_sm_conn_callback);
  if (NULL == obmysql::global_sql_nio_server) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("allocate memory for global_sql_nio_server failed", K(ret));
  } else {
    int sql_net_thread_count = (int)GCONF.sql_net_thread_count;
    if (sql_net_thread_count == 0) {
      if (GCONF.net_thread_count == 0) {
        sql_net_thread_count = get_default_net_thread_count();
      } else {
        sql_net_thread_count = GCONF.net_thread_count;
      }
    }
    if (OB_FAIL(obmysql::global_sql_nio_server->start(
            mysql_port, &deliver_, sql_net_thread_count,
            GCONF.ssl_client_authentication,
            GCONF.sql_protocol_min_tls_version.str()))) {
    } else {
      gctx_.set_effective_mysql_port(
          obmysql::global_sql_nio_server->get_bound_tcp_port());
      if (OB_FAIL(reload_config())) {
      }
    }
  }
  return ret;
}


int ObSrvNetworkFrame::reload_config()
{
  int ret = common::OB_SUCCESS;
  int enable_tcp_keepalive  = 0;
  int64_t tcp_keepidle      = GCONF.tcp_keepidle;
  int64_t tcp_keepintvl     = GCONF.tcp_keepintvl;
  int64_t tcp_keepcnt       = GCONF.tcp_keepcnt;

  if (GCONF.enable_tcp_keepalive) {
    enable_tcp_keepalive = 1;
    LOG_INFO("tcp keepalive enabled.");
  } else {
    LOG_INFO("tcp keepalive disabled.");
  }

  if (OB_FAIL(update_tcp_keepalive_parameters_for_sql_nio_server(enable_tcp_keepalive,
                                                                        tcp_keepidle, tcp_keepintvl,
                                                                        tcp_keepcnt))) {
  }
  return ret;
}

void ObSrvNetworkFrame::wait()
{
  obmysql::global_sql_nio_server->wait();
}

void ObSrvNetworkFrame::sql_nio_stop()
{
  if (NULL != obmysql::global_sql_nio_server) {
    obmysql::global_sql_nio_server->stop();
  }
  gctx_.set_effective_mysql_port(0);
}
