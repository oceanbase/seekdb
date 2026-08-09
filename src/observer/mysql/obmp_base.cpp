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

#include "obmp_base.h"

#include "sql/ob_mysql_end_trans_cb.h"
#include "rpc/obmysql/packet/ompk_row.h"
#include "observer/mysql/obsm_row.h"
#include "observer/mysql/obmp_utils.h"
#include "observer/mysql/ob_query_driver.h"
#include "sql/engine/expr/ob_expr_xml_func_helper.h"
void OB_WEAK_SYMBOL request_finish_callback();
namespace oceanbase
{
using namespace share;
using namespace rpc;
using namespace obmysql;
using namespace common;
using namespace sql;
using namespace transaction;
using namespace share::schema;
namespace sql
{
  class ObPiece;
}

namespace observer
{

ObMPBase::ObMPBase(const share::ObGlobalContext &gctx)
    : gctx_(gctx), process_timestamp_(0), end_trans_cb_to_enable_(NULL)
{
}

ObMPBase::~ObMPBase()
{
  // Finish any request still owned by the processor before releasing deferred
  // callback state. Packet retry explicitly detaches the generation instead;
  // successful async handoff makes either operation a no-op.
  if (THIS_WORKER.need_retry()) {
    const int detach_ret = packet_sender_.detach_for_retry();
    if (OB_SUCCESS != detach_ret) {
      LOG_WARN_RET(detach_ret,
                   "failed to detach mysql request for packet retry");
    }
  } else {
    (void)packet_sender_.finish_sql_request();
  }
  if (NULL != end_trans_cb_to_enable_) {
    ObSqlEndTransCb *end_trans_cb = end_trans_cb_to_enable_;
    end_trans_cb_to_enable_ = NULL;
    end_trans_cb->allow_request_completion();
  }
}

int ObMPBase::response(const int retcode)
{
  UNUSED(retcode);
  int ret = OB_SUCCESS;
  if (!THIS_WORKER.need_retry()) {
    if (OB_FAIL(flush_buffer(true))) {
    }
  }
  return ret;
}

int ObMPBase::setup_packet_sender()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(packet_sender_.init(req_))) {
    LOG_ERROR("packet sender init fail", KP(req_), K(ret));
    send_error_packet(ret, NULL);
  }
  return ret;
}

int ObMPBase::before_process()
{
  int ret = OB_SUCCESS;
  process_timestamp_ = common::ObTimeUtility::current_time();
  return ret;
}

int ObMPBase::after_process(int error_code)
{
  int ret = OB_SUCCESS;
  {
    NG_TRACE_EXT(process_end, OB_ID(run_ts), get_run_timestamp());
    const int64_t elapsed_time = common::ObTimeUtility::current_time() - get_receive_timestamp();
    bool is_slow = (elapsed_time > GCONF.trace_log_slow_query_watermark)
      && !THIS_WORKER.need_retry();
    if (is_slow) {
      if (THIS_WORKER.need_retry() && OB_TRY_LOCK_ROW_CONFLICT == error_code) {
        // If it is a lock conflict and a retry will follow, then this log does not need to be printed.
      } else {
        FORCE_PRINT_TRACE(THE_TRACE, "[slow query]");

        // slow query will flush cache
        FLUSH_TRACE();
      }
    } else if (can_force_print(error_code)) {
      // Error codes that need to print TRACE logs are added here
      int process_ret = error_code;
      NG_TRACE_EXT(process_ret, OB_Y(process_ret));
      FORCE_PRINT_TRACE(THE_TRACE, "[err query]");
    } else if (THIS_WORKER.need_retry()) {
      if (OB_TRY_LOCK_ROW_CONFLICT != error_code) {
        FORCE_PRINT_TRACE(THE_TRACE, "[packet retry query]");
      }
    } else {
      PRINT_TRACE(THE_TRACE);
    }

    if (common::OB_SUCCESS != error_code) {
      FLUSH_TRACE();
    }
  }
  return ret;
}

void ObMPBase::cleanup()
{
  if (NULL != end_trans_cb_to_enable_) {
    ObSqlEndTransCb *end_trans_cb = end_trans_cb_to_enable_;
    end_trans_cb_to_enable_ = NULL;
    // This is the final processor hook: response() and after_process() have
    // stopped touching request-owned storage, so callback completion may now
    // stage the final packet and let Rust reuse the request pool.
    end_trans_cb->allow_request_completion();
  }
}

int ObMPBase::handoff_async_request(ObSqlEndTransCb &end_trans_cb)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(end_trans_cb_to_enable_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_ERROR("an async mysql callback is already registered", K(ret));
  } else {
    ret = end_trans_cb.take_request_ownership(packet_sender_);
    if (OB_SUCCESS != ret) {
      LOG_ERROR("failed to hand off async mysql request ownership", K(ret));
      const int abort_ret = end_trans_cb.abort_request_handoff();
      if (OB_SUCCESS != abort_ret) {
      } else {
        // The main sender still owns the request. Let response() finish it,
        // then cleanup() can safely release the callback's session lease.
        end_trans_cb_to_enable_ = &end_trans_cb;
      }
    } else {
      end_trans_cb_to_enable_ = &end_trans_cb;
      request_finish_callback();
    }
  }
  return ret;
}

int ObMPBase::cancel_unsubmitted_callback(ObSqlEndTransCb &end_trans_cb)
{
  int ret = OB_SUCCESS;
  bool needs_cleanup = false;
  ret = end_trans_cb.cancel_unsubmitted(needs_cleanup);
  if (needs_cleanup) {
    if (OB_NOT_NULL(end_trans_cb_to_enable_)) {
      const int register_ret = OB_STATE_NOT_MATCH;
      LOG_ERROR("an async mysql callback is already registered",
                K(register_ret), K(ret));
      ret = OB_SUCCESS == ret ? register_ret : ret;
    } else {
      end_trans_cb_to_enable_ = &end_trans_cb;
    }
    // A callback arriving despite a negative submission flag breaks the wire
    // response contract; retire the connection after both sides are cleaned.
    force_disconnect();
  }
  return ret;
}

void ObMPBase::disconnect()
{
  return packet_sender_.disconnect();
}

void ObMPBase::force_disconnect()
{
  return packet_sender_.force_disconnect();
}

int ObMPBase::flush_buffer(const bool is_last)
{
  return packet_sender_.is_disable_response()? OB_SUCCESS: packet_sender_.flush_buffer(is_last);
}

ObSMConnection* ObMPBase::get_conn() const
{
  return packet_sender_.get_conn();
}

int ObMPBase::get_conn_id(uint32_t &sessid) const
{
  return packet_sender_.get_conn_id(sessid);
}

int ObMPBase::send_error_packet(int err,
                                const char* errmsg,
                                void *extra_err_info /* = NULL */)
{
  return packet_sender_.send_error_packet(err, errmsg, extra_err_info);
}

int ObMPBase::load_system_variables(const ObSysVariableSchema &sys_variable_schema, ObSQLSessionInfo &session) const
{
  int ret = OB_SUCCESS;
  ObArenaAllocator calc_buf(ObModIds::OB_SQL_SESSION);
  for (int64_t i = 0; OB_SUCC(ret) && i < sys_variable_schema.get_sysvar_count(); ++i) {
    const ObSysVarSchema *sysvar = NULL;
    sysvar = sys_variable_schema.get_sysvar_schema(i);
    if (sysvar != NULL) {
      if (OB_FAIL(session.load_sys_variable(calc_buf, sysvar->get_name(), sysvar->get_data_type(),
                                            sysvar->get_value(), sysvar->get_min_val(),
                                            sysvar->get_max_val(), sysvar->get_flags(), true))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    //Set the maximum version number of the system variable
    //Serialize and cache the system variable sequence that affects the plan
    if (OB_FAIL(session.gen_sys_var_in_pc_str())) {
    } else if (OB_FAIL(session.gen_configs_in_pc_str())) {
    } else {
      session.set_global_vars_version(sys_variable_schema.get_schema_version());
      session.set_enable_mysql_compatible_dates(
        session.get_enable_mysql_compatible_dates_from_config());
    }
  }
  return ret;
}

int ObMPBase::send_ok_packet(ObSQLSessionInfo &session, ObOKPParam &ok_param, obmysql::ObMySQLPacket* pkt)
{
  return packet_sender_.send_ok_packet(session, ok_param, pkt);
}

int ObMPBase::send_eof_packet(const ObSQLSessionInfo &session, const ObMySQLResultSet &result, ObOKPParam *ok_param)
{
  return packet_sender_.send_eof_packet(session, result, ok_param);
}

int ObMPBase::create_session(ObSMConnection *conn, ObSQLSessionInfo *&sess_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("get connection fail", K(ret));
  } else {
    if (OB_FAIL(OBSERVER.get_sql_session_mgr().create_session(conn, sess_info))) {
    } else {
      conn->is_sess_alloc_.store(true, std::memory_order_release);
      sess_info->set_user_session();
      sess_info->set_shadow(false);
      sess_info->set_ssl_cipher("");
    }
  }
  return ret;
}

int ObMPBase::free_session()
{
  int ret = OB_SUCCESS;
  ObSMConnection* conn = NULL;
  if (NULL == (conn = packet_sender_.get_conn())) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection already disconnected", K(ret));
  } else {
    ObFreeSessionCtx ctx;
    
    ctx.sessid_ = conn->sessid_;
    ctx.has_inc_active_num_ = conn->has_inc_active_num_;
    if (OB_FAIL(OBSERVER.get_sql_session_mgr().free_session(ctx))) {
    } else {
      LOG_INFO("free session successfully", K(ctx));
      conn->is_sess_free_.store(true, std::memory_order_release);
    }
  }
  return ret;
}

int ObMPBase::get_session(ObSQLSessionInfo *&sess_info)
{
  return packet_sender_.get_session(sess_info);
}

int ObMPBase::revert_session(ObSQLSessionInfo *sess_info)
{
  return packet_sender_.revert_session(sess_info);
}

int ObMPBase::init_process_var(sql::ObSqlCtx &ctx,
                               const ObMultiStmtItem &multi_stmt_item,
                               sql::ObSQLSessionInfo &session) const
{
  int ret = OB_SUCCESS;
  if (!packet_sender_.is_conn_valid()) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection already disconnected", K(ret));
  } else {
    const int64_t debug_sync_timeout = GCONF.debug_sync_timeout;
    // ignore session debug sync action actions to thread local actions error
    if (debug_sync_timeout > 0) {
      int tmp_ret = GDS.set_thread_local_actions(session.get_debug_sync_actions());
      if (OB_UNLIKELY(OB_SUCCESS != tmp_ret)) {
      }
    }
    // construct sql context
    ctx.multi_stmt_item_ = multi_stmt_item;
    ctx.session_info_ = &session;
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());

    ctx.set_enable_strict_defensive_check(GCONF.enable_strict_defensive_check());
    LOG_DEBUG("protocol flag info", K(ctx.get_enable_strict_defensive_check()));
  }
  return ret;
}
//The outer call will ignore the error code of do_after_process, therefore returning the error code of set_session_state here is also meaningless.
//Therefore, here the set_session_state error code is ignored, and the reset of the warning buffer and the trace log recording process are not affected.
int ObMPBase::do_after_process(sql::ObSQLSessionInfo &session,
                               bool async_resp_used,
                               int process_ret) const
{
  int ret = OB_SUCCESS;
  if (!async_resp_used && OB_SUCCESS == process_ret && session.get_in_transaction()) {
    session.set_curr_trans_last_stmt_end_time(ObClockGenerator::getClock());
  } else if (!session.get_in_transaction()) {
    session.set_curr_trans_last_stmt_end_time(0);
  }
  if (session.get_is_in_retry()) {
    // do nothing.
  } else {
    session.set_is_request_end(true);
    session.set_retry_active_time(0);
  }
  // reset warning buffers
  // Finish ownership may already have moved to an async sender; do not access
  // the request object again in that case.
  // @todo Refactor wb logic
  if (!async_resp_used) { // Asynchronous response does not reset warning buffer, reset operation is done in callback
    session.reset_warnings_buf();
    if (!session.get_is_in_retry()) {
      session.set_session_sleep();
      session.reset_cur_sql_id();
      session.reset_current_plan_id();
      session.reset_current_plan_hash();
    }
  }
  // clear tsi warning buffer
  ob_setup_tsi_warning_buffer(NULL);
  session.reset_plsql_exec_time();
  session.reset_plsql_compile_time();
  return ret;
}

// force refresh schema if local schema version < last schema version
int ObMPBase::check_and_refresh_schema(ObSQLSessionInfo *session_info)
{
  int ret = OB_SUCCESS;
  int64_t local_version = 0;
  int64_t last_version = 0;

  if (OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null schema service", K(ret), K(gctx_));
  } else {
    bool need_revert_session = false;
    if (NULL == session_info) {
      if (OB_FAIL(get_session(session_info))) {
      } else if (OB_ISNULL(session_info)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid session info", K(ret), K(session_info));
      } else {
        need_revert_session = true;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(gctx_.schema_service_->get_runtime_refreshed_schema_version(local_version))) {
      } else if (FALSE_IT(last_version = session_info->get_last_ddl_schema_version())) {
      } else if (local_version >= last_version) {
        // skip
      } else if (OB_FAIL(gctx_.schema_service_->async_refresh_schema(last_version))) {
      }
      if (need_revert_session && OB_LIKELY(NULL != session_info)) {
        revert_session(session_info);
      }
    }
  }
  return ret;
}

int ObMPBase::response_row(ObSQLSessionInfo &session,
                           common::ObNewRow &row,
                           const ColumnsFieldIArray *fields,
                           bool is_packed,
                           ObExecContext *exec_ctx,
                           bool is_ps_protocol,
                           ObSchemaGetterGuard *schema_guard)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  ObNewRow tmp_row;
  bool has_charset_convert = false;
  if (OB_ISNULL(fields) || row.get_count() != fields->count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("fields is null", K(ret), KP(fields));
  } else if (OB_FAIL(ob_write_row(allocator, row, tmp_row))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tmp_row.get_count(); ++i) {
      ObObj &value = tmp_row.get_cell(i); 
      ObCharsetType charset_type = CHARSET_INVALID;
      // need at ps mode
      if (!is_packed && value.get_type() != fields->at(i).type_.get_type()) {
        ObCastCtx cast_ctx(&allocator, NULL, CM_WARN_ON_FAIL, fields->at(i).type_.get_collation_type());
        if (ObDecimalIntType == fields->at(i).type_.get_type()) {
          cast_ctx.res_accuracy_ = const_cast<ObAccuracy*>(&fields->at(i).accuracy_);
        }
        if (OB_FAIL(common::ObObjCaster::to_type(fields->at(i).type_.get_type(),
                                          cast_ctx,
                                          value,
                                          value))) {
        }
      }
      if (OB_FAIL(ret)) {
      } else if (is_packed) {
        // do nothing
      } else if (OB_FAIL(session.get_character_set_results(charset_type))) {
      } else {
        if (ob_is_string_tc(value.get_type())
            && CS_TYPE_INVALID != value.get_collation_type()
            && OB_FAIL(value.convert_string_value_charset(charset_type, allocator))) {
          LOG_WARN("convert string value charset failed", K(ret), K(value));
        } else if (ob_is_text_tc(value.get_type())
                    && OB_FAIL(ObQueryDriver::convert_text_value_charset(value, charset_type, allocator, &session, exec_ctx))) {
          LOG_WARN("convert text value charset failed", K(ret));
        }
        if (OB_FAIL(ret)) {
        } else if(OB_FAIL(ObQueryDriver::process_lob_locator_results(value,
                                    &allocator,
                                    &session,
                                    exec_ctx))) {
        } else if ((value.is_collection_sql_type() || value.is_geometry())
                   && OB_FAIL(ObSqlUdtUtils::convert_result_for_client(value,
                                    &allocator,
                                    &session,
                                    exec_ctx,
                                    is_ps_protocol,
                                    fields,
                                    schema_guard))) {
          LOG_WARN("convert udt to client format failed", K(ret), K(value.get_udt_subschema_id()));      
        }
      }
    }

    if (OB_SUCC(ret)) {
      const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(&session);
      ObSMRow sm_row(obmysql::BINARY, tmp_row, dtc_params, session, fields, schema_guard);
      sm_row.set_packed(is_packed);
      obmysql::OMPKRow rp(sm_row);
      if (OB_FAIL(response_packet(rp))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("response packet fail", K(ret));
      }
    }
  }
  return ret;
}

int ObMPBase::update_charset_sys_vars(ObSMConnection &conn, ObSQLSessionInfo &sess_info)
{
  int ret = OB_SUCCESS;
  int64_t cs_type = conn.client_cs_type_;
  if (ObCharset::is_valid_collation(cs_type)) {
    if (OB_FAIL(sess_info.update_sys_variable(SYS_VAR_CHARACTER_SET_CLIENT, cs_type))) {
    } else if (OB_FAIL(sess_info.update_sys_variable(SYS_VAR_CHARACTER_SET_RESULTS, cs_type))) {
    } else if (OB_FAIL(sess_info.update_sys_variable(SYS_VAR_CHARACTER_SET_CONNECTION, cs_type))) {
    } else if (OB_FAIL(sess_info.update_sys_variable(SYS_VAR_COLLATION_CONNECTION, cs_type))) {
    }
  }
  return ret;
}

int ObMPBase::load_privilege_info_for_change_user(sql::ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;

  ObSchemaGetterGuard schema_guard;
  ObSMConnection *conn = NULL;
  if (OB_ISNULL(session) || OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN,"invalid argument", K(session), K(gctx_.schema_service_));
  } else if (OB_ISNULL(conn = get_conn())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("null conn", K(ret));
  } else if (OB_FAIL(gctx_.schema_service_->get_runtime_schema_guard(
                                  schema_guard))) {
  } else {
    share::schema::ObUserLoginInfo login_info = session->get_login_info();
    share::schema::ObSessionPrivInfo session_priv;
    EnableRoleIdArray enable_role_id_array;
    // disconnect previous user connection first.
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(session->on_user_disconnect())) {
    }
    const ObUserInfo *user_info = NULL;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(schema_guard.check_user_access(login_info, session_priv,
                enable_role_id_array, NULL, user_info))) {
    } else if (OB_FAIL(session->on_user_connect(session_priv, user_info))) {
    } else {
      uint64_t db_id = OB_INVALID_ID;
      const ObSysVariableSchema *sys_variable_schema = NULL;
      session->set_user(session_priv.user_name_, session_priv.host_name_, session_priv.user_id_);
      session->set_user_priv_set(session_priv.user_priv_set_);
      session->set_db_priv_set(session_priv.db_priv_set_);
      session->set_enable_role_array(enable_role_id_array);
      if (OB_FAIL(session->set_runtime(login_info.runtime_name_))) {
      } else if (OB_FAIL(session->set_real_client_ip_and_port(login_info.client_ip_, session->get_client_addr_port()))) {
      } else if (OB_FAIL(schema_guard.get_sys_variable_schema( sys_variable_schema))) {
      } else if (OB_ISNULL(sys_variable_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("sys variable schema is null", K(ret));
      } else if (OB_FAIL(session->load_all_sys_vars(*sys_variable_schema, true))) {
      } else if (OB_FAIL(session->update_database_variables(&schema_guard))) {
      } else if (!session->get_database_name().empty() &&
                  OB_FAIL(schema_guard.get_database_id(session->get_database_name(),
                                                      db_id))) {
        OB_LOG(WARN, "failed to get database id", K(ret));
      } else if (OB_FAIL(update_charset_sys_vars(*conn, *session))) {
      } else {
        session->set_database_id(db_id);
        session->reset_user_var();
      }
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
