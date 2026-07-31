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

#include "observer/mysql/obmp_stmt_prepare.h"
#include "observer/mysql/ob_mysql_result_set.h"
#include "lib/trace/ob_trace.h"

#include "rpc/obmysql/packet/ompk_prepare.h"
#include "rpc/obmysql/packet/ompk_field.h"
#include "observer/omt/ob_server_runtime.h"
#include "sql/ob_sql.h"

namespace oceanbase
{

using namespace rpc;
using namespace common;
using namespace share;
using namespace obmysql;
using namespace sql;

namespace observer
{

ObMPStmtPrepare::ObMPStmtPrepare(const share::ObGlobalContext &gctx)
    : ObMPBase(gctx),
      retry_ctrl_(/*ctx_.retry_info_*/),
      sql_(),
      sql_len_(),
      single_process_timestamp_(0),
      exec_start_timestamp_(0),
      exec_end_timestamp_(0)
{
  ctx_.exec_type_ = MpQuery;
}

int ObMPStmtPrepare::deserialize()
{
  int ret = OB_SUCCESS;
  if ((OB_ISNULL(req_)) || (req_->get_type() != ObRequest::OB_MYSQL)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid request", K(ret), K(req_));
  } else {
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    if (OB_UNLIKELY(ObMySQLCommandLayout::BYTES != pkt.get_command_layout())) {
      ret = OB_INVALID_DATA;
      LOG_WARN("unexpected prepare command layout", K(ret),
               K(pkt.get_command_layout()));
    } else if (OB_FAIL(pkt.get_command_field(0, sql_))) {
      LOG_WARN("get rust parsed prepare text failed", K(ret));
    }
  }

  return ret;
}

int ObMPStmtPrepare::multiple_query_check(ObSQLSessionInfo &session,
                                          ObString &sql,
                                          bool &force_sync_resp,
                                          bool &need_response_error)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(1 == session.get_capability().cap_flags_.OB_CLIENT_MULTI_STATEMENTS)) {
    ObSEArray<ObString, 1> queries;
    ObParser parser(THIS_WORKER.get_allocator(),
                    session.get_sql_mode(), session.get_charsets4parser());
    bool parse_fail = false;
    ObMPParseStat parse_stat;
    force_sync_resp = true;
    /* MySQL behavior when handling Multi-Stmt errors:
      * After encountering the first failed SQL (including parsing or execution), stop reading subsequent data
      *  For example:
      *  (1) select 1; selct 2; select 3;
      *  select 1 executes successfully, selct 2 reports a syntax error, select 3 is not executed
      *  (2) select 1; drop table not_exists_table; select 3;
      *  select 1 executes successfully, drop table not_exists_table reports a table does not exist error, select 3 is not executed
      *
      * Special note:
      * split_multiple_stmt splits statements based on semicolons, but there might be "syntax errors",
      * here "syntax error" does not mean select is written as selct, but "token" level syntax errors, for example the statement
      * select 1;`select 2; select 3;
      * In the above example, neither ` nor ' form closed string tokens, the token parser will report a syntax error
      * In the above example, the queries.count() equals 2, which are select 1 and `select 2; select 3;
      */
    ret = parser.split_multiple_stmt(sql, queries, parse_stat, false, true);
    if (OB_SUCC(ret)) { // ret=SUCC does not necessarily mean that parse was successful, the last query may have failed to parse
      if (OB_UNLIKELY(queries.count() <= 0)) {
        LOG_ERROR("emtpy query count. client would have suspended. never be here!",
                  K(sql), K(parse_fail));
      } else if (queries.count() > 1) {
        ret = OB_NOT_SUPPORTED;
        need_response_error = true;
        LOG_WARN("can't not prepare multi stmt", K(ret), K(queries.count()));
      } else {
        if (OB_UNLIKELY(parse_stat.parse_fail_ && (0 == parse_stat.fail_query_idx_)
                        && ObSQLUtils::check_need_disconnect_parser_err(parse_stat.fail_ret_))) {
          // Enter this branch, indicating that parsing of a query in multi_query failed, if not due to a syntax error, then enter this branch
          // If the current query_count is 1, then keep connecting; if greater than 1,
          // then it is necessary to disconnect after sending the error packet to prevent the client from waiting indefinitely for the next response
          // This change is to solve
          ret = parse_stat.fail_ret_;
          need_response_error = true;
        }
      }
    } else {
      // Enter this branch, indicating that push_back failed due to OOM, delegate the outer code to return an error code
      // and after entering this branch, the connection should be terminated
      need_response_error = true;
      LOG_WARN("need response error", K(ret));
    }
  }
  return ret;
}

int ObMPStmtPrepare::process()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = NULL;
  bool need_response_error = true;
  bool async_resp_used = false; // Asynchronously reply to the client by the transaction commit thread
  int64_t query_timeout = 0;
  ObSMConnection *conn = get_conn();
  bool need_disconnect = true;

  if (OB_ISNULL(req_) || OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("req or conn is null", K_(req), K(conn), K(ret));
  } else if (OB_UNLIKELY(!conn->is_in_authed_phase())) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("receive sql without session", K_(sql), K(ret));
  } else if (OB_ISNULL(conn->runtime_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid runtime", K_(sql), K(conn->runtime_), K(ret));
  } else if (OB_FAIL(get_session(sess))) {
    LOG_WARN("get session fail", K_(sql), K(ret));
  } else if (OB_ISNULL(sess)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL or invalid", K_(sql), K(sess), K(ret));
  } else {
    ObSQLSessionInfo &session = *sess;
    THIS_WORKER.set_session(sess);
    ObSQLSessionInfo::LockGuard lock_guard(session.get_query_lock());
    SQL_INFO_GUARD(ctx_.cur_sql_, ObString(ctx_.sql_id_));
    session.set_current_trace_id(ObCurTraceId::get_trace_id());
    session.get_raw_audit_record().request_memory_used_ = 0;
    observer::ObProcessMallocCallback pmcb(0,
          session.get_raw_audit_record().request_memory_used_);
    lib::ObMallocCallbackGuard guard(pmcb);
    int64_t database_schema_version = 0;
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    int64_t packet_len = pkt.get_clen();
    if (OB_UNLIKELY(!session.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid session", K_(sql), K(ret));
    } else if (OB_UNLIKELY(session.is_zombie())) {
      ret = OB_ERR_SESSION_INTERRUPTED;
      LOG_WARN("session has been killed", K(session.get_session_state()), K_(sql),
               K(session.get_server_sid()), K(ret));
    } else if (OB_FAIL(session.get_query_timeout(query_timeout))) {
      LOG_WARN("fail to get query timeout", K_(sql), K(ret));
    } else if (OB_FAIL(gctx_.schema_service_->get_published_schema_version(
                database_schema_version))) {
      LOG_WARN("fail to get published database schema version", K(ret));
    } else if (OB_UNLIKELY(packet_len > session.get_max_packet_size())) {
      ret = OB_ERR_NET_PACKET_TOO_LARGE;
      need_disconnect = false;
      LOG_WARN("packet too large than allowd for the session", K_(sql), K(ret));
    } else {
      THIS_WORKER.set_timeout_ts(get_receive_timestamp() + query_timeout);
      retry_ctrl_.set_current_global_schema_version(database_schema_version);
      session.set_pl_can_retry(true);

      bool has_more = false;
      bool force_sync_resp = false;
      need_disconnect = false;
      need_response_error = false;
      if (OB_FAIL(multiple_query_check(session, sql_, force_sync_resp, need_response_error))) {
        need_disconnect = OB_NOT_SUPPORTED == ret ? false : true; 
        LOG_WARN("check multiple query fail.", K(ret));
      } else {
        ret = process_prepare_stmt(ObMultiStmtItem(false, 0, sql_), session, has_more, force_sync_resp, async_resp_used);
      }

      if (OB_FAIL(ret)) {
        // Log the current attempt; retryable errors are handled by the upper scheduler.
        if (is_conn_valid()) { // The SQL text may be request-owned after an async handoff.
          LOG_WARN("execute sql failed", "sql_id", ctx_.sql_id_, K_(sql), K(ret));
        } else {
          LOG_WARN("execute sql failed", K(ret));
        }
      }
    }

    if (!session.get_in_transaction()) {
        // transcation ends, end trace
    }

    if (OB_FAIL(ret) && is_conn_valid()) {
      if (need_response_error) {
        send_error_packet(ret, NULL);
      }
      if (need_disconnect) {
        force_disconnect();
        LOG_WARN("disconnect connection when process query", K(ret));
      }
    }

    session.set_last_trace_id(ObCurTraceId::get_trace_id());
    THIS_WORKER.set_session(NULL);
    revert_session(sess); //current ignore revert session ret
  }
  return ret;
}

int ObMPStmtPrepare::process_prepare_stmt(const ObMultiStmtItem &multi_stmt_item,
                                          ObSQLSessionInfo &session,
                                          bool has_more_result,
                                          bool force_sync_resp,
                                          bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  bool need_response_error = true;
  int64_t database_schema_version = 0;
  setup_wb(session);

  if (OB_FAIL(init_process_var(ctx_, multi_stmt_item, session))) {
    LOG_WARN("init process var faield.", K(ret), K(multi_stmt_item));
  } else {
    ObThreadLogLevelUtils::init(session.get_log_id_level_map());
    if (OB_FAIL(check_and_refresh_schema())) {
      LOG_WARN("failed to check_and_refresh_schema", K(ret));
    } else if (OB_FAIL(session.update_timezone_info())) {
      LOG_WARN("fail to update time zone info", K(ret));
    } else {
      ctx_.self_add_plan_ = false;
      ctx_.is_prepare_protocol_ = true; //set to prepare protocol
      ctx_.is_prepare_stage_ = true;
      need_response_error = false;
      do {
        // reset `ret` explicitly before local retry
        ret = OB_SUCCESS;
        share::schema::ObSchemaGetterGuard schema_guard;
        retry_ctrl_.clear_state_before_each_retry(session.get_retry_info_for_update());
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(gctx_.schema_service_->get_runtime_schema_guard(
                    schema_guard))) {
          LOG_WARN("get schema guard failed", K(ret));
        } else if (OB_FAIL(schema_guard.get_schema_version(
                    database_schema_version))) {
          LOG_WARN("fail get schema version", K(ret));
        } else {
          ctx_.schema_guard_ = &schema_guard;
          retry_ctrl_.set_current_local_schema_version(database_schema_version);
        }
        if (OB_SUCC(ret)) {
          ret = do_process(session,
                           has_more_result,
                           force_sync_resp,
                           async_resp_used);
          session.set_session_in_retry(retry_ctrl_.need_retry());
        }
      } while (RETRY_TYPE_LOCAL == retry_ctrl_.get_retry_type());
      if (OB_SUCC(ret) && retry_ctrl_.get_retry_times() > 0) {
        LOG_TRACE("sql retry succeed", K(ret),
                  "retry_times", retry_ctrl_.get_retry_times(), K(multi_stmt_item));
      }
    }
    ObThreadLogLevelUtils::clear();
  }
  //For the handling of tracelog, it does not affect the normal logic, and the error code does not need to be assigned to ret
  int tmp_ret = OB_SUCCESS;
  //Clear WARNING BUFFER
  tmp_ret = do_after_process(session, async_resp_used, ret);
  // the need_response_error variable ensures that it only occurs in
  // do { do_process } while(retry) will only occur if an error happens before
  // Walk to the send_error_packet logic
  // So there is no need to consider whether the current mode is sync or async
  if (!OB_SUCC(ret) && need_response_error && is_conn_valid()) {
    send_error_packet(ret, NULL);
  }
  UNUSED(tmp_ret);
  return ret;
}

int ObMPStmtPrepare::check_and_refresh_schema()
{
  int ret = OB_SUCCESS;
  int64_t local_version = 0;
  int64_t last_version = 0;

  if (OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null schema service", K(ret), K(gctx_));
  } else {
    if (OB_ISNULL(ctx_.session_info_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid session info", K(ret), K(ctx_.session_info_));
    } else if (OB_FAIL(gctx_.schema_service_->get_runtime_refreshed_schema_version(local_version))) {
      LOG_WARN("fail to get refreshed runtime schema version", K(ret));
    } else if (FALSE_IT(last_version = ctx_.session_info_->get_last_ddl_schema_version())) {
    } else if (local_version >= last_version) {
      // skip
    } else if (OB_FAIL(gctx_.schema_service_->async_refresh_schema(last_version))) {
      LOG_WARN("failed to refresh schema", K(ret), K(1UL), K(last_version));
    }
  }
  return ret;
}

int ObMPStmtPrepare::do_process(ObSQLSessionInfo &session,
                                const bool has_more_result,
                                const bool force_sync_resp,
                                bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  ObAuditRecordData &audit_record = session.get_raw_audit_record();
  ObExecutingSqlStatRecord sqlstat_record;
  audit_record.try_cnt_++;
  const bool enable_sqlstat = session.is_sqlstat_enabled();
  single_process_timestamp_ = ObTimeUtility::current_time();
  bool is_diagnostics_stmt = false;
  bool need_response_error = true;
  const ObString &sql = ctx_.multi_stmt_item_.get_sql();
  ObPsStmtId inner_stmt_id = OB_INVALID_ID;

  /* !!!
   * Note that req_timeinfo_guard must be placed before result
   * !!!
   */
  ObReqTimeGuard req_timeinfo_guard;
  SMART_VAR(ObMySQLResultSet, result, session, THIS_WORKER.get_allocator()) {
    {
      {
        audit_record.exec_record_.record_start();
      }
      if (enable_sqlstat) {
        sqlstat_record.record_sqlstat_start_value(
            *session.get_query_runtime_environment());
        sqlstat_record.set_is_in_retry(session.get_is_in_retry());
        session.sql_sess_record_sql_stat_start_value(sqlstat_record);
      }
      result.set_has_more_result(has_more_result);
      ObSqlExecutorCtx *task_ctx = result.get_exec_context().get_sql_executor_ctx();
      int64_t execution_id = 0;
      if (OB_ISNULL(task_ctx)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("task executor ctx can not be NULL", K(task_ctx), K(ret));
      } else {
        task_ctx->set_query_begin_schema_version(retry_ctrl_.get_current_global_schema_version());
        ctx_.retry_times_ = retry_ctrl_.get_retry_times();
        if (OB_ISNULL(ctx_.schema_guard_)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("newest schema is NULL", K(ret));
        } else if (OB_FAIL(result.init())) {
          LOG_WARN("result set init failed", K(ret));
        } else if (OB_ISNULL(::oceanbase::observer::get_observer_sql_engine())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("invalid sql engine", K(ret), K(gctx_));
        } else if (FALSE_IT(execution_id = ::oceanbase::observer::get_observer_sql_engine()->get_execution_id())) {
          //nothing to do
        } else if (OB_FAIL(set_session_active(sql, session, ObTimeUtil::current_time(), obmysql::ObMySQLCmd::COM_STMT_PREPARE))) {
          LOG_WARN("fail to set session active", K(ret));
        } else if (OB_FAIL(::oceanbase::observer::get_observer_sql_engine()->stmt_prepare(sql, ctx_, result, false/*is_inner_sql*/))) {
          exec_start_timestamp_ = ObTimeUtility::current_time();
          int cli_ret = OB_SUCCESS;
          retry_ctrl_.test_and_save_retry_state(gctx_, ctx_, result, ret, cli_ret);
          LOG_WARN("run stmt_query failed, check if need retry",
                   K(ret), K(cli_ret), K(retry_ctrl_.need_retry()), K(sql));
          ret = cli_ret;
        } else if (common::OB_INVALID_ID != result.get_statement_id()
                   && OB_FAIL(session.get_inner_ps_stmt_id(result.get_statement_id(), inner_stmt_id))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ps : get inner stmt id fail.", K(ret), K(result.get_statement_id()));
        } else {
          //Monitoring item statistics start
          exec_start_timestamp_ = ObTimeUtility::current_time();
          // All errors within this branch will be handled properly inside response_result
          // No need to handle the error response packet additionally
          need_response_error = false;
          is_diagnostics_stmt = ObStmt::is_diagnostic_stmt(result.get_literal_stmt_type());
          ctx_.is_show_trace_stmt_ = ObStmt::is_show_trace_stmt(result.get_literal_stmt_type());
          session.set_current_execution_id(execution_id);

          //response_result
          if (OB_SUCC(ret) && OB_FAIL(response_result(result,
                                                      session,
                                                      force_sync_resp,
                                                      async_resp_used))) {
            ObPhysicalPlanCtx *plan_ctx = result.get_exec_context().get_physical_plan_ctx();
            if (OB_ISNULL(plan_ctx)) {
              // ignore ret
              LOG_ERROR("execute query fail, and plan_ctx is NULL", K(ret));
            } else {
              LOG_WARN("execute query fail", K(ret), "timeout_timestamp",
                      plan_ctx->get_timeout_timestamp());
            }
          }
          //Monitoring item statistics end
          exec_end_timestamp_ = ObTimeUtility::current_time();

          // some statistics must be recorded for plan stat, even though sql audit disabled
          bool first_record = (1 == audit_record.try_cnt_);
          ObExecStatUtils::record_exec_timestamp(*this, first_record, audit_record.exec_timestamp_);
          audit_record.exec_timestamp_.update_stage_time();
        }
      }
    } // diagnose end

    {
      audit_record.exec_record_.record_end();
      audit_record.update_event_stage_state();
      if (!THIS_THWORKER.need_retry()) {
        const int64_t time_cost = exec_end_timestamp_ - get_receive_timestamp();
      }
    }
    if (enable_sqlstat) {
      sqlstat_record.record_sqlstat_end_value(
          *session.get_query_runtime_environment());
      sqlstat_record.set_rows_processed(result.get_affected_rows() + result.get_return_rows());
      sqlstat_record.set_partition_cnt(result.get_exec_context().get_das_ctx().get_related_tablet_cnt());
      sqlstat_record.set_is_plan_cache_hit(ctx_.plan_cache_hit_);
      sqlstat_record.move_to_sqlstat_cache(result.get_session(),
                                                 ctx_.cur_sql_,
                                                 result.get_physical_plan());
    }
    // Retry needs to meet the following conditions:
    // 1. rs.open execution failed
    // 2. No result was returned to the client, this execution has no side effects
    // 3. need_retry(result, ret): schema or location cache invalidation
    // 4. less than retry count limit
    if (OB_UNLIKELY(retry_ctrl_.need_retry())) {
      LOG_WARN("try to execute again",
              K(ret),
              N_TYPE, result.get_stmt_type(),
              "retry_type", retry_ctrl_.get_retry_type(),
              "timeout_remain", THIS_WORKER.get_timeout_remain());
    } else {
      // store the warning message from the most recent statement in the current session
      if (OB_SUCC(ret) && is_diagnostics_stmt) {
        // if diagnostic stmt execute successfully, it dosen't clear the warning message
        session.update_show_warnings_buf();
      } else {
        session.set_show_warnings_buf(ret); // TODO: Move this to a better place, reduce some wb copy
      }

      if (!OB_SUCC(ret) && !async_resp_used && need_response_error && is_conn_valid() && !THIS_WORKER.need_retry()) {
        LOG_WARN("query failed", K(ret), K(retry_ctrl_.need_retry()), K_(sql));
        // When need_retry=false, a packet may have been sent to the client, or no packets may have been sent at all.
        // However, it can be determined: this request has errored, and is not yet complete. If it has not already been handed over to asynchronous EndTrans for finalization,
        // then it is necessary to reply with an error_packet below as a conclusion. Otherwise, no one will help send the error packet to the client afterwards,
        // May cause the client to hang waiting for a response.
        int err = send_error_packet(ret, NULL);
        if (OB_SUCCESS != err) {  // send error packet
          LOG_WARN("send error packet failed", K(ret), K(err));
        }
      }
    }
    bool need_retry = (THIS_THWORKER.need_retry()
                       || RETRY_TYPE_NONE != retry_ctrl_.get_retry_type());
  }

  // reset thread waring buffer in sync mode
  if (!async_resp_used) {
    clear_wb_content(session);
  }
  return ret;
}

// return false only if send packet fail.
int ObMPStmtPrepare::response_result(
    ObMySQLResultSet &result,
    ObSQLSessionInfo &session,
    bool force_sync_resp,
    bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  UNUSED(force_sync_resp);
  UNUSED(async_resp_used);
//  const ObMySQLRawPacket &packet = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
  if (OB_FAIL(send_prepare_packet(result))) {
    LOG_WARN("send prepare packet failed", K(ret));
  } else if (OB_FAIL(send_param_packet(session, result))) {
    LOG_WARN("send param packet failed", K(ret));
  } else if (OB_FAIL(send_column_packet(session, result))) {
    LOG_WARN("send column packet failed", K(ret));
  }
  return ret;
}

int ObMPStmtPrepare::send_prepare_packet(const ObMySQLResultSet &result)
{
  int ret = OB_SUCCESS;
  OMPKPrepare prepare_packet;
  const ParamsFieldIArray *params = result.get_param_fields();
  const ColumnsFieldIArray *columns = result.get_field_columns();
  if (OB_ISNULL(params) || OB_ISNULL(columns)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(columns), K(params));
  } else {
    prepare_packet.set_statement_id(static_cast<uint32_t>(result.get_statement_id()));
    prepare_packet.set_column_num(static_cast<uint16_t>(result.get_field_cnt()));
    prepare_packet.set_warning_count(static_cast<uint16_t>(result.get_warning_count()));
    if (OB_ISNULL(result.get_param_fields())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(result.get_param_fields()));
    } else {
      prepare_packet.set_param_num(
        static_cast<uint16_t>(result.get_param_fields()->count()));
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(response_packet(prepare_packet))) {
    LOG_WARN("response packet failed", K(ret));
  }

  return ret;
}

int ObMPStmtPrepare::send_column_packet(const ObSQLSessionInfo &session,
                                        ObMySQLResultSet &result)
{
  int ret = OB_SUCCESS;
  const ColumnsFieldIArray *columns = result.get_field_columns();
  if (OB_ISNULL(columns)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(columns));
  } else if (columns->count() > 0) {
    ObMySQLField field;
    ret = result.next_field(field);
    while (OB_SUCC(ret)) {
      OMPKField fp(field);
      if (OB_FAIL(response_packet(fp))) {
        LOG_WARN("response packet fail", K(ret));
      } else {
        LOG_DEBUG("response field succ", K(field));
        ret = result.next_field(field);
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(send_eof_packet(session, result))) {
        LOG_WARN("send eof field failed", K(ret));
      }
    }
  }
  return ret;
}

int ObMPStmtPrepare::send_param_packet(const ObSQLSessionInfo &session,
                                       ObMySQLResultSet &result)
{
  int ret = OB_SUCCESS;
  const ParamsFieldIArray *params = result.get_param_fields();
  const ColumnsFieldIArray *columns = result.get_field_columns();
  if (OB_ISNULL(params) || OB_ISNULL(columns)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(columns), K(params));
  } else if (params->count() > 0) {
    ObMySQLField field;
    ret = result.next_param(field);
    while (OB_SUCC(ret)) {
      OMPKField fp(field);
      if (OB_FAIL(response_packet(fp))) {
        LOG_DEBUG("response packet fail", K(ret));
      } else {
        //        LOG_INFO("response field succ", K(field));
        ret = result.next_param(field);
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(send_eof_packet(session, result))) {
        LOG_WARN("send eof field failed", K(ret));
      }
    }
  }
  return ret;
}

} //end of namespace observer
} //end of namespace oceanbase
