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
#include "obmp_stmt_close.h"
#include "lib/trace/ob_trace.h"
#include "observer/omt/ob_server_runtime.h"

namespace oceanbase
{
using namespace common;
using namespace rpc;
using namespace obmysql;
using namespace sql;

namespace observer
{

int ObMPStmtClose::deserialize()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid packet", K(ret), K_(req));
  } else if (OB_UNLIKELY(req_->get_type() != ObRequest::OB_MYSQL)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid packet", K(ret), K_(req), K(req_->get_type()));
  } else {
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    if (OB_UNLIKELY(ObMySQLCommandLayout::U32 != pkt.get_command_layout())) {
      ret = OB_INVALID_DATA;
      LOG_WARN("unexpected stmt-close command layout", K(ret),
               K(pkt.get_command_layout()));
    } else {
      stmt_id_ = static_cast<uint32_t>(pkt.get_command_scalar0());
    }
  }
  return ret;
}

int ObMPStmtClose::process()
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = NULL;
  trace::UUID ps_close_span_id;
  if (OB_ISNULL(req_) || OB_ISNULL(get_conn())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid packet", K(ret), KP(req_));
  } else if (OB_INVALID_STMT_ID == stmt_id_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("stmt_id is invalid", K(ret));
  } else if (OB_FAIL(get_session(session))) {
    LOG_WARN("get session failed");
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL or invalid", K(ret), K(session));
  } else {
    ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
    LOG_TRACE("close ps stmt or cursor", K_(stmt_id), K(session->get_server_sid()));
    if (is_cursor_close()) {
      if (OB_FAIL(session->close_cursor(stmt_id_))) {
        LOG_WARN("fail to close cursor", K(ret), K_(stmt_id), K(session->get_server_sid()));
      }
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_NOT_NULL(session->get_cursor(stmt_id_))) {
        if (OB_FAIL(session->close_cursor(stmt_id_))) {
          tmp_ret = ret;
          LOG_WARN("fail to close cursor", K(ret), K_(stmt_id), K(session->get_server_sid()));
        }
      }
      if (OB_FAIL(session->close_ps_stmt(stmt_id_))) {
        // overwrite ret, low priority, will be overridden
        LOG_WARN("fail to close ps stmt", K(ret), K_(stmt_id), K(session->get_server_sid()));
      }
      if (OB_SUCCESS != tmp_ret) {
        // close_cursor failure error code priority is higher than close_ps_stmt, here we override
        ret = tmp_ret;
      }
    }
  }
  if (NULL != session) {
    revert_session(session);
  }
  return ret;
}

} //end of namespace sql
} //end of namespace oceanbase
