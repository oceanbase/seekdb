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

#include "observer/mysql/obmp_connect.h"
#include "rpc/ob_sql_request_operator.h"
#include "observer/ob_server.h"
#include "observer/omt/ob_server_runtime.h"
#include "storage/tx/ob_weak_read_util.h"      //ObWeakReadUtil
#include "sql/privilege_check/ob_privilege_check.h"
#include "rpc/obmysql/packet/ompk_auth_switch.h"
#include "sql/engine/dml/ob_trigger_handler.h"
#include "observer/mysql/ob_mysql_result_set.h"
#include "observer/ob_service.h"
#include "share/ob_version_parser.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::obmysql;
using namespace oceanbase::observer;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace observer
{
ObString extract_user_name(const ObString &in)
{
  ObString user_name = in;
  // Keep the historical @sys spelling as a login alias while tenant routing
  // is no longer supported. Other suffixes are authenticated as-is.
  static const char *const SYS_TENANT_SUFFIX = "@sys";
  if (user_name.suffix_match(SYS_TENANT_SUFFIX)) {
    user_name.assign_ptr(user_name.ptr(),
                         user_name.length() - static_cast<int32_t>(STRLEN(SYS_TENANT_SUFFIX)));
  }
  if (user_name.length() > 1 && '\'' == user_name[0] && '\'' == user_name[user_name.length() - 1]) {
    user_name.assign_ptr(user_name.ptr() + 1, user_name.length() - 2);
  }
  return user_name;
}

}  // namespace observer
}  // namespace oceanbase

ObMPConnect::ObMPConnect(const ObGlobalContext &gctx)
    : ObMPBase(gctx),
      user_name_(),
      client_ip_(),
      db_name_(),
      deser_ret_(OB_SUCCESS),
      allocator_(ObModIds::OB_SQL_REQUEST),
      asr_mem_pool_(&allocator_)
{
  client_ip_buf_[0] = '\0';
  user_name_var_[0] = '\0';
  db_name_var_[0] = '\0';
}

ObMPConnect::~ObMPConnect()
{

}

int ObMPConnect::deserialize()
{
  int ret = OB_SUCCESS;

  ObSMConnection *conn = get_conn();
  //OB_ASSERT(conn);
  if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid conn", K(ret), K(conn));
  } else if (OB_ISNULL(req_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid req_", K(ret), K(req_));
  } else {
    // Rust owns the login parse now; the request carries the raw login body and
    // the view maps Rust's offsets onto it (server_handle_context is the session
    // handle the nio_login_* getters are keyed by).
    const obmysql::ObMySQLRawPacket &login_pkt =
        reinterpret_cast<const obmysql::ObMySQLRawPacket&>(req_->get_packet());
    const uint64_t generation = req_->get_nio_request_generation();
    if (OB_FAIL(hsr_.load(req_->get_server_handle_context(), generation,
                          login_pkt.get_cdata(), login_pkt.get_clen()))) {
      LOG_WARN("load login view fail", K(ret));
    } else {
      conn->cap_flags_ = hsr_.get_capability_flags();
      conn->client_cs_type_ = hsr_.get_char_set();
      db_name_ = hsr_.get_database();
      user_name_ = extract_user_name(hsr_.get_username());
      LOG_DEBUG("database name", K(hsr_.get_database()));
    }
    // get_user_tenant() is an earlier consumer of the same Rust login view and
    // ObMPConnect is the final one, but neither releases it: nio_commit_request
    // frees unclaimed login metadata when the request completes, making commit
    // the single owner instead of a comment-enforced "last consumer releases"
    // protocol spread across three files.

    deser_ret_ = ret;  // record deserialize ret code.
    ret = OB_SUCCESS;  // return OB_SUCCESS anyway.
  }
  return ret;
}

int ObMPConnect::init_process_single_stmt(const ObMultiStmtItem &multi_stmt_item,
                                          ObSQLSessionInfo &session,
                                          bool has_more_result) const
{
  int ret = OB_SUCCESS;
  const ObString &sql = multi_stmt_item.get_sql();
  ObVirtualTableIteratorFactory vt_iter_factory(*gctx_.vt_iter_creator_);
  ObSchemaGetterGuard schema_guard;
  // init_connect can execute query and dml statements, must add req_timeinfo_guard
  observer::ObReqTimeGuard req_timeinfo_guard;
  //Do not change the order of SqlCtx and Allocator. ObSqlCtx uses the resultset's allocator to
  //allocate memory for ObSqlCtx::base_constraints_. The allocator must be deconstructed after sqlctx. 
  ObArenaAllocator allocator(ObModIds::OB_SQL_SESSION);
  ObSqlCtx ctx;
  ctx.exec_type_ = MpQuery;
  if (OB_FAIL(init_process_var(ctx, multi_stmt_item, session))) {
    LOG_WARN("init process var failed.", K(ret), K(multi_stmt_item));
  } else if (OB_FAIL(gctx_.schema_service_->get_runtime_schema_guard(
                                  schema_guard))) {
    LOG_WARN("get schema guard failed.", K(ret));
  } else if (OB_FAIL(set_session_active(sql, session, ObTimeUtil::current_time()))) {
    LOG_WARN("fail to set session active", K(ret));
  } else {
    //set session log_level.Must use ObThreadLogLevelUtils::clear() in pair
    ObThreadLogLevelUtils::init(session.get_log_id_level_map());
    ctx.retry_times_ = 0; // This is the initialization SQL execution when establishing a connection, no retry
    ctx.schema_guard_ = &schema_guard;
    HEAP_VAR(ObMySQLResultSet, result, session, allocator) {
      result.set_has_more_result(has_more_result);
      if (OB_FAIL(result.init())) {
        LOG_WARN("result set init failed");
      } else if (OB_ISNULL(gctx_.sql_engine_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid sql engine", K(ret), K(gctx_));
      } else if (OB_FAIL(gctx_.sql_engine_->stmt_query(sql, ctx, result))) {
        LOG_WARN("sql execute failed", K(multi_stmt_item), K(sql), K(ret));
      } else {
        int open_ret = result.open();
        if (open_ret) {
          LOG_WARN("failed to do result set open", K(open_ret));
        }
        if (OB_FAIL(result.close())) {
          LOG_WARN("result close failed, disconnect.", K(ret));
        }
        ret = (open_ret != OB_SUCCESS) ? open_ret : ret;
      }
      ObThreadLogLevelUtils::clear();
    }
    //For the handling of tracelog, it does not affect the normal logic, and the error code does not need to be assigned to ret
    int tmp_ret = OB_SUCCESS;
    tmp_ret = do_after_process(session, false, ret); // not asynchronous response
    UNUSED(tmp_ret);
  }
  return ret;
}

int ObMPConnect::init_connect_process(ObString &init_sql,
                                      ObSQLSessionInfo &session) const
{
  int ret = OB_SUCCESS;
  ObSEArray<ObString, 4> queries;
  ObArenaAllocator allocator(ObModIds::OB_SQL_PARSER);
  ObParser parser(allocator, session.get_sql_mode(), session.get_charsets4parser());
  ObMPParseStat parse_stat;
  if (OB_SUCC(parser.split_multiple_stmt(init_sql, queries, parse_stat))) {
    if (OB_UNLIKELY(0 == queries.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("empty query!", K(ret), K(init_sql));
    }
    bool has_more;
    ARRAY_FOREACH(queries, i) {
      has_more = (queries.count() > i + 1);
      if (OB_FAIL(init_process_single_stmt(ObMultiStmtItem(true, i, queries[i]), session, has_more))) {
        LOG_WARN("process single stmt failed!", K(ret), K(queries[i]));
      }
    }
  } else {
    LOG_WARN("split multiple stmt failed!", K(ret));
  }
  return ret;
}

int ObMPConnect::process()
{
  int ret = deser_ret_;
  ObSMConnection *conn = NULL;
  ObSQLSessionInfo *session = NULL;
  bool autocommit = false;
  THIS_WORKER.set_timeout_ts(INT64_MAX); // avoid see a former timeout value
  if (THE_TRACE != nullptr) {
    THE_TRACE->reset();
  }
  if (OB_FAIL(ret)) {
    LOG_ERROR("deserialize failed", K(ret));
  } else if (OB_ISNULL(req_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("null ez_req", K(ret));
  } else if (OB_ISNULL(conn = get_conn())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("null conn", K(ret));
  } else if (OB_ISNULL(GCTX.session_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("session mgr is NULL", K(ret));
  } else if (OB_FAIL(conn->ret_)) {
    LOG_WARN("connection fail at obsm_handle process", K(conn->ret_));
  } else {
    if (SS_STOPPING == GCTX.status_) {
      ret = OB_SERVER_IS_STOPPING;
      LOG_WARN("server is stopping", K(ret));
    } else if (OB_FAIL(share::check_server_runtime_ready())) {
      LOG_WARN("server runtime is not ready", K(ret));
    } else if (OB_FAIL(check_client_property(*conn))) {
      LOG_WARN("check_client_property fail", K(ret));
    } else if (OB_FAIL(verify_connection())) {
      LOG_WARN("verify connection fail", K(ret));
    } else if (OB_FAIL(create_session(conn, session))) {
      LOG_WARN("alloc session fail", K(ret));
    } else if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("null session", K(ret), K(session));
    } else if (OB_FAIL(verify_identify(*conn, *session))) {
      LOG_WARN("fail to verify_identify", K(ret));
    } else if (OB_FAIL(update_charset_sys_vars(*conn, *session))) {
      LOG_WARN("fail to update charset sys vars", K(ret));
    } else {
      // set connection info to session
      LOG_TRACE("setup user session OK", "user_id", session->get_user_id(), K(user_name_));
      conn->set_auth_phase();
      conn->set_logined(true);
      session->get_autocommit(autocommit);
    }

    int proc_ret = ret;
    char client_ip_buf[OB_IP_STR_BUFF] = {};
    if (!get_peer().ip_to_string(client_ip_buf, OB_IP_STR_BUFF)) {
      LOG_WARN("fail to ip to string");
      snprintf(client_ip_buf, OB_IP_STR_BUFF, "xxx.xxx.xxx.xxx");
    }
    char host_name_buf[OB_IP_STR_BUFF] = {};
    if (NULL != session && !session->get_client_ip().empty()) {
      session->get_host_name().to_string(host_name_buf, OB_IP_STR_BUFF);
    } else {
      snprintf(host_name_buf, OB_IP_STR_BUFF, "xxx.xxx.xxx.xxx");
    }
    const ObString host_name(host_name_buf);
    const ObCSProtocolType protoType = conn->get_cs_protocol_type();
    const uint32_t sessid = conn->sessid_;
    const uint32_t capability = conn->cap_flags_.capability_;

    if (OB_SUCC(proc_ret)) {
      // send packet for client
      ObOKPParam ok_param;
      ok_param.is_on_connect_ = true;
      ok_param.affected_rows_ = 0;
      const int login_warning_buf_len = 50;
      char login_warning[login_warning_buf_len];
      int tmp_ret = OB_SUCCESS;
      login_warning[0] = '\0';
      ok_param.message_ = login_warning;
      if (OB_FAIL(send_ok_packet(*session, ok_param))) {
        LOG_WARN("fail to send ok packet", K(ok_param), K(ret));
      }
    } else {
      char buf[OB_MAX_ERROR_MSG_LEN];
      switch (proc_ret) {
        case OB_PASSWORD_WRONG: {
          ret = OB_PASSWORD_WRONG;
          snprintf(buf, OB_MAX_ERROR_MSG_LEN, ob_errpkt_str_user_error(ret),
                   user_name_.length(), user_name_.ptr(),
                   host_name.length(), host_name.ptr(),
                   (hsr_.get_auth_response().empty() ? "NO" : "YES"));
          break;
        }
        default: {
          buf[0]='\0';
          break;
        }
      }
      if (OB_FAIL(send_error_packet(ret, buf))) {
        LOG_WARN("response fail packet fail", K(ret));
      }
    }

    if (NULL != session) {
      //Action!!:must revert it after no use it
      revert_session(session);
    }
    if (OB_SUCCESS != proc_ret) {
      if (NULL != session) {
        free_session();
      }
      disconnect();
    }


    LOG_INFO("MySQL LOGIN", "direct_client_ip", client_ip_buf, K_(client_ip),
             K_(user_name), K(host_name),
             K(sessid),
             K(capability),
             "c/s protocol", get_cs_protocol_type_name(protoType),
             K(autocommit), K(proc_ret), K(ret), K(conn->client_version_));
  }
  return ret;
}

const char *AUTH_PLUGIN_MYSQL_NATIVE_PASSWORD = "mysql_native_password";
int ObMPConnect::load_privilege_info(ObSQLSessionInfo &session)
{
  LOG_DEBUG("load privilege info");
  int ret = OB_SUCCESS;
  ObSMConnection *conn = get_conn();
  ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(gctx_.schema_service_) || OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(gctx_.schema_service_));
  } else if (OB_FAIL(gctx_.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get schema guard failed", K(ret));
  } else {
    ObString host_name;

    if (OB_SUCC(ret)) {
      if (user_name_.length() > OB_MAX_USER_NAME_LENGTH) {
        // Tenant routing used to split user@tenant before this check.  Once
        // tenant routing is removed, an overlong historical spelling is just
        // an invalid login name and must follow the normal 1045 path.
        ret = OB_PASSWORD_WRONG;
        LOG_WARN("user name is too long", K(user_name_), K(ret));
      } else if (db_name_.length() > OB_MAX_DATABASE_NAME_LENGTH) {
        ret = OB_INVALID_ARGUMENT_FOR_LENGTH;
        LOG_WARN("invalid length for db_name", K(db_name_), K(ret));
      } else {
        MEMCPY(db_name_var_, db_name_.ptr(), db_name_.length());
        db_name_var_[db_name_.length()] = '\0';
        MEMCPY(user_name_var_, user_name_.ptr(), user_name_.length());
        user_name_var_[user_name_.length()] = '\0';
        user_name_.assign_ptr(user_name_var_, user_name_.length());
        db_name_.assign_ptr(db_name_var_, db_name_.length());
      }
    }
    share::schema::ObSessionPrivInfo session_priv;
    EnableRoleIdArray enable_role_id_array;
    const ObSysVariableSchema *sys_variable_schema = NULL;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(schema_guard.get_sys_variable_schema( sys_variable_schema))) {
      LOG_WARN("get sys variable schema failed", K(ret));
    } else if (OB_ISNULL(sys_variable_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sys variable schema is null", K(ret));
    } else if (OB_FAIL(session.init_runtime(OB_SERVER_RUNTIME_NAME))) {
        LOG_WARN("failed to initialize session runtime", K(ret));
    } else if (OB_FAIL(session.load_all_sys_vars(*sys_variable_schema, false))) {
      LOG_WARN("load system variables failed", K(ret));
    } else if (OB_FAIL(session.update_sys_variable(
                   SYS_VAR_AUTOCOMMIT, conn->autocommit_snapshot_))) {
      LOG_WARN("update autocommit failed", K(ret), K(conn->autocommit_snapshot_));
    } else {
      share::schema::ObUserLoginInfo login_info;
      login_info.user_name_ = user_name_;
      login_info.client_ip_ = client_ip_;
      const ObUserInfo *user_info = NULL;
      // Normalize the requested database name before session privilege checks.
      if (!db_name_.empty()) {
        ObString db_name = db_name_;
        ObNameCaseMode mode = OB_NAME_CASE_INVALID;
        bool perserve_lettercase = true;
        ObCollationType cs_type = CS_TYPE_INVALID;
        if (OB_FAIL(session.get_collation_connection(cs_type))) {
          LOG_WARN("fail to get collation_connection", K(ret));
        } else if (OB_FAIL(session.get_name_case_mode(mode))) {
          LOG_WARN("fail to get name case mode", K(mode), K(ret));
        } else if (FALSE_IT(perserve_lettercase = (mode != OB_LOWERCASE_AND_INSENSITIVE))) {
        } else if (OB_FAIL(ObSQLUtils::check_and_convert_db_name(
                    cs_type, perserve_lettercase, db_name))) {
          LOG_WARN("fail to check and convert database name", K(db_name), K(ret));
        } else if (OB_FAIL(ObSQLUtils::cvt_db_name_to_org(schema_guard, &session, db_name, &allocator_))) {
          LOG_WARN("fail to convert db name to org", K(ret));
        } else {
          login_info.db_ = db_name;
        }
      }
      LOG_TRACE("some important information required for login verification, print it before doing login", K(ret), K(ObString(sizeof(conn->scramble_buf_), conn->scramble_buf_)), K(hsr_.get_auth_plugin_name()), K(hsr_.get_auth_response()));
      if (OB_FAIL(ret)) {
        // Do nothing
      } else {
        login_info.scramble_str_.assign_ptr(conn->scramble_buf_, static_cast<ObString::obstr_size_t>(sizeof(conn->scramble_buf_)));
        login_info.passwd_ = hsr_.get_auth_response();// Assume client is use mysql_native_password
        bool is_empty_passwd = false;
        if (OB_FAIL(schema_guard.is_user_empty_passwd(login_info, is_empty_passwd))) {
          LOG_WARN("failed to check is user account is empty && login_info.passwd_ is empty", K(ret), K(login_info.passwd_));
        } else if (!is_empty_passwd && // user account with empty password do not need auth switch, same as MySQL 5.7 and 8.x
                  !hsr_.get_auth_plugin_name().empty() && // client do not use mysql_native_method
                  hsr_.get_auth_plugin_name().compare(AUTH_PLUGIN_MYSQL_NATIVE_PASSWORD)) {
          // Client is not use mysql_native_password method,
          // but observer only support mysql_native_password in user account's authentication, 
          // so observer need tell client use mysql_native_password method by sending "AuthSwitchRequest"
          LOG_TRACE("auth plugin from client is not mysql_native_password, start to auth switch request", K(ret), K(hsr_.get_auth_plugin_name()));
          conn->set_auth_switch_phase(); // State of connection turn to auth_switch_phase
          OMPKAuthSwitch auth_switch;
          auth_switch.set_plugin_name(ObString(AUTH_PLUGIN_MYSQL_NATIVE_PASSWORD));
          // "AuthSwitchRequest" carry 20 bit random salt value(MySQL call it scramble) to client which has sent in "Initial Handshake Packet"
          auth_switch.set_scramble(ObString(sizeof(conn->scramble_buf_), conn->scramble_buf_));
          /*-------------------START-----------------If error occur, disconnect-------------------START-----------------*/
          if (OB_FAIL(packet_sender_.response_packet(auth_switch))) {
            RPC_LOG(WARN, "failed to send auth switch request packet, disconnect", K(auth_switch), K(ret));
            LOG_WARN("failed to send auth switch request packet, disconnect", K(auth_switch), K(ret));
            packet_sender_.disable_response(); // The connection is about to be closed, do not need response ok pkt or err pkt, so disable it
            disconnect();// If send "AuthSwitchRequest" failed, observer need disconnect with client
          } else if (OB_FAIL(packet_sender_.flush_buffer(
                         false /*is_last*/))) { // "AuthSwitchRequest" may not
                                                // have been sent yet, flush the
                                                // buffer to ensure it has been
                                                // sent.
            RPC_LOG(WARN, "failed to flush socket buffer while sending auth switch request packet, disconnect", K(auth_switch), K(ret));
            LOG_WARN("failed to flush socket buffer while sending auth switch request packet, disconnect", K(auth_switch), K(ret));
            packet_sender_.disable_response(); // The connection is about to be closed, do not need response ok pkt or err pkt, so disable it
            disconnect();// If send "AuthSwitchRequest" failed, observer need disconnect with client
          } else {
            LOG_TRACE("suuc to send auth switch request", K(ret));
            obmysql::ObMySQLPacket *asr_pkt = NULL;
            static const int64_t AUTH_SWITCH_TIMEOUT_US = 10 * 1000000L;
            if (OB_FAIL(packet_sender_.wait_packet(
                    asr_mem_pool_, AUTH_SWITCH_TIMEOUT_US, asr_pkt))) {
              RPC_LOG(WARN, "failed to wait for auth switch response pkt, disconnect", K(ret));
              LOG_WARN("failed to wait for auth switch response pkt, disconnect", K(ret));
              packet_sender_.disable_response(); // The connection is about to be closed, do not need response ok pkt or err pkt, so disable it
              disconnect(); // If receive "AuthSwitchResponse" failed, observer need disconnect with client
            } else if (OB_ISNULL(asr_pkt)) {
              ret = OB_WAIT_NEXT_TIMEOUT;
              RPC_LOG(WARN, "read auth switch response pkt timeout, disconnect", K(ret));
              LOG_WARN("read auth switch response pkt timeout, disconnect", K(ret));
              packet_sender_.disable_response();
              disconnect();
            } else {
              /*--------------------END------------------if error occur,
               * disconnect--------------------END------------------*/
              LOG_TRACE("suuc to receive auth switch response", K(ret));
              const obmysql::ObMySQLRawPacket *asr_raw_pkt  = reinterpret_cast<const ObMySQLRawPacket*>(asr_pkt);
              const char *auth_data = asr_raw_pkt->get_cdata();
              const int64_t auth_data_len = asr_raw_pkt->get_clen();
              void *auth_buf = NULL;
              // Length of authentication response data in AuthSwitchResponse which is using mysql_native_password methon is 20 byte, 
              // the ObSMConnection::SCRAMBLE_BUF_SIZE is 20
              if (ObSMConnection::SCRAMBLE_BUF_SIZE != auth_data_len) { 
                ret = OB_PASSWORD_WRONG;
                LOG_WARN("invalid length of authentication response data", K(ret), K(auth_data_len), K(ObString(auth_data_len, auth_data)));
              } else if (OB_ISNULL(auth_buf = asr_mem_pool_.alloc(auth_data_len))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                LOG_WARN("alloc auth data buffer for auth switch response failed", K(ret), K(auth_data_len));
              } else {
                // packet_sender_.release_packet will recycle mem of auth_data, need using mem allocated by asr_mem_pool_ to save it
                MEMCPY(auth_buf, auth_data, auth_data_len);
                login_info.scramble_str_.assign_ptr(conn->scramble_buf_, static_cast<ObString::obstr_size_t>(sizeof(conn->scramble_buf_)));
                login_info.passwd_.assign_ptr(static_cast<const char*>(auth_buf), auth_data_len);
              }
              const int release_ret = packet_sender_.release_packet(asr_pkt);
              if (OB_SUCCESS != release_ret) {
                LOG_WARN("failed to release auth switch response pkt", K(release_ret));
                if (OB_SUCC(ret)) {
                  ret = release_ret;
                }
              }
              asr_pkt = NULL;
              asr_raw_pkt = NULL;
            }
          }
          conn->set_auth_phase(); // State of connection turn to auth_phase
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(schema_guard.check_user_access(login_info, session_priv, enable_role_id_array, NULL, user_info))) {
        int tmp_ret = OB_SUCCESS;
        ObMultiVersionSchemaService *schema_service = gctx_.schema_service_;
        int64_t local_version = OB_INVALID_VERSION;
        int64_t global_version = OB_INVALID_VERSION;
        if (OB_SUCCESS != (tmp_ret = schema_service->get_runtime_refreshed_schema_version(local_version))) {
          LOG_WARN("fail to get local version", K(ret), K(tmp_ret));
        } else if (OB_SUCCESS != (tmp_ret = schema_service->get_published_schema_version(global_version))) {
          LOG_WARN("fail to get local version", K(ret), K(tmp_ret));
        } else if (local_version < global_version) {
          LOG_INFO("try to refresh schema", K(local_version), K(global_version));
          if (OB_SUCCESS != (tmp_ret = gctx_.schema_service_->async_refresh_schema(global_version))) {
            LOG_WARN("failed to refresh schema", K(tmp_ret), K(global_version));
          } else if (OB_SUCCESS != (tmp_ret = gctx_.schema_service_->get_runtime_schema_guard(
                                    schema_guard))) {
            LOG_WARN("get schema guard failed", K(ret), K(tmp_ret));
          } else if (OB_FAIL(schema_guard.check_user_access(login_info, session_priv,
                     enable_role_id_array, NULL, user_info))) {
            LOG_WARN("User access denied", K(login_info), K(ret));
          }
        }

        if (OB_FAIL(ret)) {
          LOG_WARN("User access denied", K(login_info), K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(session.on_user_connect(session_priv, user_info))) {
          LOG_WARN("session on user connect failed", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(check_password_expired(schema_guard, session))) {
        LOG_WARN("fail to check password expired", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      // Attention!! must set session capability firstly
      session.set_capability(hsr_.get_capability_flags());
      session.set_user_priv_set(session_priv.user_priv_set_);
      session.set_db_priv_set(session_priv.db_priv_set_);
      session.set_enable_role_array(enable_role_id_array);
      host_name = session_priv.host_name_;
      uint64_t db_id = OB_INVALID_ID;
      if (OB_FAIL(session.set_user(session_priv.user_name_, session_priv.host_name_, session_priv.user_id_))) {
        LOG_WARN("failed to set_user", K(ret));
      } else if (OB_FAIL(session.set_real_client_ip_and_port(client_ip_, client_port_))) {
        LOG_WARN("failed to set_real_client_ip_and_port", K(ret));
      } else if (OB_FAIL(session.set_default_database(session_priv.db_))) {
        LOG_WARN("failed to set default database", K(ret), K(session_priv.db_));
      } else if (OB_FAIL(session.update_database_variables(&schema_guard))) {
        LOG_WARN("failed to update database variables", K(ret));
      } else if (OB_FAIL(session.update_max_packet_size())) {
        LOG_WARN("failed to update max packet size", K(ret));
      }

      if (OB_SUCC(ret) && !session.get_database_name().empty()) {
        if (OB_FAIL(schema_guard.get_database_id(session.get_database_name(),
                                                 db_id))) {
          int tmp_ret = OB_SUCCESS;
          LOG_WARN("failed to get database id", K(ret), K(session.get_database_name()));
          ObMultiVersionSchemaService *schema_service = gctx_.schema_service_;
          int64_t local_version = OB_INVALID_VERSION;
          int64_t global_version = OB_INVALID_VERSION;
          
          if (OB_SUCCESS != (tmp_ret = schema_service->get_runtime_refreshed_schema_version(local_version))) {
            LOG_WARN("fail to get local version", K(ret), K(tmp_ret));
          } else if (OB_SUCCESS != (tmp_ret = schema_service->get_published_schema_version(global_version))) {
            LOG_WARN("fail to get local version", K(ret), K(tmp_ret));
          } else if (local_version < global_version) {
            LOG_INFO("try to refresh schema", K(1UL),
                     K(local_version), K(global_version));
            if (OB_SUCCESS != (tmp_ret = gctx_.schema_service_->async_refresh_schema(global_version))) {
              LOG_WARN("failed to refresh schema", K(tmp_ret),
                       K(1UL), K(global_version));
            } else if (OB_SUCCESS != (tmp_ret = gctx_.schema_service_->get_runtime_schema_guard(schema_guard))) {
              LOG_WARN("get schema guard failed", K(ret), K(tmp_ret));
            } else if (OB_SUCCESS != (tmp_ret = schema_guard.get_database_id(session.get_database_name(), db_id))) {
              LOG_WARN("failed to get database id", K(ret), K(tmp_ret));
            } else {
              // Only reset the error code when schema is successfully refreshed
              ret = OB_SUCCESS;
            }
          }
        }
        if (OB_SUCC(ret)) {
          session.set_database_id(db_id);
        }
      }
    }

    LOG_DEBUG("obmp connect info:", K(ret), K_(user_name),
              K(host_name), K_(client_ip), "database", hsr_.get_database(),
              K(hsr_.get_capability_flags().capability_));
  }
  return ret;
}

int ObMPConnect::check_password_expired(ObSchemaGetterGuard &schema_guard,
                                        ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  uint64_t user_id = OB_INVALID_ID;
  bool is_exist = false;
  if (OB_FAIL(schema_guard.check_user_exist(user_name_,
                                                   ObString(OB_DEFAULT_HOST_NAME),
                                                   is_exist,
                                                   &user_id))) {
    LOG_WARN("fail to check user exist", K(ret));
  } else if (!is_exist) {
    //do nothing
  } else if (OB_FAIL(ObPrivilegeCheck::check_password_expired_on_connection(user_id, schema_guard, session))) {
    LOG_WARN("fail to check password expired", K(ret), K(user_id));
  }
  return ret;
}

int64_t ObMPConnect::get_user_id()
{
  return OB_SYS_USER_ID;
}

int64_t ObMPConnect::get_database_id()
{
  return OB_SYS_DATABASE_ID;
}

int ObMPConnect::get_conn_id(uint32_t &conn_id) const
{
  int ret = OB_SUCCESS;
  bool is_found = false;
  ObString key_str;
  key_str.assign_ptr(OB_MYSQL_CONNECTION_ID , static_cast<int32_t>(STRLEN(OB_MYSQL_CONNECTION_ID)));
  for (int64_t i = 0; i < hsr_.get_connect_attrs().count() && OB_SUCC(ret) && !is_found; ++i) {
    ObStringKV kv =  hsr_.get_connect_attrs().at(i);
    if (key_str == kv.key_) {
      ObObj value;
      value.set_varchar(kv.value_);
      ObArenaAllocator allocator(ObModIds::OB_SQL_EXPR);
      ObCastCtx cast_ctx(&allocator, NULL, CM_NONE, ObCharset::get_system_collation());
      EXPR_GET_UINT32_V2(value, conn_id);
      if (OB_FAIL(ret)) {
        LOG_WARN("fail to cast connection id to uint32", K(kv.value_), K(ret));
      } else {
        is_found = true;
      }
    }
  }

  if (OB_SUCC(ret) && !is_found) {
    ret = OB_ENTRY_NOT_EXIST;
  }

  return ret;
}

int ObMPConnect::check_client_property(ObSMConnection &conn)
{
  int ret = OB_SUCCESS;
  ObMySQLCapabilityFlags client_cap = hsr_.get_capability_flags();
  if (OB_FAIL(set_client_version(conn))) {
    LOG_WARN("get proxy version fail", K(ret));
  }

  if (OB_SUCC(ret)) {
    get_peer().ip_to_string(client_ip_buf_, common::MAX_IP_ADDR_LENGTH);
    const char *peer_ip = client_ip_buf_;
    client_ip_.assign_ptr(peer_ip, static_cast<int32_t>(STRLEN(peer_ip)));
    client_port_ = get_peer().get_port();
    hsr_.set_capability_flags(client_cap);
    conn.cap_flags_ = client_cap;
  }
  return ret;
}

int ObMPConnect::verify_connection() const
{
  int ret = OB_SUCCESS;
  const char *IPV4_LOCAL_STR = "127.0.0.1";
  const char *IPV6_LOCAL_STR = "::1";

  if (OB_SUCC(ret)) {
    // Keep local administrator access available to recover invalid network settings.
    if (0 == user_name_.compare(OB_SYS_USER_NAME)
        && (0 == client_ip_.compare(IPV4_LOCAL_STR)
            || 0 == client_ip_.compare(IPV6_LOCAL_STR))) {
      LOG_DEBUG("local administrator bypasses the IP allowlist", K(ret));
    } else if (SS_INIT == GCTX.status_ || SS_STARTING == GCTX.status_) {
      LOG_INFO("server is initializing, ignore verify_ip_white_list", "status", GCTX.status_, K(ret));
    } else if (OB_FAIL(verify_ip_white_list())) {
      LOG_WARN("failed to verify_ip_white_list", K(ret));
    }
  }
  return ret;
}

int ObMPConnect::verify_identify(ObSMConnection &conn, ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  // At this point, the fixed runtime and session id are valid.
  ObSQLSessionInfo::LockGuard lock_guard(session.get_query_lock());
  if (OB_ISNULL(req_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("null request", K(ret));
  } else if (OB_FAIL(load_privilege_info(session))) {
    int pre_ret = ret;
    if (SS_INIT == GCTX.status_) {
      ret = OB_SERVER_IS_INIT;
    }
    LOG_WARN("load privilege info fail", K(pre_ret), K(ret), K(GCTX.status_));
  } else {
    session.update_last_active_time();
    SQL_REQ_OP.get_sock_desc(req_, session.get_sock_desc());
    SQL_REQ_OP.bind_sql_session(req_);
    session.set_peer_addr(get_peer());
    session.set_client_addr(get_peer());
    session.set_trans_type(transaction::ObTxClass::USER);
    // Lock the server runtime until the connection is destroyed.
    if (NULL != gctx_.server_runtime_controller_) {
      if (OB_FAIL(gctx_.server_runtime_controller_->lock_runtime(conn.runtime_))) {
        LOG_WARN("can't get server runtime", K(ret));
      } else if (OB_ISNULL(conn.runtime_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("null server runtime", K(ret));
      } else if (FALSE_IT(conn.is_runtime_locked_ = true)) {
      } else if (conn.runtime_->has_stopped()) {
        ret = OB_SERVER_RUNTIME_NOT_READY;
        LOG_WARN("server runtime is stopping, reject connecting", K(ret));
      }
    }

    // At this point, conn.runtime_ and sessid are stable.
    if (conn.sessid_ != 0) {
      conn.has_inc_active_num_ = true;
    }

    // init_connect is not executed for users that have the super privilege
    if (OB_SUCC(ret)
        && !(OB_PRIV_SUPER & session.get_user_priv_set())) {
      ObString sql_str;
      if (OB_FAIL(session.get_init_connect(sql_str))) {
        LOG_WARN("get sys variable init_connect failed.", K(ret));
      } else {
        if (0 == sql_str.compare("")) {
          // do nothing
        } else {
          if (OB_FAIL(init_connect_process(sql_str, session))) {
            LOG_WARN("init connect failed.", K(sql_str), K(ret));
          }
        }
      }
      LOG_DEBUG("INIT_CONNECT", K(ret), K(sql_str));
      //a statement that has a error will causing client connections to fail
      if (OB_SUCCESS != ret) {
        force_disconnect();
      }
    }

    //set session state
    if (OB_SUCC(ret)) {
      if(OB_FAIL(session.set_session_state(SESSION_SLEEP))) {
        LOG_WARN("fail to set session state", K(ret));
      }
    }
  }
  return ret;
}

int ObMPConnect::verify_ip_white_list() const
{
  int ret = OB_SUCCESS;
  const ObSysVariableSchema *sys_variable_schema = NULL;
  share::schema::ObSchemaGetterGuard schema_guard;
  ObString var_name(OB_SV_TCP_INVITED_NODES);
  const ObSysVarSchema *sysvar = NULL;
  if (OB_UNLIKELY(client_ip_.empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("client_ip is empty", K(ret));
  } else if (0 == client_ip_.compare(UNIX_SOCKET_CLIENT_IP)) {
    LOG_INFO("match unix socket connection", K(client_ip_));
  } else if (OB_FAIL(gctx_.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get_schema_guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_sys_variable_schema( sys_variable_schema))) {
    LOG_WARN("get sys variable schema failed", K(ret));
  } else if (OB_ISNULL(sys_variable_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sys variable schema is null", K(ret));
  } else if (OB_FAIL(sys_variable_schema->get_sysvar_schema(var_name, sysvar))) {
    LOG_WARN("fail to get_sysvar_schema",  K(ret));
  } else {
    ObString var_value = sysvar->get_value();
    if (!obsys::ObNetUtil::is_in_white_list(client_ip_, var_value)) {
      ret = OB_ERR_NO_PRIVILEGE;
      LOG_WARN("client is not invited into this runtime", K(ret));
    }
  }
  return ret;
}

int ObMPConnect::set_client_version(ObSMConnection &conn)
{
  int ret = OB_SUCCESS;
  bool is_found = false;
  ObString key_str;
  const char *client_version_str = NULL;
  int64_t length = 0;
  key_str.assign_ptr(OB_MYSQL_CLIENT_VERSION,
                     static_cast<int32_t>(STRLEN(OB_MYSQL_CLIENT_VERSION)));
  for (int64_t i = 0; !is_found && i < hsr_.get_connect_attrs().count(); ++i) {
    const ObStringKV &kv =  hsr_.get_connect_attrs().at(i);
    if (key_str == kv.key_) {
      client_version_str = kv.value_.ptr();
      length = kv.value_.length();
      is_found = true;
    }
  }
  int64_t min_len = 5;//The shortest valid version string passed over is "1.1.1", with a length of at least 5
  if (!is_found || OB_ISNULL(client_version_str) || length < min_len) {
    conn.client_version_ = 0;
  } else {
    const int64_t VERSION_ITEM = 3;//The version number only needs the first three digits, for example, "1.7.6.1" only needs to take "1.7.6" to determine;
    char buff[OB_MAX_VERSION_LENGTH];
    memset(buff, 0, OB_MAX_VERSION_LENGTH);
    int64_t cur_item = 0;
    for (int64_t i = 0; cur_item != VERSION_ITEM && i < length; ++i) {
      if (client_version_str[i] == '.') {
        ++cur_item;
      }
      if (cur_item != VERSION_ITEM) {
        buff[i] = client_version_str[i];
      }
    }
    if (OB_FAIL(ObVersionParser::get_version(buff, conn.client_version_))) {
      LOG_WARN("failed to get version", K(ret));
    } else {/*do nothing*/}
  }
  return ret;
}
