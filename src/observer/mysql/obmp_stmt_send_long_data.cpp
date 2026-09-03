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

#include "observer/ob_server_runtime_access.h"
#include "observer/mysql/obmp_stmt_send_long_data.h"

#include "sql/ob_sql.h"
#include "observer/omt/ob_server_runtime.h"
#include "sql/session/ob_piece_cache.h"
#include "sql/plan_cache/ob_ps_cache.h"

namespace oceanbase
{

using namespace rpc;
using namespace common;
using namespace share;
using namespace obmysql;
using namespace sql;

namespace observer
{

ObMPStmtSendLongData::ObMPStmtSendLongData(const share::ObGlobalContext &gctx)
    : ObMPBase(gctx),
      single_process_timestamp_(0),
      exec_start_timestamp_(0),
      exec_end_timestamp_(0),
      stmt_id_(0),
      param_id_(OB_MAX_PARAM_ID),
      buffer_len_(0),
      buffer_(),
      need_disconnect_(false)
{
  ctx_.exec_type_ = MpQuery;
}

/*
 * request packet:
 * 1  COM_STMT_SEND_LONG_DATA
 * 4  stmt_id
 * 2  param_id
 * n  data
 */
int ObMPStmtSendLongData::before_process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObMPBase::before_process())) {
  } else {
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    if (OB_UNLIKELY(ObMySQLCommandLayout::LONG_DATA !=
                    pkt.get_command_layout())) {
      ret = OB_INVALID_DATA;
      LOG_WARN("unexpected stmt-long-data command layout", K(ret),
               K(pkt.get_command_layout()));
    } else if (OB_FAIL(pkt.get_command_field(0, buffer_))) {
    } else {
      stmt_id_ = static_cast<int32_t>(pkt.get_command_scalar0());
      param_id_ = static_cast<uint16_t>(pkt.get_command_scalar1());
      buffer_len_ = buffer_.length();
    }

    if (OB_SUCC(ret) && stmt_id_ < 1) {
      ret = OB_ERR_PARAM_INVALID;
      LOG_WARN("send_long_data receive unexpected stmt_id_", K(ret), K(stmt_id_), K(param_id_));
    } else if (param_id_ >= OB_PARAM_ID_OVERFLOW_RISK_THRESHOLD) {
      LOG_WARN("param_id_ has the risk of overflow", K(ret), K(stmt_id_), K(param_id_));
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("resolve send_long_data protocol packet successfully",
               K(stmt_id_), K(param_id_), K(buffer_len_));
    }
    LOG_INFO("resolve send_long_data protocol packet",
             K(ret), K(stmt_id_), K(param_id_), K(buffer_len_), K(buffer_.length()));
  }
  return ret;
}

int ObMPStmtSendLongData::process()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = NULL;
  bool need_response_error = true;
  bool async_resp_used = false; // Asynchronously reply to the client by the transaction commit thread
  int64_t query_timeout = 0;
  ObSMConnection *conn = get_conn();

  if (OB_ISNULL(req_) || OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("req or conn is null", K_(req), K(conn), K(ret));
  } else if (OB_UNLIKELY(!conn->is_in_authed_phase())) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("receive sql without session", K_(stmt_id), K_(param_id), K(ret));
  } else if (OB_ISNULL(conn->runtime_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid runtime", K_(stmt_id), K_(param_id), K(conn->runtime_), K(ret));
  } else if (OB_FAIL(get_session(sess))) {
  } else if (OB_ISNULL(sess)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL or invalid", K_(stmt_id), K_(param_id), K(sess), K(ret));
  } else {
    ObSQLSessionInfo &session = *sess;
    THIS_WORKER.set_session(sess);
    ObSQLSessionInfo::LockGuard lock_guard(session.get_query_lock());
    session.set_current_trace_id(ObCurTraceId::get_trace_id());
    int64_t runtime_version = 0;
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    int64_t packet_len = pkt.get_clen();
    if (OB_UNLIKELY(!session.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid session", K_(stmt_id), K_(param_id), K(ret));
    } else if (OB_UNLIKELY(session.is_zombie())) {
      ret = OB_ERR_SESSION_INTERRUPTED;
      LOG_WARN("session has been killed", K(session.get_session_state()), K_(stmt_id), K_(param_id),
               K(session.get_server_sid()), K(ret));
    } else if (OB_UNLIKELY(packet_len > session.get_max_packet_size())) {
      ret = OB_ERR_NET_PACKET_TOO_LARGE;
      LOG_WARN("packet too large than allowd for the session", K_(stmt_id), K_(param_id), K(ret));
    } else if (OB_FAIL(session.get_query_timeout(query_timeout))) {
    } else if (OB_FAIL(gctx_.schema_service_->get_published_schema_version(
                runtime_version))) {
    } else {
      THIS_WORKER.set_timeout_ts(get_receive_timestamp() + query_timeout);
      if (OB_FAIL(process_send_long_data_stmt(session))) {
      }
    }

    if (OB_FAIL(ret)) {
      // send long data fail will not response packet, just print log
      LOG_WARN("send long data error happend ", K(ret), K(stmt_id_), K(param_id_), K(need_disconnect_));

      if (!need_disconnect_) {
        ObPiece *piece = NULL;
        ObPieceCache *piece_cache = session.get_piece_cache(false);
        if (OB_ISNULL(piece_cache)) {
          need_disconnect_ = true;
          LOG_WARN("piece cache is null.", K(ret), K(stmt_id_), K(param_id_));
        } else if (OB_SUCCESS != piece_cache->get_piece(stmt_id_, param_id_, piece)) {
          need_disconnect_ = true;
          LOG_WARN("get piece fail", K(stmt_id_), K(param_id_), K(ret));
        } else if (NULL == piece) {
          need_disconnect_ = true;
          LOG_WARN("get piece fail", K(stmt_id_), K(param_id_), K(ret));
        } else {
          piece->set_error_ret(ret);
        }
      }
    }
    if (need_disconnect_) {
      force_disconnect();
    }

    session.set_last_trace_id(ObCurTraceId::get_trace_id());
    THIS_WORKER.set_session(NULL);
    revert_session(sess); //current ignore revert session ret
  }
  return ret;
}

int ObMPStmtSendLongData::process_send_long_data_stmt(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  bool need_response_error = true;
  setup_wb(session);

  ObThreadLogLevelUtils::init(session.get_log_id_level_map());
  ret = do_process(session);
  ObThreadLogLevelUtils::clear();
  //For the handling of tracelog, it does not affect the normal logic, and the error code does not need to be assigned to ret
  int tmp_ret = OB_SUCCESS;
  //Clear WARNING BUFFER
  tmp_ret = do_after_process(session, false, ret);
  UNUSED(tmp_ret);
  return ret;
}

int ObMPStmtSendLongData::do_process(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  ObExecutingSqlStatRecord sqlstat_record;
  ObAuditRecordData &audit_record = session.get_raw_audit_record();
  audit_record.try_cnt_++;
  const bool enable_sqlstat = session.is_sqlstat_enabled();
  single_process_timestamp_ = ObTimeUtility::current_time();
  bool is_diagnostics_stmt = false;

  {
    {
      audit_record.exec_record_.record_start();
    }
    if (enable_sqlstat) {
      sqlstat_record.record_sqlstat_start_value(
          ::oceanbase::observer::get_observer_sql_engine()->get_query_runtime_environment());
      sqlstat_record.set_is_in_retry(session.get_is_in_retry());
      session.sql_sess_record_sql_stat_start_value(sqlstat_record);
    }
    int64_t execution_id = 0;
    ObString sql = "send long data";
    if (FALSE_IT(execution_id = ::oceanbase::observer::get_observer_sql_engine()->get_execution_id())) {
      //nothing to do
    } else if (OB_FAIL(set_session_active(sql, session, ObTimeUtil::current_time(), 
                                          obmysql::ObMySQLCmd::COM_STMT_SEND_LONG_DATA))) {
    } else if (OB_FAIL(store_piece(session))) {
      exec_start_timestamp_ = ObTimeUtility::current_time();
    } else {
      //Monitoring item statistics start
      exec_start_timestamp_ = ObTimeUtility::current_time();

      session.set_current_execution_id(execution_id);
      //Monitoring item statistics end
      exec_end_timestamp_ = ObTimeUtility::current_time();

      // some statistics must be recorded for plan stat, even though sql audit disabled
      bool first_record = (1 == audit_record.try_cnt_);
      ObExecStatUtils::record_exec_timestamp(*this, first_record, audit_record.exec_timestamp_);
      audit_record.exec_timestamp_.update_stage_time();
    }
  } // diagnose end

  {
    audit_record.exec_record_.record_end();
    audit_record.update_event_stage_state();
    const int64_t time_cost = exec_end_timestamp_ - get_receive_timestamp();
  }
  if (enable_sqlstat) {
    sqlstat_record.record_sqlstat_end_value(
        ::oceanbase::observer::get_observer_sql_engine()->get_query_runtime_environment());
  }

  // store the warning message from the most recent statement in the current session
  if (OB_SUCC(ret) && is_diagnostics_stmt) {
    // if diagnostic stmt execute successfully, it dosen't clear the warning message
    session.update_show_warnings_buf();
  } else {
    session.set_show_warnings_buf(ret); // TODO: Move this to a better place, reduce some wb copy
  }

  clear_wb_content(session);
  return ret;
}

int ObMPStmtSendLongData::store_piece(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  ObPieceCache *piece_cache = session.get_piece_cache(true);
  if (OB_ISNULL(piece_cache)) {
    ret = OB_ERR_UNEXPECTED;
    need_disconnect_ = true;
    LOG_WARN("piece cache is null.", K(ret), K(stmt_id_), K(param_id_));
  } else {
    ObPiece *piece = NULL;
    if (OB_FAIL(piece_cache->get_piece(stmt_id_, param_id_, piece))) {
    } else if (NULL == piece) {
      if (OB_FAIL(piece_cache->make_piece(stmt_id_, param_id_, piece, session))) {
      }
    }
    if (OB_FAIL(ret) || NULL == piece) {
      ret = OB_SUCCESS == ret ? OB_ERR_UNEXPECTED : ret; 
      need_disconnect_ = true;
      LOG_WARN("piece is null.", K(ret), K(piece), K(stmt_id_), K(param_id_));
    } else if (OB_FAIL(piece_cache->add_piece_buffer(piece, 
                                                      ObPieceMode::ObInvalidPiece, 
                                                      &buffer_))) {
    } else {
      // send long data do not response.
      LOG_INFO("store piece successfully", K(ret), K(session.get_server_sid()),
                                           K(stmt_id_), K(param_id_));
    }
  }
  return ret;
}

} //end of namespace observer
} //end of namespace oceanbase
