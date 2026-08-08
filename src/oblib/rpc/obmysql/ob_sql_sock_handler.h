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

#ifndef OCEANBASE_OBMYSQL_OB_SQL_SOCK_HANDLER_H_
#define OCEANBASE_OBMYSQL_OB_SQL_SOCK_HANDLER_H_
#include "nio.h"
#include "rpc/obmysql/ob_i_sm_conn_callback.h"
#include "rpc/obmysql/ob_sql_sock_session.h"

namespace oceanbase
{
namespace rpc {
class ObPacket;
class ObRequest;
namespace frame { class ObReqDeliver;};
};
namespace obmysql
{
class ObSqlSockSession;
class ObSqlSockHandler
{
public:
  ObSqlSockHandler(ObISMConnectionCallback& conn_cb):
      conn_cb_(conn_cb), deliver_(nullptr) {}
  ~ObSqlSockHandler() = default;
  int init(rpc::frame::ObReqDeliver* deliver);
  int on_readable(void *sess, char *body, int64_t body_len, uint64_t wire_bytes,
                  int packet_kind, const nio_mysql_command_view *command_view,
                  uint64_t generation);
  void on_close(void* sess, int err);
  int on_connect(void* sess, int fd, bool is_unix_socket);
private:
  // Wrap a decoded packet in the session's embedded ObRequest and stamp the
  // connection phase (drives xlator routing). One request in flight at a time —
  // the Rust reactor guarantees it (request_slot_busy), since sql_req_ is reused.
  int build_sql_req(ObSqlSockSession& sess, rpc::ObPacket* pkt,
                    uint64_t generation, rpc::ObRequest*& sql_req);
private:
  ObISMConnectionCallback& conn_cb_;
  rpc::frame::ObReqDeliver* deliver_;
};

}; // end namespace obmysql
}; // end namespace oceanbase

#endif /* OCEANBASE_OBMYSQL_OB_SQL_SOCK_HANDLER_H_ */
