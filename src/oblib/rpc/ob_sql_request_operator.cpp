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

#ifdef _WIN32
#define USING_LOG_PREFIX RPC_OBMYSQL
#endif
#include "rpc/ob_sql_request_operator.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "rpc/obmysql/ob_mysql_packet_storage.h"
#include "rpc/obmysql/ob_sql_sock_session.h"
#include "nio.h"

void OB_WEAK_SYMBOL request_finish_callback();

namespace oceanbase
{
using namespace common;
namespace rpc {

observer::ObSMConnection *
ObSqlRequestOperator::get_sql_session(ObRequest *req) {
  obmysql::ObSqlSockSession *sess = static_cast<obmysql::ObSqlSockSession *>(
      req->get_server_handle_context());
  return &sess->conn_;
}

nio_connection_handle *
ObSqlRequestOperator::get_nio_connection_handle(const ObRequest *req) {
  obmysql::ObSqlSockSession *sess = static_cast<obmysql::ObSqlSockSession *>(
      req->get_server_handle_context());
  return sess->get_nio_connection_handle();
}

ObAddr ObSqlRequestOperator::get_peer(const ObRequest *req) {
  obmysql::ObSqlSockSession *sess = static_cast<obmysql::ObSqlSockSession *>(
      req->get_server_handle_context());
  return sess->client_addr_;
}

int ObSqlRequestOperator::disconnect_sql_conn(ObRequest *req,
                                              uint64_t generation) {
  obmysql::ObSqlSockSession *sess =
      static_cast<obmysql::ObSqlSockSession *>(req->get_server_handle_context());
  return sess->set_shutdown(generation);
}

int ObSqlRequestOperator::finish_sql_request(ObRequest *req,
                                             uint64_t generation) {
  obmysql::ObSqlSockSession *sess =
      static_cast<obmysql::ObSqlSockSession *>(req->get_server_handle_context());
  const int ret = sess->prepare_request_commit(generation);
  if (OB_SUCCESS == ret) {
    req->set_trace_point(ObRequest::OB_REQUEST_FINISH_SQL);
    request_finish_callback();
    sess->commit_request(generation);
  } else {
    RPC_LOG(WARN, "ignore stale or duplicate sql request finish", K(ret),
            K(generation));
  }
  return ret;
}

static int materialize_read_packet(obmysql::ObSqlSockSession *sess,
                                   obmysql::ObICSMemPool &mem_pool,
                                   uint64_t generation,
                                   int rc,
                                   char *body,
                                   int64_t body_len,
                                   uint64_t rust_packet_lease,
                                   ObPacket *&pkt,
                                   uint64_t &packet_lease)
{
  int ret = OB_SUCCESS;
  pkt = NULL;
  packet_lease = 0;
  if (rc < 0) {
    ret = OB_IO_ERROR;
  } else if (rc > 0) {
    obmysql::ObMySQLRawPacket *raw = NULL;
    const obmysql::ObMySQLRawPacketMode mode = sess->conn_.is_in_auth_switch_phase()
        ? obmysql::ObMySQLRawPacketMode::AUTH_SWITCH_RESPONSE
        : obmysql::ObMySQLRawPacketMode::LOCAL_INFILE_DATA;
    if (OB_UNLIKELY(0 == rust_packet_lease)) {
      ret = OB_ERR_UNEXPECTED;
      RPC_LOG(WARN, "mid-request mysql packet has no Rust lease", K(ret),
              K(body_len));
    } else if (OB_FAIL(obmysql::build_mysql_raw_packet_view(
                   mem_pool, body, body_len, 0, mode, NULL, raw))) {
      RPC_LOG(WARN, "build mid-request mysql packet fail", K(ret), K(body_len));
      const int release_ret = nio_release_read_packet(sess, generation, rust_packet_lease);
      if (0 != release_ret) {
        RPC_LOG(WARN, "release mid-request mysql packet after build failure",
                K(release_ret), K(rust_packet_lease));
      }
    } else {
      pkt = raw;
      packet_lease = rust_packet_lease;
    }
  }
  return ret;
}

int ObSqlRequestOperator::wait_packet(ObRequest *req,
                                      obmysql::ObICSMemPool &mem_pool,
                                      uint64_t generation,
                                      int64_t timeout_us,
                                      ObPacket *&pkt,
                                      uint64_t &packet_lease)
{
  obmysql::ObSqlSockSession *sess =
      static_cast<obmysql::ObSqlSockSession *>(req->get_server_handle_context());
  char *body = NULL;
  int64_t body_len = 0;
  uint64_t rust_packet_lease = 0;
  const int rc = nio_wait_one_packet(sess, generation, timeout_us, &body,
                                     &body_len, &rust_packet_lease);
  return materialize_read_packet(sess, mem_pool, generation, rc, body,
                                 body_len, rust_packet_lease, pkt,
                                 packet_lease);
}

int ObSqlRequestOperator::release_read_packet(ObRequest *req,
                                               uint64_t generation,
                                               uint64_t packet_lease)
{
  int ret = OB_SUCCESS;
  obmysql::ObSqlSockSession *sess =
      static_cast<obmysql::ObSqlSockSession *>(req->get_server_handle_context());
  if (0 == packet_lease) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 != nio_release_read_packet(sess, generation, packet_lease)) {
    ret = OB_STATE_NOT_MATCH;
  }
  return ret;
}

void ObSqlRequestOperator::get_sock_desc(ObRequest *req, ObSqlSockDesc &desc)
{
  desc.set(req->get_server_handle_context());
}

void ObSqlRequestOperator::disconnect_by_sql_sock_desc(ObSqlSockDesc &desc)
{
  desc.sock_desc_->shutdown();
}

void ObSqlRequestOperator::interrupt_read_by_sql_sock_desc(ObSqlSockDesc &desc)
{
  if (NULL != desc.sock_desc_) {
    (void)nio_interrupt_read(desc.sock_desc_, 0);
  }
}

void ObSqlRequestOperator::bind_sql_session(ObRequest *req)
{
  static_cast<obmysql::ObSqlSockSession *>(req->get_server_handle_context())
      ->bind_sql_session();
}

void ObSqlSockDesc::clear_sql_session_info()
{
  if (NULL != sock_desc_) {
    sock_desc_->clear_sql_session_info();
  }
}

ObSqlRequestOperator global_sql_req_operator;
} // namespace rpc
} // namespace oceanbase
