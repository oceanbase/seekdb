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
#include "observer/mysql/obmp_stmt_fetch.h"
#include "query/protocol/ob_mysql_protocol_util.h"
#include "share/ob_lob_access_utils.h"
#include "sql/ob_sql.h"
#include "observer/omt/ob_server_runtime.h"
#include "observer/mysql/ob_sync_plan_driver.h"
#include "rpc/obmysql/packet/ompk_eof.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "sql/pl/ob_pl_server_cursor.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace obmysql;
using namespace rpc;
using namespace sql;
using namespace pl;
namespace observer
{
ObMPStmtFetch::ObMPStmtFetch(const share::ObGlobalContext &gctx)
    : ObMPBase(gctx),
      cursor_id_(OB_INVALID_ID),
      fetch_rows_(OB_INVALID_COUNT),
      single_process_timestamp_(0),
      exec_start_timestamp_(0),
      exec_end_timestamp_(0)
{
}
int ObMPStmtFetch::before_process()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObMPBase::before_process())) {
    LOG_WARN("fail to call before process", K(ret));
  } else if ((OB_ISNULL(req_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("request should not be null", K(ret));
  } else if (req_->get_type() != ObRequest::OB_MYSQL) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid request", K(ret), K_(*req));
  } else {
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    ObString tail;
    if (OB_UNLIKELY(ObMySQLCommandLayout::FETCH != pkt.get_command_layout())) {
      ret = OB_INVALID_DATA;
      LOG_WARN("unexpected stmt-fetch command layout", K(ret),
               K(pkt.get_command_layout()));
    } else if (OB_FAIL(pkt.get_command_field(0, tail))) {
      LOG_WARN("get rust parsed stmt-fetch tail failed", K(ret));
    } else if (!tail.empty()) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not support offset type in mysql mode.", K(ret),
               K(pkt.get_command_scalar0()));
    } else {
      cursor_id_ = static_cast<uint32_t>(pkt.get_command_scalar0());
      fetch_rows_ = static_cast<int32_t>(pkt.get_command_scalar1());
    }
  }
  if (OB_FAIL(ret)) {
    send_error_packet(ret, NULL);
    if (OB_ERR_PREPARE_STMT_CHECKSUM == ret) {
      force_disconnect();
      LOG_ERROR("prepare stmt checksum error, disconnect connection", K(ret));
    }
    flush_buffer(true);
  }
  return ret;
}
int ObMPStmtFetch::set_session_active(ObSQLSessionInfo &session) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(session.set_session_state(QUERY_ACTIVE))) {
    LOG_WARN("fail to set session state", K(ret));
  } else {
    session.set_query_start_time(get_receive_timestamp());
    session.set_mysql_cmd(obmysql::COM_STMT_FETCH);
    session.update_last_active_time();
    session.set_is_request_end(false);
  }
  return ret;
}
int ObMPStmtFetch::do_process(ObSQLSessionInfo &session,
                              bool &need_response_error)
{
  int ret = OB_SUCCESS;
  ObAuditRecordData &audit_record = session.get_raw_audit_record();
  ObExecutingSqlStatRecord sqlstat_record;
  audit_record.try_cnt_++;
  const bool enable_sqlstat = session.is_sqlstat_enabled();
  single_process_timestamp_ = ObTimeUtility::current_time();
  ObPLCursorInfo *cursor = session.get_cursor(cursor_id_);
  if (OB_ISNULL(cursor)) {
    ret = OB_ERR_FETCH_OUT_SEQUENCE;
    LOG_WARN("cursor not found", K(cursor_id_), K(ret));
    //If a cursor is not found during the fetch process for any reason, immediately disconnect and let the application handle the fault tolerance
    //disconnect();
  } else if (!cursor->is_ps_cursor()) {
    ret = OB_ERR_FETCH_OUT_SEQUENCE;
    LOG_WARN("cursor is not a prepared-statement server cursor", K(cursor_id_), K(ret));
  } else {
    int64_t fetch_limit = OB_INVALID_COUNT == fetch_rows_ ? INT64_MAX : fetch_rows_;
    int64_t true_row_num = 0;
    {
      //Record the execution wait time of sql_audit, which depends on the end of the lifecycle of max_wait_guard and total_wait_guard,
      //Therefore, the destructor of total_wait_guard should be called before the audit record statistics logic
      int64_t execution_id = 0;
      {
        audit_record.exec_record_.record_start();
      }
      if (enable_sqlstat) {
        sqlstat_record.record_sqlstat_start_value(
            *session.get_query_runtime_environment());
        sqlstat_record.set_is_in_retry(session.get_is_in_retry());
        session.sql_sess_record_sql_stat_start_value(sqlstat_record);
      }
      if (OB_ISNULL(::oceanbase::observer::get_observer_sql_engine())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid sql engine", K(ret), K(gctx_));
      } else if (FALSE_IT(execution_id = ::oceanbase::observer::get_observer_sql_engine()->get_execution_id())) {
        //nothing to do
      } else if (OB_FAIL(set_session_active(session))) {
        LOG_WARN("fail to set session active", K(ret));
      } else {
        exec_start_timestamp_ = ObTimeUtility::current_time();
      }
      if (OB_SUCC(ret)) {
        //Monitoring item statistics start
        exec_start_timestamp_ = ObTimeUtility::current_time();
        // All errors within this branch will be handled properly inside response_result
        // No need to handle the error response packet additionally
        session.set_current_execution_id(execution_id);
        OX(need_response_error = false);
        OZ(response_result(static_cast<ObPLServerCursorInfo &>(*cursor),
                           session,
                           fetch_limit,
                           true_row_num));
        if (OB_READ_NOTHING == ret) {
          LOG_WARN("nothing to read", K(ret));
          ret = OB_SUCCESS;
        }
        OX(need_response_error = true);
      }
    }
    //Monitoring item statistics end
    exec_end_timestamp_ = ObTimeUtility::current_time();

    // some statistics must be recorded for plan stat, even though sql audit disabled
    bool first_record = (1 == audit_record.try_cnt_);
    ObExecStatUtils::record_exec_timestamp(*this, first_record, audit_record.exec_timestamp_);
    audit_record.exec_timestamp_.update_stage_time();

    {
      audit_record.exec_record_.record_end();
      audit_record.stmt_type_ = stmt::T_EXECUTE;
      audit_record.update_event_stage_state();
    }

    if (enable_sqlstat) {
      sqlstat_record.record_sqlstat_end_value(
          *session.get_query_runtime_environment());
      sqlstat_record.inc_fetch_cnt();
      ObString sql = ObString::make_empty_string();
      if (OB_NOT_NULL(cursor)) {
        ObPsStmtInfoGuard guard;
        ObPsStmtInfo *ps_info = NULL;
        ObPsStmtId inner_stmt_id = OB_INVALID_ID;
        if (OB_SUCC(session.get_inner_ps_stmt_id(cursor_id_, inner_stmt_id))
              && OB_SUCC(session.get_ps_cache()->get_stmt_info_guard(inner_stmt_id, guard))
              && OB_NOT_NULL(ps_info = guard.get_stmt_info())) {
          sql = ps_info->get_ps_sql();
        } else {
          LOG_WARN("get sql fail in fetch", K(ret), K(cursor_id_), K(cursor->get_id()));
        }
      }
      sqlstat_record.move_to_sqlstat_cache(session, sql);
    }
    session.set_show_warnings_buf(ret); // TODO: Move this to a better place, reduce some wb copy

    clear_wb_content(session);
  }
  return ret;
}

int ObMPStmtFetch::response_query_header(ObSQLSessionInfo &session, 
                                         const ColumnsFieldArray *fields)
{
  // TODO: Add handling for com type
  int ret = OB_SUCCESS;
  bool ac = true;
  ObSqlCtx ctx;
  ObQueryRetryCtrl retry_ctrl;
  ObSyncPlanDriver drv(gctx_, ctx, session, retry_ctrl, packet_sender_,
                       OB_INVALID_COUNT);
  if (NULL == fields) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(drv.response_query_header(*fields, false, false))) {
    LOG_WARN("fail to get autocommit", K(ret));
  }
  return ret;
}

/* Fetch protocol sends rows from a MySQL prepared-statement server cursor.
 * Protocol notes: MySQL does not require a head packet before returning each result set
 * Memory usage notes: When obtaining rows to be returned by fetch, note that you need to switch to the allocator where the cursor is located
 */
int ObMPStmtFetch::response_result(pl::ObPLServerCursorInfo &cursor,
                                   ObSQLSessionInfo &session,
                                   int64_t fetch_limit,
                                   int64_t &row_num)
{
  int ret = OB_SUCCESS;
  bool process_ok = false;
  // for select SQL
  bool ac = true;
  bool admission_fail_and_need_retry = false;
  bool last_row = false;
  int64_t max_count = 0;
  row_num = 0;

  if (OB_FAIL(session.get_autocommit(ac))) {
    LOG_WARN("fail to get autocommit", K(ret));
  } else {
    CK (OB_NOT_NULL(cursor.get_cursor_entity()));
    if (OB_SUCC(ret)) {
      WITH_CONTEXT(cursor.get_cursor_entity()) {
        lib::ContextTLOptGuard guard(false);
        ParamStore params;
        ObExecContext *exec_ctx = NULL;
        bool need_fetch = true;
        int64_t cur = 0;
        const ColumnsFieldArray *fields = NULL;
        ObArenaAllocator allocator(ObModIds::OB_SQL_EXECUTOR);
        ObSchemaGetterGuard schema_guard;
        SMART_VAR(ObExecContext, tmp_exec_ctx, allocator) {
          if (cursor.is_streaming()) {
            CK (OB_NOT_NULL(cursor.get_cursor_handler()));
            CK (OB_NOT_NULL(cursor.get_cursor_handler()->get_result_set()));
            OX (fields = dynamic_cast<const common::ColumnsFieldArray *>(
              cursor.get_cursor_handler()->get_result_set()->get_field_columns()));
          } else {
            fields = &cursor.get_field_columns();
          }
          if (OB_SUCC(ret)) {
            if (cursor.is_streaming()) {
              // Streaming result set requires exec_ctx, cannot be replaced by temporary result
              if (OB_NOT_NULL(cursor.get_cursor_handler()) &&
                  OB_NOT_NULL(cursor.get_cursor_handler()->get_result_set())){
                exec_ctx = &cursor.get_cursor_handler()->get_result_set()->get_exec_context();
              } else {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("get unexpect streaming result set.", K(ret), K(cursor.get_id()));
              }
            } else {
              tmp_exec_ctx.set_my_session(&session);
              tmp_exec_ctx.set_mem_attr(ObMemAttr(ObModIds::OB_SQL_EXEC_CONTEXT,
                                                  ObCtxIds::EXECUTE_CTX_ID));
              exec_ctx = &tmp_exec_ctx;
              if (OB_ISNULL(cursor.get_spi_cursor())) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("cursor result set is null.", K(ret), K(cursor.get_id()));
              } else {
                ObSPICursor *spi_cursor = cursor.get_spi_cursor();
                cur = cursor.get_current_position() + 1;
                max_count = spi_cursor->row_store_.get_row_cnt();
                if (OB_SUCC(ret) && (cur >= max_count || cur < 0 || max_count <= 0)) {
                  // During fetch, all errors except OB_ITER_END disconnect the connection.
                  // If the scan exceeds the range, set OB_ITER_END, report no error,
                  // and return no data.
                  need_fetch = false;
                  ret = OB_ITER_END;
                }
                if (OB_SUCC(ret)) {
                  OZ (cursor.set_current_position(cur));
                }
              }
            }
          }
          if (OB_FAIL(ret)) {
            // do nothing
          } else if (OB_FAIL(gctx_.schema_service_->get_runtime_schema_guard(schema_guard))) {
            LOG_WARN("get runtime schema guard failed ", K(ret));
          }
          ObPLExecCtx pl_ctx(cursor.get_allocator(), exec_ctx, &params,
                            NULL/*result*/, &ret, NULL/*func*/, true);
          while (OB_SUCC(ret) && need_fetch && row_num < fetch_limit
                  && OB_SUCC(sql::ObSPIService::fetch_server_cursor(&pl_ctx, cursor))) {
            common::ObNewRow &row = cursor.get_current_row();
#ifndef NDEBUG
            LOG_INFO("cursor fetch: ", K(cursor.get_id()),
                                       K(cursor.is_streaming()),
                                       K(cursor.is_ps_cursor()),
                                       K(cursor.get_current_row().cells_[0]),
                                       K(cursor.get_current_position()),
                                       K(row_num), K(fetch_limit));
#endif
            cur = cursor.get_current_position();
            ++cur;
            cursor.set_current_position(cur);
            OZ (response_row(session, row, fields, cursor.is_packed(), exec_ctx, &schema_guard));
            if (OB_SUCC(ret)) {
              ++row_num;
            } else {
              LOG_WARN("response row fail at line: ", K(ret), K(row_num));
            }
          }
          if (need_fetch) {
            cur = cursor.get_current_position();
            cur = cur - 1;
            cursor.set_current_position(cur);
          }
          if (OB_ITER_END == ret || OB_READ_NOTHING == ret) {
            ret = OB_SUCCESS;
            // need_fetch is true and got the OB_ITER_END error code, which means the last row was found normally, need to set last_row
            if (need_fetch || !cursor.is_scrollable()) {
              last_row = true;
            }
          }
        }
      }
      if (OB_FAIL(ret)) {
        LOG_WARN("response query result fail", K(ret));
      } else {
        process_ok = true;
        OMPKEOF eofp;
        const ObWarningBuffer *warnings_buf = common::ob_get_tsi_warning_buffer();
        uint16_t warning_count = 0;
        if (OB_ISNULL(warnings_buf)) {
          // ignore ret
          LOG_WARN("can not get thread warnings buffer");
        } else {
          warning_count = static_cast<uint16_t>(warnings_buf->get_readable_warning_count());
        }
        eofp.set_warning_count(warning_count);
        ObServerStatusFlags flags = eofp.get_server_status();
        flags.status_flags_.OB_SERVER_STATUS_IN_TRANS
          = (session.is_server_status_in_transaction() ? 1 : 0);
        flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT = (ac ? 1 : 0);
        flags.status_flags_.OB_SERVER_MORE_RESULTS_EXISTS = false; /*no more result*/
        flags.status_flags_.OB_SERVER_STATUS_CURSOR_EXISTS = !last_row ? 1 : 0;
        if ((!cursor.is_streaming()
             && max_count == cursor.get_current_position() + 1)
              || last_row) {
          flags.status_flags_.OB_SERVER_STATUS_LAST_ROW_SENT = 1;
        } else {
          flags.status_flags_.OB_SERVER_STATUS_LAST_ROW_SENT = 0;
        }
        eofp.set_server_status(flags);
        if (OB_SUCC(ret)) {
          if (OB_FAIL(response_packet(eofp))) {
            LOG_WARN("response packet fail", K(ret));
          }
        }
      }
    }
  }
  if (OB_FAIL(ret) &&
      !process_ok &&
      !admission_fail_and_need_retry) {
    int sret = OB_SUCCESS;
    if (OB_SUCCESS != (sret = send_error_packet(ret, NULL))) {
      LOG_WARN("send error packet fail", K(sret), K(ret));
    }
  }
  return ret;
}

int ObMPStmtFetch::process_fetch_stmt(ObSQLSessionInfo &session,
                                      bool &need_response_error)
{
  int ret = OB_SUCCESS;
  // After executing setup_wb, all WARNINGS will be written to the WARNING BUFFER of the current session
  setup_wb(session);
  //set session log_level.Must use ObThreadLogLevelUtils::clear() in pair
  ObThreadLogLevelUtils::init(session.get_log_id_level_map());
  // Clients may use 'SET @@last_schema_version = xxxx' to publish a newer schema
  // version; observer refreshes when its local version is older.
  if (OB_FAIL(check_and_refresh_schema())) {
    LOG_WARN("failed to check_and_refresh_schema", K(ret));
  } else {
    ret = do_process(session, need_response_error);
  }
  ObThreadLogLevelUtils::clear();
  const int64_t debug_sync_timeout = GCONF.debug_sync_timeout;
  if (debug_sync_timeout > 0) {
    // ignore thread local debug sync actions to session actions failed
    int tmp_ret = OB_SUCCESS;
    tmp_ret = GDS.collect_result_actions(session.get_debug_sync_actions());
    if (OB_UNLIKELY(OB_SUCCESS != tmp_ret)) {
      LOG_WARN("set thread local debug sync actions to session actions failed", K(tmp_ret));
    }
  }
  //For the handling of tracelog, it does not affect the normal logic, and the error code does not need to be assigned to ret
  {
    int tmp_ret = OB_SUCCESS;
    //Clear WARNING BUFFER
    tmp_ret = do_after_process(session, false/*no asyn response*/, ret);
    UNUSED(tmp_ret);
  }
  return ret;
}
int ObMPStmtFetch::process()
{
  int ret = OB_SUCCESS;
  int flush_ret = OB_SUCCESS;
  bool need_disconnect = true;
  bool need_response_error = true;
  ObSQLSessionInfo *sess = NULL;
  int64_t query_timeout = 0;
  ObCurTraceId::TraceId *cur_trace_id = ObCurTraceId::get_trace_id();
  ObSMConnection *conn = get_conn();
  bool cursor_fetched = false;
  reset_close_cursor();
  if (OB_ISNULL(req_) || OB_ISNULL(conn) || OB_ISNULL(cur_trace_id)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null conn ptr", K_(cursor_id), K_(req), K(cur_trace_id), K(ret));
  } else if (OB_UNLIKELY(!conn->is_in_authed_phase())) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("receive sql without session", K_(cursor_id), K(ret));
  } else if (OB_ISNULL(conn->runtime_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid runtime", K_(cursor_id), K(conn->runtime_), K(ret));
  } else if (OB_FAIL(get_session(sess))) {
    LOG_WARN("get session fail", K_(cursor_id), K(ret));
  } else if (OB_ISNULL(sess)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL or invalid", K_(cursor_id), K(sess), K(ret));
  } else {
    ObSQLSessionInfo &session = *sess;
    int64_t runtime_version = 0;
    THIS_WORKER.set_session(sess);
    ObSQLSessionInfo::LockGuard lock_guard(session.get_query_lock());
    session.set_current_trace_id(ObCurTraceId::get_trace_id());
    session.get_raw_audit_record().request_memory_used_ = 0;
    observer::ObProcessMallocCallback pmcb(0,
          session.get_raw_audit_record().request_memory_used_);
    lib::ObMallocCallbackGuard guard(pmcb);
    int64_t packet_len = (reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet())).get_clen();
    if (OB_UNLIKELY(!session.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid session", K_(cursor_id), K(ret));
    } else if (OB_UNLIKELY(session.is_zombie())) {
      //session has been killed some moment ago
      ret = OB_ERR_SESSION_INTERRUPTED;
      LOG_WARN("session has been killed", K(session.get_session_state()), K_(cursor_id),
               K(session.get_server_sid()), K(ret));
    } else if (OB_UNLIKELY(packet_len > session.get_max_packet_size())) {
      //packet size check with session variable max_allowd_packet or net_buffer_length
      ret = OB_ERR_NET_PACKET_TOO_LARGE;
      LOG_WARN("packet too large than allowed for the session", K_(cursor_id), K(ret));
    } else if (OB_FAIL(session.get_query_timeout(query_timeout))) {
      LOG_WARN("fail to get query timeout", K(ret));
    } else if (OB_FAIL(gctx_.schema_service_->get_published_schema_version(
                runtime_version))) {
      LOG_WARN("fail to get runtime broadcast version", K(ret));
    } else {
      need_disconnect = false;
      ObPLCursorInfo *cursor = NULL;
      THIS_WORKER.set_timeout_ts(get_receive_timestamp() + query_timeout);
      ret = process_fetch_stmt(session, need_response_error);
      // set cursor fetched info. if cursor has be fetched, we need to disconnect
      cursor = session.get_cursor(cursor_id_);
      if (OB_NOT_NULL(cursor) && cursor->get_fetched()) {
        cursor_fetched = true;
      }
      if (need_close_cursor()) {
        // close at here because after do_process, need read some cursor info for log in process_fetch_stmt
        int tmp_ret = session.close_cursor(cursor_id_);
        ret = ret == OB_SUCCESS ? tmp_ret : ret;
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("no scrollable cursor close cursor failed at last row.", K(tmp_ret));
        }
      }
    }
    session.check_and_reset_retry_info(*cur_trace_id, THIS_WORKER.need_retry());
    session.set_last_trace_id(ObCurTraceId::get_trace_id());
  }

  if (OB_FAIL(ret) && is_conn_valid()) {
    if (need_response_error) {
      send_error_packet(ret, NULL);
    }
    if (cursor_fetched || need_disconnect) {
      force_disconnect();
      LOG_WARN("disconnect connection when process query", K(ret));
    }
  }

  if (!THIS_WORKER.need_retry()) {
    flush_ret = flush_buffer(true);
  }
  THIS_WORKER.set_session(NULL);
  if (sess != NULL) {
    revert_session(sess); //current ignore revert session ret
  }
  return (OB_SUCCESS != ret) ? ret : flush_ret;
}

} //end of namespace observer
} //end of namespace oceanbase
