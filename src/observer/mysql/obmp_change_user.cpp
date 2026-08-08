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
#include "observer/mysql/obmp_change_user.h"
#include "observer/ob_server_runtime_access.h"
#include "sql/ob_sql.h"
#include "rpc/obmysql/packet/ompk_auth_switch.h"
#include "sql/session/ob_piece_cache.h"


using namespace oceanbase::common;
using namespace oceanbase::rpc;
using namespace oceanbase::obmysql;
using namespace oceanbase::share::schema;
namespace oceanbase
{
namespace observer
{
const char *AUTH_PLUGIN_MYSQL_NATIVE_PASSWORD = "mysql_native_password";
int ObMPChangeUser::deserialize()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req_) || OB_UNLIKELY(ObRequest::OB_MYSQL != req_->get_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid request", K(req_));
  } else {
    const ObMySQLRawPacket &pkt =
        reinterpret_cast<const ObMySQLRawPacket &>(req_->get_packet());
    const int64_t charset = pkt.get_command_scalar0();
    if (ObMySQLCommandLayout::CHANGE_USER != pkt.get_command_layout()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("change-user command view has wrong layout", K(ret), "layout",
                static_cast<uint32_t>(pkt.get_command_layout()));
    } else if (OB_FAIL(pkt.get_command_field(0, username_))) {
      LOG_WARN("get change-user username failed", K(ret));
    } else if (OB_FAIL(pkt.get_command_field(1, auth_response_))) {
      LOG_WARN("get change-user auth response failed", K(ret));
    } else if (OB_FAIL(pkt.get_command_field(2, database_))) {
      LOG_WARN("get change-user database failed", K(ret));
    } else if (charset < -1 || charset > UINT16_MAX) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("change-user charset is outside typed ABI", K(ret), K(charset));
    } else {
      has_charset_ = charset >= 0;
      charset_ = has_charset_ ? static_cast<uint16_t>(charset) : 0;
    }
  }
  return ret;
}

int ObMPChangeUser::process()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  bool need_disconnect = true;
  bool need_response_error = true;
  int64_t query_timeout = 0;
  bool need_send_auth_switch =
      get_conn()->is_support_plugin_auth();
  if (OB_FAIL(get_session(session))) {
    LOG_ERROR("get session  fail", K(ret));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("fail to get session info", K(ret), K(session));
  } else if (OB_FAIL(session->get_query_timeout(query_timeout))) {
    LOG_WARN("fail to get query timeout", K(ret));
  } else if (FALSE_IT(THIS_WORKER.set_timeout_ts(get_receive_timestamp() + query_timeout))) {
  } else {
    need_disconnect = false;
    if (has_charset_) {
      get_conn()->client_cs_type_ = charset_;
    }
    ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
    session->update_last_active_time();
    if (OB_FAIL(ObSqlTransControl::rollback_trans(session, need_disconnect))) {
      OB_LOG(WARN, "fail to rollback trans for change user", K(ret), K(session));
    } else {
      session->clean_status();
      if (OB_FAIL(load_login_info(session))) {
        OB_LOG(WARN,"load log info failed", K(ret),K(session->get_server_sid()));
      } else if (need_send_auth_switch) {
        // do nothing
      } else if (OB_FAIL(load_privilege_info_for_change_user(session))) {
        OB_LOG(WARN,"load privilige info failed", K(ret),K(session->get_server_sid()));
      }
    }
  }

  //send packet to client
  if (OB_SUCC(ret)) {
    /*
     In order to be compatible with the behavior of mysql change user, 
     an AuthSwitchRequest request will be sent every time to the external client.

     If we're dealing with an older client we can't just send a change plugin
     packet to re-initiate the authentication handshake, because the client
     won't understand it. The good thing is that we don't need to : the old
     client expects us to just check the user credentials here, which we can do
     by just reading the cached data that are placed there by change user's 
     passwd field.
     * */
    if (need_send_auth_switch) {
      // send auth switch request
      OMPKAuthSwitch auth_switch;
      auth_switch.set_plugin_name(ObString(AUTH_PLUGIN_MYSQL_NATIVE_PASSWORD));
      auth_switch.set_scramble(ObString(sizeof(get_conn()->scramble_buf_), get_conn()->scramble_buf_));
      if (OB_FAIL(packet_sender_.response_packet(auth_switch))) {
        RPC_LOG(WARN, "failed to send error packet", K(auth_switch), K(ret));
        disconnect();
      } else {
        get_conn()->set_auth_switch_phase();
      }
    } else {
      ObOKPParam ok_param;
      ok_param.is_on_change_user_ = true;
      if (OB_FAIL(send_ok_packet(*session, ok_param))) {
        OB_LOG(WARN, "response ok packet fail", K(ret));
      }
    }
  } else if (need_response_error) {
    if (OB_FAIL(send_error_packet(ret, NULL))) {
      OB_LOG(WARN,"response fail packet fail", K(ret));
    }
    need_disconnect = true;
  }

  // Releases prepared statements. (include ps stmt, ps cursor, piece)
  if (OB_SUCC(ret)) {
    // 1 ps stmt
    if (OB_FAIL(session->close_all_ps_stmt(
            get_observer_sql_engine()->get_ps_cache()))) {
      LOG_WARN("failed to close all stmt", K(ret));
    }

    // 2 ps cursor
    if (OB_SUCC(ret) && session->get_cursor_cache().is_inited()) {
      if (OB_FAIL(session->get_cursor_cache().close_all(*session))) {
        LOG_WARN("failed to close all cursor", K(ret));
      } else {
        session->get_cursor_cache().reset();
      }
    }

    // 3 piece
    if (OB_SUCC(ret) && NULL != session->get_piece_cache()) {
      sql::ObPieceCache* piece_cache =
        static_cast<sql::ObPieceCache*>(session->get_piece_cache());
      if (OB_FAIL(piece_cache->close_all(*session))) {
        LOG_WARN("failed to close all piece", K(ret));
      }
      piece_cache->reset();
      session->get_session_allocator().free(session->get_piece_cache());
      session->set_piece_cache(NULL);
    }

    if (OB_SUCC(ret)) {
      // 4 ps session info 
      session->reset_ps_session_info();

      // 5 ps name
      session->reset_ps_name();
    }
  }

  if (OB_UNLIKELY(need_disconnect) && is_conn_valid()) {
    if (OB_ISNULL(session)) {
      // ignore ret
      LOG_WARN("will disconnect connection", K(ret), K(session));
    } else {
      LOG_WARN("will disconnect connection", K(ret), KPC(session));
    }
    force_disconnect();
  }

  if (session != NULL) {
    revert_session(session);
  }
  return ret;
}
int ObMPChangeUser::load_login_info(ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  share::schema::ObUserLoginInfo login_info;
  login_info.user_name_ = username_;
  login_info.runtime_name_ = session->get_runtime_name();
  if (OB_SUCC(ret)) {
    if (!database_.empty()) {
      login_info.db_ = database_;
    }
    login_info.client_ip_ = session->get_client_ip();
    OB_LOG(INFO, "com change user", "username", login_info.user_name_,
          "runtime name", login_info.runtime_name_);
    const ObSMConnection &conn = *get_conn();
    login_info.scramble_str_.assign_ptr(conn.scramble_buf_, static_cast<ObString::obstr_size_t>(sizeof(conn.scramble_buf_)));
    login_info.passwd_ = auth_response_;
    if (OB_FAIL(session->set_login_info(login_info))) {
      LOG_WARN("failed to set login_info", K(ret));
    } else if (OB_FAIL(session->set_default_database(database_))) {
      OB_LOG(WARN, "failed to set default database", K(ret), K(database_));
    }
  }
  return ret;
}

} //namespace observer
} //namespace oceanbase
