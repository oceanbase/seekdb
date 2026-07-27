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
#include "rpc/obmysql/ob_sql_sock_processor.h"
#include "rpc/frame/ob_req_deliver.h"

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
  new(sess)ObSqlSockSession(conn_cb_, nio_);
  if (OB_UNLIKELY(is_unix_socket)) {
    sess->client_addr_.set_unix_addr("");
  } else if (OB_FAIL(get_client_addr_for_sql_sock_session(fd, sess->client_addr_))) {
    LOG_WARN("sql nio get_client_addr_for_sql_sock_session failed", K(ret));
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

void ObSqlSockHandler::on_flushed(void* udata)
{
  ObSqlSockSession* sess = (ObSqlSockSession*)udata;
  sess->on_flushed();
}

int ObSqlSockHandler::on_readable(void* udata)
{
  int ret = OB_SUCCESS;
  rpc::ObPacket *pkt = NULL;
  rpc::ObRequest *sql_req = NULL;
  ObSqlSockSession* sess = (ObSqlSockSession*)udata;

  if (NULL == sess) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("sess is null!", K(ret));
  } else if (OB_FAIL(sock_processor_.decode_sql_packet(sess->pool_, *sess, NULL, pkt))) {
    LOG_WARN("decode sql req fail", K(ret), K(sess->sql_session_id_));
  } else if (NULL == pkt) {
    sess->revert_sock();
  } else if (OB_FAIL(sock_processor_.build_sql_req(*sess, pkt, sql_req))) {
    LOG_WARN("build sql req fail", K(ret), K(sess->sql_session_id_));
  }

  if (OB_SUCCESS != ret || NULL == sql_req) {
  } else if (FALSE_IT(sess->set_last_decode_succ_and_deliver_time(ObClockGenerator::getClock()))) {
  } else if (OB_FAIL(deliver_->deliver(*sql_req))) {
    LOG_WARN("deliver sql request fail", K(ret), K(sess->sql_session_id_));
  }

  return ret;
}

}; // end namespace obmysql
}; // end namespace oceanbase
