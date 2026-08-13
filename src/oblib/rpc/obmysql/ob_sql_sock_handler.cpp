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

#define USING_LOG_PREFIX RPC_OBMYSQL
#include "rpc/obmysql/ob_sql_sock_handler.h"
#include "rpc/obmysql/ob_sql_sock_session.h"
#include "rpc/frame/ob_req_deliver.h"
#include "rpc/obmysql/ob_mysql_packet.h"   // ObMySQLRawPacket (login + command bodies)
#include "rpc/obmysql/ob_mysql_packet_storage.h"
#include "nio.h"

namespace oceanbase
{
using namespace common;
using namespace observer;
namespace obmysql
{

int ObSqlSockHandler::init(rpc::frame::ObReqDeliver* deliver)
{
  int ret = OB_SUCCESS;
  deliver_ = deliver;
  return ret;
}

static int get_client_addr_for_sql_sock_session(int fd, ObAddr& client_addr)
{
  int ret = OB_SUCCESS;
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql nio getpeername failed", K(errno), K(ret));
  } else {
    client_addr.from_sockaddr(&addr);
  }

  return ret;
}

int ObSqlSockHandler::on_connect(void* udata, int fd, bool is_unix_socket)
{
  int ret  = OB_SUCCESS;
  ObSqlSockSession* sess = (ObSqlSockSession*)udata;
  new(sess)ObSqlSockSession(conn_cb_);
  sess->fd_ = fd;
  if (OB_UNLIKELY(is_unix_socket)) {
    sess->client_addr_.set_unix_addr("");
  } else if (OB_FAIL(get_client_addr_for_sql_sock_session(fd, sess->client_addr_))) {
  }
  if (OB_SUCC(ret) && OB_FAIL(sess->init())) {
    LOG_WARN("sess init failed", K(ret));
  }
  return ret;
}

void ObSqlSockHandler::on_close(void* udata, int err)
{
  UNUSED(err);
  ObSqlSockSession* sess = (ObSqlSockSession*)udata;
  sess->destroy();
}

int ObSqlSockHandler::build_sql_req(ObSqlSockSession& sess,
                                    rpc::ObPacket* pkt,
                                    uint64_t generation,
                                    rpc::ObRequest*& sql_req)
{
  int ret = OB_SUCCESS;
  rpc::ObRequest* ret_req = &sess.sql_req_;
  new(ret_req)rpc::ObRequest(rpc::ObRequest::OB_MYSQL);
  ret_req->set_server_handle_context(&sess);
  ret_req->set_packet(pkt);
  ret_req->set_nio_request_generation(generation);
  ret_req->set_receive_timestamp(common::ObTimeUtility::current_time());
  ret_req->set_connection_phase(sess.conn_.connection_phase_);
  sql_req = ret_req;
  return ret;
}

int ObSqlSockHandler::on_readable(void *udata, char *body, int64_t body_len,
                                  uint64_t wire_bytes, int packet_kind,
                                  const nio_mysql_command_view *command_view,
                                  uint64_t generation) {
  // Rust supplies an explicit wire packet kind. C++ only builds the legacy
  // ObMySQLRawPacket view and verifies that ObMP's execution phase agrees.
  int ret = OB_SUCCESS;
  rpc::ObRequest *sql_req = NULL;
  ObSqlSockSession* sess = (ObSqlSockSession*)udata;
  ObMySQLRawPacket *pkt = NULL;
  ObMySQLRawPacketMode mode = ObMySQLRawPacketMode::LOGIN;
  bool phase_matches = false;
  if (NULL == sess || body_len < 0 || (body_len > 0 && NULL == body)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    switch (packet_kind) {
      case NIO_PACKET_LOGIN:
        mode = ObMySQLRawPacketMode::LOGIN;
        phase_matches =
            NULL == command_view && rpc::ConnectionPhaseEnum::CPE_CONNECTED ==
                                        sess->conn_.connection_phase_;
        break;
      case NIO_PACKET_COMMAND:
        mode = ObMySQLRawPacketMode::COMMAND;
        phase_matches =
            NULL != command_view && rpc::ConnectionPhaseEnum::CPE_AUTHED ==
                                        sess->conn_.connection_phase_;
        break;
      case NIO_PACKET_AUTH_SWITCH_RESPONSE:
        mode = ObMySQLRawPacketMode::AUTH_SWITCH_RESPONSE;
        phase_matches =
            NULL == command_view && rpc::ConnectionPhaseEnum::CPE_AUTH_SWITCH ==
                                        sess->conn_.connection_phase_;
        break;
      default:
        break;
    }
    if (!phase_matches) {
      ret = OB_STATE_NOT_MATCH;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(build_mysql_raw_packet_view(sess->pool_, body, body_len,
                                                 wire_bytes, mode, command_view,
                                                 pkt))) {
  } else if (OB_FAIL(build_sql_req(*sess, pkt, generation, sql_req))) {
  } else if (NULL == sql_req) {
    // nothing to deliver
  } else if (OB_FAIL(deliver_->deliver(*sql_req))) {
  }
  return ret;
}

}; // end namespace obmysql
}; // end namespace oceanbase
