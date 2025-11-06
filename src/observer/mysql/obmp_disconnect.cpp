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

#include "obmp_disconnect.h"


using namespace oceanbase::observer;
using namespace oceanbase::common;

void __attribute__((weak)) request_finish_callback();

ObMPDisconnect::ObMPDisconnect(const sql::ObFreeSessionCtx &ctx)
    : ctx_(ctx)
{
}

ObMPDisconnect::~ObMPDisconnect()
{

}

int ObMPDisconnect::kill_unfinished_session(uint32_t sessid)
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = NULL;
  sql::ObSessionGetterGuard guard(*GCTX.session_mgr_, sessid);
  if (OB_FAIL(guard.get_session(session))) {
    LOG_WARN("get session fail", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("fail to get session info", K(session), K(sessid), K(ret));
  } else {
    /* NOTE:
     * In the context of Disconnect, there are two possibilities:
     * (1) The long SQL is executed first and is currently running, so it can already detect the IS_KILLED flag
     *     At this point, disconnect_session will wait for the long SQL to exit before ending the transaction in the session
     * (2) disconnect_session is executed first, it will end the transaction and return, then execute the subsequent free_session
     *     (free_session does not release session memory, it is just a logical deletion action).
     *     When the long SQL acquires the query_lock lock, it will immediately check the IS_KILLED status, and exit the processing flow upon detection.
     *     Ultimately, the reference count is reduced to 0, and the session is physically recycled.
     */
    if (OB_FAIL(GCTX.session_mgr_->disconnect_session(*session))) {
      LOG_WARN("fail to disconnect session", K(session), K(sessid), K(ret));
    }
  }
  return ret;
}

int ObMPDisconnect::run()
{
  int ret = OB_SUCCESS;
  bool is_need_clear = false;
  if (ctx_.sessid_ != 0) {
    if (OB_ISNULL(GCTX.session_mgr_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid session mgr", K(GCTX.session_mgr_), K(ret));
    } else {
      ObSMConnection conn;
      conn.sessid_ = ctx_.sessid_;
      conn.is_need_clear_sessid_ = true;
      // bugfix: 
      (void) kill_unfinished_session(ctx_.sessid_); // ignore ret
      if (OB_FAIL(GCTX.session_mgr_->free_session(ctx_))) {
        LOG_WARN("free session fail", K(ctx_));
      } else {
        common::ObTenantDiagnosticInfoSummaryGuard guard(ctx_.tenant_id_);
        EVENT_INC(SQL_USER_LOGOUTS_CUMULATIVE);
        LOG_INFO("free session successfully", "sessid", ctx_.sessid_);
        if (OB_UNLIKELY(OB_FAIL(sql::ObSQLSessionMgr::is_need_clear_sessid(&conn, is_need_clear)))) {
          LOG_ERROR("fail to judge need clear", K(ret), "sessid", conn.sessid_);
        } else if (is_need_clear) {
          if (OB_FAIL(GCTX.session_mgr_->mark_sessid_unused(conn.sessid_))) {
            LOG_WARN("mark session id unused failed", K(ret), "sessid", conn.sessid_);
          } else {
            LOG_INFO("mark session id unused", "sessid", conn.sessid_);
          }
        }
      }
    }
  }
  request_finish_callback();
  return ret;
}
