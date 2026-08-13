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
#include "rpc/obmysql/ob_sql_sock_session.h"
#include "nio.h"

namespace oceanbase
{
using namespace common;
using namespace rpc;
using namespace observer;
namespace obmysql
{

ObSqlSockSession::ObSqlSockSession(ObISMConnectionCallback &conn_cb)
    : sm_conn_cb_(conn_cb), sql_req_(ObRequest::OB_MYSQL), fd_(-1),
      sql_session_id_(0), nio_connection_handle_(NULL) {
  sql_req_.set_server_handle_context(this);
}

int ObSqlSockSession::init() {
  int ret = sm_conn_cb_.init(*this, conn_);
  if (OB_SUCC(ret) &&
      OB_ISNULL(nio_connection_handle_ = nio_connection_handle_acquire(this))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to acquire Rust SQL-NIO connection handle", K(ret));
  }
  return ret;
}

void ObSqlSockSession::destroy() {
  // Clear the published pointer first so an accidental re-entry cannot release
  // the opaque Rust owner twice. Append/flush/prepare cannot overlap release;
  // commit pins Conn before clearing BUSY, so its remaining tail may overlap.
  nio_connection_handle *connection_handle = nio_connection_handle_;
  nio_connection_handle_ = NULL;
  sm_conn_cb_.destroy(conn_);
  pool_.reset();
  nio_connection_handle_release(connection_handle);
}

int ObSqlSockSession::on_disconnect() {
  return sm_conn_cb_.on_disconnect(conn_);
}

int ObSqlSockSession::set_shutdown(uint64_t generation) {
  int ret = OB_SUCCESS;
  if (0 != nio_set_shutdown((void *)this, generation)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN_RET(OB_STATE_NOT_MATCH, "ignore stale sql request shutdown",
                 K(generation));
  }
  return ret;
}

void ObSqlSockSession::shutdown() { return nio_shutdown((void *)this); }

int ObSqlSockSession::prepare_request_commit(uint64_t generation) {
  int ret = OB_SUCCESS;
  // ABI 22: report what this request DID to the auth state; Rust owns the
  // wire phase machine and derives the next read state itself. The C++
  // connection phase remains a C++-side concept and is no longer transmitted.
  int auth_outcome = NIO_AUTH_NO_CHANGE;
  switch (conn_.connection_phase_) {
  case rpc::ConnectionPhaseEnum::CPE_CONNECTED:
    auth_outcome = NIO_AUTH_NO_CHANGE;
    break;
  case rpc::ConnectionPhaseEnum::CPE_AUTHED:
    auth_outcome = NIO_AUTH_OK;
    break;
  case rpc::ConnectionPhaseEnum::CPE_AUTH_SWITCH:
    auth_outcome = NIO_AUTH_SWITCH_SENT;
    break;
  default:
    // Rust accepts the invalid outcome only to let this current owner release
    // its pool, then closes instead of decoding another packet.
    auth_outcome = -1;
    break;
  }
  if (0 != nio_prepare_commit(nio_connection_handle_, generation, auth_outcome)) {
    ret = OB_STATE_NOT_MATCH;
  }
  return ret;
}

void ObSqlSockSession::commit_request(uint64_t generation) {
  sql_req_.reset_trace_id();
  // nio_prepare_commit granted this generation exclusive finish ownership. A
  // staged response already belongs to Rust, so pool reuse is now safe.
  pool_.reuse();
  nio_commit_request(nio_connection_handle_, generation);
}

int ObSqlSockSession::clear_sql_session_info() {
  return 0 == nio_release_sql_session(this) ? common::OB_SUCCESS
                                            : common::OB_STATE_NOT_MATCH;
}
void ObSqlSockSession::bind_sql_session() { nio_bind_sql_session(this); }

}; // end namespace obmysql
}; // end namespace oceanbase
