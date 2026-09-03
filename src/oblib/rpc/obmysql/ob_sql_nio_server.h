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

#ifndef OCEANBASE_OBMYSQL_OB_SQL_NIO_SERVER_H_
#define OCEANBASE_OBMYSQL_OB_SQL_NIO_SERVER_H_
#include "nio.h"
#include "lib/lock/ob_mutex.h"
#include "rpc/obmysql/ob_sql_sock_handler.h"

namespace oceanbase
{
namespace obmysql
{
class ObSqlNioServer
{
public:
  ObSqlNioServer(ObISMConnectionCallback &conn_cb)
      : io_handler_(conn_cb) {}
  virtual ~ObSqlNioServer() {}
  int get_thread_count() const { return n_thread_; }
  int64_t get_bound_tcp_port() const { return bound_tcp_port_; }
  int start(int port, rpc::frame::ObReqDeliver* deliver, int n_thread,
            bool use_tls, const char *min_tls_version);
  int set_thread_count(const int thread_num);
  void stop();
  void wait();
  void destroy();
  void update_tcp_keepalive_params(int keepalive_enabled, uint32_t tcp_keepidle, uint32_t tcp_keepintvl, uint32_t tcp_keepcnt);

private:
  ObSqlSockHandler io_handler_; // for io thread
  lib::ObMutex reactor_lock_;
  nio_reactor* reactor_ = nullptr;
  int n_thread_ = 1;
  int64_t bound_tcp_port_ = 0;
  
};
extern ObSqlNioServer* global_sql_nio_server;
}; // end namespace obmysql
}; // end namespace oceanbase

#endif /* OCEANBASE_OBMYSQL_OB_SQL_NIO_SERVER_H_ */
