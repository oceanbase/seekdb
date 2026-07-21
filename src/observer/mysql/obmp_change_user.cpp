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
#include "sql/ob_sql.h"
#include "rpc/obmysql/packet/ompk_auth_switch.h"
#include "observer/mysql/obmp_stmt_send_piece_data.h"


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
    ObSQLSessionInfo *session = NULL;
    ObMySQLCapabilityFlags capability;
    if (OB_FAIL(get_session(session))) {
      LOG_WARN("get session  fail", K(ret));
    } else if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("fail to get session info", K(ret), K(session));
    } else {
      ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
      session->update_last_active_time();
      capability = session->get_capability();
    }
    if (NULL != session) {
      revert_session(session);
    }
    if (OB_SUCC(ret)) {
      pkt_  = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
      const char *buf = pkt_.get_cdata();
      const char *pos = pkt_.get_cdata();
      // need skip command byte
      const int64_t len = pkt_.get_clen() - 1;
      const char *end = buf + len;

      if (OB_LIKELY(pos < end)) {
        username_.assign_ptr(pos, static_cast<int32_t>(STRLEN(pos)));
        pos += username_.length() + 1;
      }

      if (OB_LIKELY(pos < end)) {
        if (capability.cap_flags_.OB_CLIENT_SECURE_CONNECTION) {
          uint8_t auth_response_len = 0;
          ObMySQLUtil::get_uint1(pos, auth_response_len);
          auth_response_.assign_ptr(pos, static_cast<int32_t>(auth_response_len));
          pos += auth_response_len;
        } else {
          auth_response_.assign_ptr(pos, static_cast<int32_t>(STRLEN(pos)));
          pos += auth_response_.length() + 1;
        }
      }

      if (OB_LIKELY(pos < end)) {
        database_.assign_ptr(pos, static_cast<int32_t>(STRLEN(pos)));
        pos += database_.length() + 1;
      }

      if (OB_LIKELY(pos < end)) {
        ObMySQLUtil::get_uint2(pos, charset_);
      }

      if (OB_LIKELY(pos < end)) {
        if (capability.cap_flags_.OB_CLIENT_PLUGIN_AUTH) {
          auth_plugin_name_.assign_ptr(pos, static_cast<int32_t>(STRLEN(pos)));
          pos += auth_plugin_name_.length() + 1;
        }
      }

      if (OB_LIKELY(pos < end)) {
        if (capability.cap_flags_.OB_CLIENT_CONNECT_ATTRS) {
          uint64_t all_attrs_len = 0;
          const char *attrs_end = NULL;
          if (OB_FAIL(ObMySQLUtil::get_length(pos, all_attrs_len))) {
            LOG_WARN("fail to get all_attrs_len", K(ret));
          } else {
            attrs_end = pos + all_attrs_len;
          }
          ObStringKV str_kv;
          while(OB_SUCC(ret) && OB_LIKELY(pos < attrs_end)) {
            if (OB_FAIL(decode_string_kv(attrs_end, pos, str_kv))) {
              OB_LOG(WARN, "fail to decode string kv", K(ret));
            }
          }
        } // end connect attrs
      } // end if
    }
  }
  return ret;
}

int ObMPChangeUser::decode_string_kv(const char *attrs_end, const char *&pos, ObStringKV &kv)
{
  int ret = OB_SUCCESS;
  uint64_t key_len = 0;
  uint64_t value_len = 0;
  if (OB_ISNULL(pos)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalie input value", K(pos), K(ret));
  } else {
    if (OB_FAIL(ObMySQLUtil::get_length(pos, key_len))) {
      OB_LOG(WARN, "fail t get key len", K(pos), K(ret));
    } else if (pos + key_len >= attrs_end) {
      // skip this value
      pos = attrs_end;
    } else {
      kv.key_.assign_ptr(pos, static_cast<uint32_t>(key_len));
      pos += key_len;
      if (OB_FAIL(ObMySQLUtil::get_length(pos, value_len))) {
        OB_LOG(WARN, "fail t get value len", K(pos), K(ret));
      } else {
        kv.value_.assign_ptr(pos, static_cast<uint32_t>(value_len));
        pos += value_len;
      }
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
  const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
  int64_t query_timeout = 0;
  bool need_send_auth_switch =
      get_conn()->is_support_plugin_auth() &&
      GCONF._enable_auth_switch;
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
    get_conn()->client_cs_type_ = charset_;
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
      if (OB_FAIL(packet_sender_.response_packet(auth_switch, session))) {
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
    if (OB_FAIL(session->close_all_ps_stmt())) {
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
      observer::ObPieceCache* piece_cache = 
        static_cast<observer::ObPieceCache*>(session->get_piece_cache());
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

      // With auth switch enabled, privilege verification completes in
      // ObMPAuthResponse. That handler clears the cache after authentication.
      if (!need_send_auth_switch) {
        session->reset_sql_plan_cache();
      }
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
  const char *sep_pos = username_.find('@');
  if (NULL != sep_pos) {
    ObString username(sep_pos - username_.ptr(), username_.ptr());
    login_info.user_name_ = username;
    login_info.tenant_name_ = username_.after(sep_pos);
    if (login_info.tenant_name_ != session->get_tenant_name()) {
      ret = OB_OP_NOT_ALLOW;
      OB_LOG(WARN, "failed to change user in different tenant", K(ret),
          K(login_info.tenant_name_), K(session->get_tenant_name()));
      LOG_USER_ERROR(OB_OP_NOT_ALLOW, "forbid! change user command in differernt tenant");
    }
  } else {
    login_info.user_name_ = username_;
  }
  if (OB_SUCC(ret)) {
    if (login_info.tenant_name_.empty()) {
      login_info.tenant_name_ = session->get_tenant_name();
    }
    if (!database_.empty()) {
      login_info.db_ = database_;
    }
    login_info.client_ip_ = session->get_client_ip();
    OB_LOG(INFO, "com change user", "username", login_info.user_name_,
          "tenant name", login_info.tenant_name_);
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
