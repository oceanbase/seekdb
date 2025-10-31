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

#include "observer/mysql/obmp_query.h"

#include "share/ob_resource_limit.h"
#include "observer/mysql/ob_sync_plan_driver.h"
#include "observer/mysql/ob_sync_cmd_driver.h"
#include "observer/mysql/ob_async_cmd_driver.h"
#include "observer/mysql/ob_async_plan_driver.h"
#include "observer/omt/ob_tenant.h"
#include "observer/ob_server.h"
#include "sql/ob_sql_mock_schema_utils.h"

using namespace oceanbase::rpc;
using namespace oceanbase::obmysql;
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::trace;
using namespace oceanbase::sql;
void __attribute__((weak)) request_finish_callback();
ObMPQuery::ObMPQuery(const ObGlobalContext &gctx)
    : ObMPBase(gctx),
      single_process_timestamp_(0),
      exec_start_timestamp_(0),
      exec_end_timestamp_(0),
      is_com_filed_list_(false),
      params_value_len_(0),
      params_value_(NULL)
{
  ctx_.exec_type_ = MpQuery;
}


ObMPQuery::~ObMPQuery()
{
}


int ObMPQuery::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSQLSessionInfo *sess = NULL;
  bool need_response_error = true;
  bool need_disconnect = true;
  bool async_resp_used = false; // Asynchronously reply to the client by the transaction commit thread
  int64_t query_timeout = 0;
  ObCurTraceId::TraceId *cur_trace_id = ObCurTraceId::get_trace_id();
  ObSMConnection *conn = get_conn();
  static int64_t concurrent_count = 0;
  bool need_dec = false;
  bool do_ins_batch_opt = false;
  if (RL_IS_ENABLED) {
    if (ATOMIC_FAA(&concurrent_count, 1) > RL_CONF.get_max_concurrent_query_count()) {
      ret = OB_RESOURCE_OUT;
      LOG_WARN("reach max concurrent limit", K(ret), K(concurrent_count),
          K(RL_CONF.get_max_concurrent_query_count()));
    }
    need_dec = true;
  }
  DEFER(if (need_dec) (void)ATOMIC_FAA(&concurrent_count, -1));
  if (OB_FAIL(ret)) {
    // do-nothing
  } else if (OB_ISNULL(req_) || OB_ISNULL(conn) || OB_ISNULL(cur_trace_id)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null conn ptr", K_(sql), K_(req), K(conn), K(cur_trace_id), K(ret));
  } else if (OB_UNLIKELY(!conn->is_in_authed_phase())) {
    ret = OB_ERR_NO_PRIVILEGE;
    LOG_WARN("receive sql without session", K_(sql), K(ret));
  } else if (OB_ISNULL(conn->tenant_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid tenant", K_(sql), K(conn->tenant_), K(ret));
  } else if (OB_FAIL(get_session(sess))) {
    LOG_WARN("get session fail", K_(sql), K(ret));
  } else if (OB_ISNULL(sess)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL or invalid", K_(sql), K(sess), K(ret));
  } else {
    lib::CompatModeGuard g(sess->get_compatibility_mode() == ORACLE_MODE ?
                             lib::Worker::CompatMode::ORACLE : lib::Worker::CompatMode::MYSQL);
    THIS_WORKER.set_session(sess);
    ObSQLSessionInfo &session = *sess;
    ObSQLSessionInfo::LockGuard lock_guard(session.get_query_lock());
    session.set_current_trace_id(ObCurTraceId::get_trace_id());
    session.init_use_rich_format();
    session.set_proxy_version(conn->is_proxy_ ? conn->proxy_version_ : 0);
    int64_t val = 0;
    const bool check_throttle = !is_root_user(sess->get_user_id());
    if (check_throttle && !sess->is_inner() && sess->get_raw_audit_record().try_cnt_ == 0
               && lib::Worker::WS_OUT_OF_THROTTLE == THIS_THWORKER.check_rate_limiter()) {
      ret = OB_KILLED_BY_THROTTLING;
      LOG_WARN("query is throttled", K(ret), K(sess->get_user_id()));
      need_disconnect = false;
    } else if (OB_SUCC(sess->get_sql_throttle_current_priority(val))) {
      THIS_WORKER.set_sql_throttle_current_priority(check_throttle ? val : -1);
      if (lib::Worker::WS_OUT_OF_THROTTLE == THIS_THWORKER.check_qtime_throttle()) {
        ret = OB_KILLED_BY_THROTTLING;
        LOG_WARN("query is throttled", K(ret));
        need_disconnect = false;
      }
    } else {
      LOG_WARN("get system variable sql_throttle_current_priority fail", K(ret));
      // reset ret for compatibility.
      ret = OB_SUCCESS;
    }
    if (OB_SUCC(ret)) {
      int64_t tenant_version = 0;
      int64_t sys_version = 0;
      session.set_thread_id(GETTID());
      const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
      int64_t packet_len = pkt.get_clen();
      req_->set_trace_point(ObRequest::OB_EASY_REQUEST_MPQUERY_PROCESS);
      const bool enable_flt = session.get_control_info().is_valid();
      if (OB_UNLIKELY(!session.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid session", K_(sql), K(ret));
      } else if (OB_FAIL(process_kill_client_session(session))) {
        LOG_WARN("client session has been killed", K(ret));
      } else if (OB_UNLIKELY(session.is_zombie())) {
        //session has been killed some moment ago
        ret = OB_ERR_SESSION_INTERRUPTED;
        LOG_WARN("session has been killed", K(session.get_session_state()), K_(sql),
                 K(session.get_server_sid()), "proxy_sessid", session.get_proxy_sessid(), K(ret));
      } else if (OB_FAIL(session.check_and_init_retry_info(*cur_trace_id, sql_))) {
        // Note, the logic for retry info and last query trace id should be written inside the query lock, otherwise there will be concurrency issues
        LOG_WARN("fail to check and init retry info", K(ret), K(*cur_trace_id), K_(sql));
      } else if (OB_FAIL(session.get_query_timeout(query_timeout))) {
        LOG_WARN("fail to get query timeout", K_(sql), K(ret));
      } else if (OB_FAIL(gctx_.schema_service_->get_tenant_received_broadcast_version(
                  session.get_effective_tenant_id(), tenant_version))) {
        LOG_WARN("fail get tenant broadcast version", K(ret));
      } else if (OB_FAIL(gctx_.schema_service_->get_tenant_received_broadcast_version(
                  OB_SYS_TENANT_ID, sys_version))) {
        LOG_WARN("fail get tenant broadcast version", K(ret));
      } else if (pkt.exist_trace_info()
                 && OB_FAIL(session.update_sys_variable(SYS_VAR_OB_TRACE_INFO,
                                                        pkt.get_trace_info()))) {
        LOG_WARN("fail to update trace info", K(ret));
      } else if (OB_FAIL(process_extra_info(session, pkt, need_response_error))) {
        LOG_WARN("fail get process extra info", K(ret));
      } else if (OB_UNLIKELY(packet_len > session.get_max_packet_size())) {
        //packet size check with session variable max_allowd_packet or net_buffer_length
        need_disconnect = false;
        ret = OB_ERR_NET_PACKET_TOO_LARGE;
        LOG_WARN("packet too large than allowed for the session", K_(sql), K(ret));
      } else if (OB_FAIL(sql::ObFLTUtils::init_flt_info(pkt.get_extra_info(),
                              session,
                              conn->proxy_cap_flags_.is_full_link_trace_support(),
                              enable_flt))) {
        LOG_WARN("failed to update flt extra info", K(ret));
      } else if (OB_FAIL(session.check_tenant_status())) {
        need_disconnect = false;
        LOG_INFO("unit has been migrated, need deny new request", K(ret), K(MTL_ID()), K(sql_));
      } else if (OB_FAIL(session.gen_configs_in_pc_str())) {
        LOG_WARN("fail to generate configuration strings that can influence execution plan",
                 K(ret));
      } else {
        FLTSpanGuardIfEnable(com_query_process, enable_flt);
        if (enable_flt) {
          char trace_id_buf[OB_MAX_TRACE_ID_BUFFER_SIZE] = {'\0'};
          FLT_SET_TAG(log_trace_id, ObCurTraceId::get_trace_id_str(trace_id_buf, sizeof(trace_id_buf)),
                      receive_ts, get_receive_timestamp(),
                      client_info, session.get_client_info(),
                      module_name, session.get_module_name(),
                      action_name, session.get_action_name(),
                      sess_id, session.get_server_sid());
        }

        THIS_WORKER.set_timeout_ts(get_receive_timestamp() + query_timeout);
        retry_ctrl_.set_tenant_global_schema_version(tenant_version);
        retry_ctrl_.set_sys_global_schema_version(sys_version);
        session.partition_hit().reset();
        session.set_pl_can_retry(true);
        session.set_enable_mysql_compatible_dates(
          session.get_enable_mysql_compatible_dates_from_config());

        bool has_more = false;
        bool force_sync_resp = false;
        need_response_error = false;
        ObParser parser(THIS_WORKER.get_sql_arena_allocator(),
                        session.get_sql_mode(), session.get_charsets4parser());
        //For performance optimization, reduce the array length to lower the construction and destruction overhead of unused elements
        ObSEArray<ObString, 1> queries;
        ObMPParseStat parse_stat;
        if (GCONF.enable_record_trace_id) {
          PreParseResult pre_parse_result;
          if (OB_FAIL(ObParser::pre_parse(sql_, pre_parse_result))) {
            LOG_WARN("fail to pre parse", K(ret));
          } else {
            session.set_app_trace_id(pre_parse_result.trace_id_);
            LOG_DEBUG("app trace id", "app_trace_id", pre_parse_result.trace_id_,
                                      "sessid", session.get_server_sid(), K_(sql));
          }
        }

        if (OB_FAIL(ret)) {
          //do nothing
        } else if (OB_FAIL(parser.split_multiple_stmt(sql_, queries, parse_stat))) {
          // Enter this branch, indicating that push_back failed due to OOM, delegate the outer code to return an error code
          // and after entering this branch, the connection should be terminated
          need_response_error = true;
          if (OB_ERR_PARSE_SQL == ret) {
            need_disconnect = false;
          }
        } else if (OB_UNLIKELY(queries.count() <= 0)) {
          ret = OB_ERR_UNEXPECTED;
          need_response_error = true;//Enter this branch, the connection must be terminated, it is a critical error
          LOG_ERROR("emtpy query count. client would have suspended. never be here!", K_(sql), K(ret));
        } else if (OB_UNLIKELY(1 == session.get_capability().cap_flags_.OB_CLIENT_MULTI_STATEMENTS)) {
          // Handle Multiple Statement
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
            * In the above example, ` and ' do not form closed string tokens, the token parser will report a syntax error
            * In the above example, queries.count() equals 2, which are select 1 and `select 2; select 3;
          */
          bool optimization_done = false;
          const char *p_normal_start = nullptr;
          if (queries.count() > 1
            && OB_FAIL(try_batched_multi_stmt_optimization(session,
                                                          conn,
                                                          queries,
                                                          parse_stat,
                                                          optimization_done,
                                                          async_resp_used,
                                                          need_disconnect,
                                                          false))) {
            LOG_WARN("failed to try multi-stmt-optimization", K(ret));
          } else if (!optimization_done
                     && session.is_enable_batched_multi_statement()
                     && ObSQLUtils::is_enable_explain_batched_multi_statement()
                     && ObParser::is_explain_stmt(queries.at(0), p_normal_start)) {
            ret = OB_SUCC(ret) ? OB_NOT_SUPPORTED : ret;
            need_disconnect = false;
            need_response_error = true;
            LOG_WARN("explain batch statement failed", K(ret));
          } else if (!optimization_done) {
            ARRAY_FOREACH(queries, i) {
              // in multistmt sql, audit_record will record multistmt_start_ts_ when count over 1
              // queries.count()>1 -> batch,(m)sql1,(m)sql2,...    |    queries.count()=1 -> sql1
              if (i > 0) {
                session.get_raw_audit_record().exec_timestamp_.multistmt_start_ts_
                                                              = ObTimeUtility::current_time();
              }
              need_disconnect = true;
              //FIXME qianfu NG_TRACE_EXT(set_disconnect, OB_ID(disconnect), true, OB_ID(pos), "multi stmt begin");
              if (OB_UNLIKELY(parse_stat.parse_fail_
                  && (i == parse_stat.fail_query_idx_)
                  && ObSQLUtils::check_need_disconnect_parser_err(parse_stat.fail_ret_))) {
                // Enter this branch, indicating that parsing of a query in multi_query failed, if not due to a syntax error, then enter this branch
                // If the current query_count is 1, then keep connecting; if greater than 1,
                // then it is necessary to disconnect after sending the error packet to prevent the client from waiting indefinitely for the next response
                // This change is to solve
                ret = parse_stat.fail_ret_;
                need_response_error = true;
                break;
              } else {
                has_more = (queries.count() > i + 1);
                // Originally, it could have been designed so that the last query can always respond asynchronously regardless of queries.count(),
                // However, the current code implementation struggles to handle the response packets of the same request in different threads,
                // Therefore, only one query is allowed in an asynchronous response for a multi-query request here.
                force_sync_resp = queries.count() <= 1? false : true;
                // is_part_of_multi indicates that the current sql is one of the statements in a multi stmt,
                // The original value defaults to true, which affects the secondary routing of a single SQL statement. Now it is changed to use queries.count() for judgment.
                bool is_part_of_multi = queries.count() > 1 ? true : false;
                ret = process_single_stmt(ObMultiStmtItem(is_part_of_multi, i, queries.at(i)),
                                          conn,
                                          session,
                                          has_more,
                                          force_sync_resp,
                                          async_resp_used,
                                          need_disconnect);
              }
            }
          }
          // The total number of statements sent over the multiple query protocol
          EVENT_INC(SQL_MULTI_QUERY_COUNT);
          // Sent using the multiple query protocol, but actually contains only one SQL statement count
          if (queries.count() <= 1) {
            EVENT_INC(SQL_MULTI_ONE_QUERY_COUNT);
          }
        } else { // OB_CLIENT_MULTI_STATEMENTS not enabled
          if (OB_UNLIKELY(queries.count() != 1)) {
            ret = OB_ERR_PARSER_SYNTAX;
            need_disconnect = false;
            need_response_error = true;
            LOG_WARN("unexpected error. multi stmts sql while OB_CLIENT_MULTI_STATEMENTS not enabled.", K(ret), K(sql_));
          } else {
            EVENT_INC(SQL_SINGLE_QUERY_COUNT);
            // Handle ordinary Single Statement
            ret = process_single_stmt(ObMultiStmtItem(false, 0, sql_),
                                      conn,
                                      session,
                                      has_more,
                                      force_sync_resp,
                                      async_resp_used,
                                      need_disconnect);
          }
        }
        if (OB_FAIL(ret) && enable_flt) {
          FLT_SET_TAG(err_code, ret);
        }
      }
    }
    // THIS_WORKER.need_retry() means whether to put it back in the queue for retry, including the case of large queries being put back in the queue.
    session.check_and_reset_retry_info(*cur_trace_id, THIS_WORKER.need_retry());
    session.set_last_trace_id(ObCurTraceId::get_trace_id());
    IGNORE_RETURN record_flt_trace(session);
    // clear thread-local variables used for queue waiting
    // to prevent async callbacks from finishing before 
    // request_finish_callback, which may free the request.
    // this operation should be protected by the session lock.
    if (async_resp_used) {
      request_finish_callback();
    }
  }

  if (is_conn_valid()) {
    set_request_expect_group_id(sess);
  }
  if (OB_FAIL(ret) && need_response_error && is_conn_valid()) {
    send_error_packet(ret, NULL);
  }
  if (OB_FAIL(ret) && OB_UNLIKELY(need_disconnect) && is_conn_valid()) {
    force_disconnect();
    LOG_WARN("disconnect connection", KR(ret));
  }
  // If the response has already been sent asynchronously, this logic will be executed in cb, so skip flush_buffer() here
  if (!THIS_WORKER.need_retry()) {
    if (async_resp_used) {
      async_resp_used_ = true;
      packet_sender_.disable_response();
    } else if (OB_UNLIKELY(!is_conn_valid())) {
      tmp_ret = OB_CONNECT_ERROR;
      LOG_WARN("connection in error, maybe has disconnected", K(tmp_ret));
    } else if (OB_UNLIKELY(OB_SUCCESS != (tmp_ret = flush_buffer(true)))) {
      LOG_WARN("failed to flush_buffer", K(tmp_ret));
    }
  } else {
    need_retry_ = true;
  }

  // bugfix: 
  // Must always set the pointer in THIS_WORKER to null
  THIS_WORKER.set_session(NULL); // clear session

  if (sess != NULL) {
    revert_session(sess); //current ignore revert session ret
  }

  return (OB_SUCCESS != ret) ? ret : tmp_ret;
}

/*
 * Try to evaluate multiple update queries as a single query to optimize rpc cost
 * for details, please ref to 
 */
int ObMPQuery::try_batched_multi_stmt_optimization(sql::ObSQLSessionInfo &session,
                                                   ObSMConnection *conn,
                                                   common::ObIArray<ObString> &queries,
                                                   const ObMPParseStat &parse_stat,
                                                   bool &optimization_done,
                                                   bool &async_resp_used,
                                                   bool &need_disconnect,
                                                   bool is_ins_multi_val_opt)
{
  int ret = OB_SUCCESS;
  bool has_more = false;
  bool force_sync_resp = true;
  bool enable_batch_opt = session.is_enable_batched_multi_statement();
  bool use_plan_cache = session.get_local_ob_enable_plan_cache();
  optimization_done = false;
  if (queries.count() <= 1 || parse_stat.parse_fail_) {
    /*do nothing*/
  } else if (!enable_batch_opt) {
    // batch switch is not turned on
  } else if (!use_plan_cache) {
    // If the plan_cache switch is not turned on, optimization is not supported
  } else if (OB_FAIL(process_single_stmt(ObMultiStmtItem(false, 0, sql_, &queries, is_ins_multi_val_opt),
                                         conn,
                                         session,
                                         has_more,
                                         force_sync_resp,
                                         async_resp_used,
                                         need_disconnect))) {
    int tmp_ret = ret;
    if (THIS_WORKER.need_retry()) {
      // fail optimize, is a large query, just go back to large query queue and retry
    } else {
      ret = OB_SUCCESS;
    }
    LOG_WARN("failed to process batch stmt, cover the error code and reset retry flag",
        K(tmp_ret), K(ret), K(THIS_WORKER.need_retry()));
  } else {
    optimization_done = true;
  }

  LOG_TRACE("after to try batched multi-stmt optimization", K(optimization_done),
      K(queries), K(enable_batch_opt), K(ret), K(THIS_WORKER.need_retry()), K(retry_ctrl_.need_retry()), K(retry_ctrl_.get_retry_type()));
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_BEGIN_COMMIT_OPT_DISABLE)
int ObMPQuery::process_single_stmt(const ObMultiStmtItem &multi_stmt_item,
                                   ObSMConnection *conn,
                                   ObSQLSessionInfo &session,
                                   bool has_more_result,
                                   bool force_sync_resp,
                                   bool &async_resp_used,
                                   bool &need_disconnect)
{
  int ret = OB_SUCCESS;
  FLTSpanGuard(mpquery_single_stmt);
  ObReqTimeGuard req_timeinfo_guard;
  ctx_.bl_key_.reset();
  bool need_response_error = true;
  session.get_raw_audit_record().request_memory_used_ = 0;
  observer::ObProcessMallocCallback pmcb(0,
        session.get_raw_audit_record().request_memory_used_);
  lib::ObMallocCallbackGuard guard(pmcb);
  // After executing setup_wb, all WARNINGS will be written to the WARNING BUFFER of the current session
  setup_wb(session);
  // When a new statement starts, set this value to 0, because curr_trans_last_stmt_end_time is used for
  // Implement the timeout function for excessively long execution intervals of statements within a transaction.
  session.set_curr_trans_last_stmt_end_time(0);
  //============================ Pay attention to the lifecycle of these variables ================================
  if (OB_FAIL(init_process_var(ctx_, multi_stmt_item, session))) {
    LOG_WARN("init process var failed.", K(ret), K(multi_stmt_item));
  } else {
    //set session log_level.Must use ObThreadLogLevelUtils::clear() in pair
    ObThreadLogLevelUtils::init(session.get_log_id_level_map());
    bool is_trans_ctrl_cmd = false;
    bool do_trans_ctrl_opt = false;
    stmt::StmtType stmt_type = stmt::T_NONE;
    if (!ERRSIM_BEGIN_COMMIT_OPT_DISABLE && !multi_stmt_item.is_part_of_multi_stmt()) {
      check_is_trans_ctrl_cmd(multi_stmt_item.get_sql(), is_trans_ctrl_cmd, stmt_type);
      if (is_trans_ctrl_cmd && !session.associated_xa()) {
        do_trans_ctrl_opt = true;
      }
    }

    // obproxy may use 'SET @@last_schema_version = xxxx' to set newest schema,
    // observer will force refresh schema if local_schema_version < last_schema_version;
    if (!do_trans_ctrl_opt && OB_FAIL(check_and_refresh_schema(session.get_login_tenant_id(),
                                         session.get_effective_tenant_id(),
                                         &session))) {
      LOG_WARN("failed to check_and_refresh_schema", K(ret));
    } else if (!do_trans_ctrl_opt && OB_FAIL(session.update_timezone_info())) {
      LOG_WARN("fail to update time zone info", K(ret));
    } else {
      need_response_error = false;
      //Each execution of different SQL requires an update
      ctx_.self_add_plan_ = false;
      retry_ctrl_.reset_retry_times();//each statement records retry times separately
      oceanbase::lib::Thread::WaitGuard guard(oceanbase::lib::Thread::WAIT_FOR_LOCAL_RETRY);
      do {
        ret = OB_SUCCESS; //When a local retry occurs, the error code needs to be reset, otherwise the retry cannot proceed
        need_disconnect = true;
        // if query need mock schema, will remember table_id in mocked_tables (local thread var)
        // when resolve, and then will try mock a table schema in sqlschemaguard.
        // ObSQLMockSchemaGuard here is only used for reset mocked_tables.
        ObSQLMockSchemaGuard mock_schema_guard;
        // do the real work
        //create a new temporary memory context for executing sql can
        //avoid the problem the memory cannot be released in time due to too many sql items
        //but it will drop the sysbench performance about 1~4%
        //so we execute the first sql with the default memory context and
        //execute the rest sqls with a temporary memory context to avoid memory dynamic leaks
        retry_ctrl_.clear_state_before_each_retry(session.get_retry_info_for_update());
        bool first_exec_sql = session.get_is_in_retry() ? false :
            (multi_stmt_item.is_part_of_multi_stmt() ? multi_stmt_item.get_seq_num() <= 1 : true);
        if (do_trans_ctrl_opt) {
          ret = do_process_trans_ctrl(session,
                                      has_more_result,
                                      force_sync_resp,
                                      async_resp_used,
                                      need_disconnect,
                                      stmt_type);
          ctx_.clear();
        } else if (OB_LIKELY(first_exec_sql)) {
          ret = do_process(session,
                           has_more_result,
                           force_sync_resp,
                           async_resp_used,
                           need_disconnect);
          ctx_.clear();
        } else {
          ret = process_with_tmp_context(session,
                                         has_more_result,
                                         force_sync_resp,
                                         async_resp_used,
                                         need_disconnect);
        }
        //set session retry state
        session.set_session_in_retry(retry_ctrl_.need_retry());
      } while (RETRY_TYPE_LOCAL == retry_ctrl_.get_retry_type());
      //@notice: after the async packet is responsed,
      //the easy_buf_ hold by the sql string may have been released.
      //from here on, we can no longer access multi_stmt_item.sql_,
      //otherwise there is a risk of coredump
      //@TODO: need to determine a mechanism to ensure the safety of memory access here
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
  }
  //For the handling of tracelog, it does not affect the normal logic, and the error code does not need to be assigned to ret
  int tmp_ret = OB_SUCCESS;
  //Clear WARNING BUFFER
  tmp_ret = do_after_process(session, async_resp_used);
  // Set the end time of the previous statement, since here it is only used to implement the execution timeout between statements within a transaction,
  // Therefore, first, it is necessary to determine whether a transaction execution process is in progress. Then for the asynchronous response at the time of transaction submission,
  // It is also not necessary to set the end time here, because this is already equivalent to the last statement of the transaction.
  // Finally, need to determine the ret error code, only successfully executed sql records the end time
  if (session.get_in_transaction() && !async_resp_used && OB_SUCC(ret)) {
    session.set_curr_trans_last_stmt_end_time(ObClockGenerator::getClock());
  }
  // the need_response_error variable ensures that it only occurs in
  // do { do_process } while(retry) will only occur if an error happens before
  // Walk to the send_error_packet logic
  // So there is no need to consider whether the current mode is sync or async
  if (!OB_SUCC(ret) && need_response_error && is_conn_valid() &&
        !ctx_.multi_stmt_item_.is_batched_multi_stmt()) {
    send_error_packet(ret, NULL);
  }
  ctx_.bl_key_.reset();
  ctx_.reset();
  return ret;
}

OB_NOINLINE int ObMPQuery::process_with_tmp_context(ObSQLSessionInfo &session,
                                                    bool has_more_result,
                                                    bool force_sync_resp,
                                                    bool &async_resp_used,
                                                    bool &need_disconnect)
{
  int ret = OB_SUCCESS;
  //create a temporary memory context to process retry or the rest sql of multi-query,
  //avoid memory dynamic leaks caused by query retry or too many multi-query items
  lib::ContextParam param;
  param.set_mem_attr(MTL_ID(),
      ObModIds::OB_SQL_EXECUTOR, ObCtxIds::DEFAULT_CTX_ID)
    .set_properties(lib::USE_TL_PAGE_OPTIONAL)
    .set_page_size(OB_MALLOC_REQ_NORMAL_BLOCK_SIZE)
    .set_ablock_size(lib::INTACT_MIDDLE_AOBJECT_SIZE);
  CREATE_WITH_TEMP_CONTEXT(param) {
    ret = do_process(session,
                     has_more_result,
                     force_sync_resp,
                     async_resp_used,
                     need_disconnect);
    ctx_.first_plan_hash_ = 0;
    ctx_.first_outline_data_.reset();
    ctx_.first_equal_param_cons_cnt_ = 0;
    ctx_.first_const_param_cons_cnt_ = 0;
    ctx_.first_expr_cons_cnt_ = 0;
    ctx_.clear();
  }
  return ret;
}

OB_INLINE int ObMPQuery::get_tenant_schema_info_(const uint64_t tenant_id,
                                                ObTenantCachedSchemaGuardInfo *cache_info,
                                                ObSchemaGetterGuard *&schema_guard,
                                                int64_t &tenant_version,
                                                int64_t &sys_version)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard &cached_guard = cache_info->get_schema_guard();
  bool need_refresh = false;

  if (!cached_guard.is_inited()) {
    // First get schema guard
    need_refresh = true;
  } else if (tenant_id != cached_guard.get_tenant_id()) {
    // change tenant
    need_refresh = true;
  } else {
    int64_t tmp_tenant_version = 0;
    int64_t tmp_sys_version = 0;
    if (OB_FAIL(gctx_.schema_service_->get_tenant_refreshed_schema_version(tenant_id, tmp_tenant_version))) {
      LOG_WARN("get tenant refreshed schema version error", K(ret), K(tenant_id));
    } else if (OB_FAIL(cached_guard.get_schema_version(tenant_id, tenant_version))) {
      LOG_WARN("fail get schema version", K(ret), K(tenant_id));
    } else if (tmp_tenant_version != tenant_version) {
      //Need to obtain schema guard
      need_refresh = true;
    } else if (OB_FAIL(gctx_.schema_service_->get_tenant_refreshed_schema_version(OB_SYS_TENANT_ID, tmp_sys_version))) {
      LOG_WARN("get sys tenant refreshed schema version error", K(ret), "sys_tenant_id", OB_SYS_TENANT_ID);
    } else if (OB_FAIL(cached_guard.get_schema_version(OB_SYS_TENANT_ID, sys_version))) {
      LOG_WARN("fail get sys schema version", K(ret));
    } else if (tmp_sys_version != sys_version) {
      //Need to obtain schema guard
      need_refresh = true;
    } else {
      // do nothing
    }
  }
  if (OB_SUCC(ret)) {
    if (!need_refresh) {
      //Get the latest schema guard cached on the session
      schema_guard = &(cache_info->get_schema_guard());
    } else if (OB_FAIL(cache_info->refresh_tenant_schema_guard(tenant_id))) {
      LOG_WARN("refresh tenant schema guard failed", K(ret), K(tenant_id));
    } else {
      //Get the latest schema guard cached on the session
      schema_guard = &(cache_info->get_schema_guard());
      if (OB_FAIL(schema_guard->get_schema_version(tenant_id, tenant_version))) {
        LOG_WARN("fail get schema version", K(ret), K(tenant_id));
      } else if (OB_FAIL(schema_guard->get_schema_version(OB_SYS_TENANT_ID, sys_version))) {
        LOG_WARN("fail get sys schema version", K(ret));
      } else {
        // do nothing
      }
    }
  }

  return ret;
}

void ObMPQuery::check_is_trans_ctrl_cmd(const ObString &sql,
                                        bool &is_trans_ctrl_cmd,
                                        stmt::StmtType &stmt_type)
{
  is_trans_ctrl_cmd = false;
  const uint32_t cmd_len = sql.length();
  if (5 <= cmd_len && cmd_len <=8) {
    if (cmd_len == 5) {
      if (0 == sql.case_compare("begin")) {
        is_trans_ctrl_cmd = true;
        stmt_type = stmt::T_START_TRANS;
      }
    } else if (cmd_len == 6) {
      if (0 == sql.case_compare("commit")) {
        is_trans_ctrl_cmd = true;
        stmt_type = stmt::T_END_TRANS;
      }
    } else if (cmd_len == 8) {
      if (0 == sql.case_compare("rollback")) {
        is_trans_ctrl_cmd = true;
        stmt_type = stmt::T_END_TRANS;
      }
    }
  }
  LOG_DEBUG("check is trans ctrl cmd ", K(sql), K(is_trans_ctrl_cmd), K(stmt_type));
}

OB_INLINE int ObMPQuery::do_process_trans_ctrl(ObSQLSessionInfo &session,
                                               bool has_more_result,
                                               bool force_sync_resp,
                                               bool &async_resp_used,
                                               bool &need_disconnect,
                                               stmt::StmtType stmt_type)
{
  int ret = OB_SUCCESS;
  ObAuditRecordData &audit_record = session.get_raw_audit_record();
  ObExecutingSqlStatRecord sqlstat_record;
  audit_record.try_cnt_++;
  bool is_diagnostics_stmt = false;
  bool need_response_error = true;
  const ObString &sql = ctx_.multi_stmt_item_.get_sql();
  const bool enable_perf_event = lib::is_diagnose_info_enabled();
  const bool enable_sql_audit =
    GCONF.enable_sql_audit && session.get_local_ob_enable_sql_audit();
  const bool enable_sqlstat = session.is_sqlstat_enabled();
  bool is_rollback = false;
  bool is_commit = false;
  if (0 == sql.case_compare("commit")) {
    is_commit = true;
  } else if (0 == sql.case_compare("rollback")) {
    is_rollback = true;
  }
  single_process_timestamp_ = ObTimeUtility::current_time();
  /* !!!
   * Note that req_timeinfo_guard must be placed before result
   * !!!
   */
  ObReqTimeGuard req_timeinfo_guard;
  ObTenantCachedSchemaGuardInfo &cached_schema_info = session.get_cached_schema_guard_info();
  int64_t tenant_version = 0;
  int64_t sys_version = 0;
  SQL_INFO_GUARD(sql, session.get_cur_sql_id());
  need_disconnect = false;

  if (OB_FAIL(update_transmission_checksum_flag(session))) {
    LOG_WARN("update transmisson checksum flag failed", K(ret));
  } else {
    session.reset_plsql_exec_time();
    session.set_stmt_type(stmt_type);
  }

  ObWaitEventStat total_wait_desc;
  if (OB_SUCC(ret)) {
    ObMaxWaitGuard max_wait_guard(enable_perf_event ? &audit_record.exec_record_.max_wait_event_ : nullptr);
    ObTotalWaitGuard total_wait_guard(enable_perf_event ? &total_wait_desc : nullptr);
    if (enable_perf_event) {
      audit_record.exec_record_.record_start();
    }
    if (enable_sqlstat) {
      sqlstat_record.record_sqlstat_start_value();
      sqlstat_record.set_is_in_retry(false);
      session.sql_sess_record_sql_stat_start_value(sqlstat_record);
    }
    ctx_.enable_sql_resource_manage_ = true;
    if (OB_FAIL(set_session_active(sql, session, single_process_timestamp_))) {
      LOG_WARN("fail to set session active", K(ret));
    } else {
      // generate sql_id with sql cmd
      (void)ObSQLUtils::md5(sql, ctx_.sql_id_, (int32_t)sizeof(ctx_.sql_id_));
      session.set_cur_sql_id(ctx_.sql_id_);
      //Monitoring item statistics start
      exec_start_timestamp_ = ObTimeUtility::current_time();
      need_response_error = false;
      is_diagnostics_stmt = false;
      ctx_.is_show_trace_stmt_ = false;

      // exec cmd
      FLTSpanGuard(sql_execute);
      if (OB_FAIL(process_trans_ctrl_cmd(session,
                                         need_disconnect,
                                         async_resp_used,
                                         is_rollback,
                                         force_sync_resp,
                                         stmt_type))) {
        need_response_error = true;
        LOG_WARN("fail to execute trans ctrl cmd", KR(ret), K(sql));
      }
      // If there is no asynchronous submission, and the execution is successful, then send an ok packet to the client
      if (!async_resp_used && OB_SUCC(ret)) {
        ObOKPParam ok_param;
        ok_param.affected_rows_ = 0;
        ok_param.is_partition_hit_ = session.partition_hit().get_bool();
        ok_param.has_more_result_ = has_more_result;
        if (OB_FAIL(send_ok_packet(session, ok_param))) {
          LOG_WARN("fail to send ok packt", KR(ret), K(ok_param));
        }
      }
    }
    // Note: Do not use the sql_ member variable after calling the response_result interface, because the memory that sql_ points to comes from ObReqPacket
    // while in the transaction asynchronous commit response client process, the response client process is located in the ApplyService thread, and there is no timing guarantee with the SQL Worker thread
    //After starting the asynchronous response client process with response_result, it is possible that the response packet will be returned before executing the following logic. If the response packet is completed first, it will lead to
    //ObReqPacket memory is released, thus leading to sql_ pointing to an invalid memory address, accessing sql_ in the following logic will cause a crash
    // And the process of synchronously responding to the client does not have similar issues, because the flush action for the last packet in the synchronous response client is located at the very bottom of this interface,
    //But the asynchronous response process does not have this guarantee
    int tmp_ret = OB_SUCCESS;
    tmp_ret = OB_E(EventTable::EN_PRINT_QUERY_SQL) OB_SUCCESS;
    if (OB_SUCCESS != tmp_ret) {
      LOG_INFO("query info:",
               "sql", session.get_current_query_string(),
               "sess_id", session.get_server_sid(),
               "database_id", session.get_database_id(),
               "database_name", session.get_database_name(),
               "trans_id", audit_record.trans_id_);
    }
    //Monitoring item statistics end
    exec_end_timestamp_ = ObTimeUtility::current_time();

    // some statistics must be recorded for plan stat, even though sql audit disabled
    bool first_record = (1 == audit_record.try_cnt_);
    ObExecStatUtils::record_exec_timestamp(*this, first_record, audit_record.exec_timestamp_);
    audit_record.exec_timestamp_.update_stage_time();

    // store the warning message from the most recent statement in the current session
    if ((OB_SUCC(ret) && is_diagnostics_stmt) || async_resp_used) {
      // If diagnostic stmt execute successfully, it dosen't clear the warning message.
      // Or if it response to client asynchronously, it doesn't clear the warning message here,
      // but will do it in the callback thread.
      session.update_show_warnings_buf();
    } else {
      session.set_show_warnings_buf(ret); // TODO: Move this to a better place, reduce some wb copying
    }

    if (OB_FAIL(ret) && !async_resp_used && need_response_error && is_conn_valid() &&
          !ctx_.multi_stmt_item_.is_batched_multi_stmt()) {
      LOG_WARN("query failed", K(ret), K(session), K(sql));
      // This request has errored, it hasn't been processed yet. If it hasn't already been handed over to async EndTrans for cleanup,
      // then it is necessary to reply with an error_packet as a footer.
      // Otherwise, no one will help send the error packet to the client, which may cause the client to hang waiting for a response.
      bool is_partition_hit = session.get_err_final_partition_hit(ret);
      int err = send_error_packet(ret, NULL, is_partition_hit, (void *)ctx_.get_reroute_info());
      if (OB_SUCCESS != err) {  // send error packet
        LOG_WARN("send error packet failed", K(ret), K(err));
      }
    }
  }

  if (enable_perf_event) {
    audit_record.exec_record_.record_end();
    record_stat(stmt_type, exec_end_timestamp_, session, ret, is_commit, is_rollback);
    audit_record.stmt_type_ = stmt_type;
    audit_record.exec_record_.wait_time_end_ = total_wait_desc.time_waited_;
    audit_record.exec_record_.wait_count_end_ = total_wait_desc.total_waits_;
    audit_record.update_event_stage_state();
  }
  if (enable_sqlstat) {
    sqlstat_record.record_sqlstat_end_value();
    sqlstat_record.set_rows_processed(0);
    sqlstat_record.set_partition_cnt(0);
    sqlstat_record.set_is_route_miss(session.partition_hit().get_bool()? 0 : 1);
    sqlstat_record.set_is_plan_cache_hit(ctx_.plan_cache_hit_);
  }

  audit_record.status_ = (0 == ret || OB_ITER_END == ret)
      ? REQUEST_SUCC : (ret);
  if (enable_sql_audit) {
    audit_record.seq_ = 0;  //don't use now
    audit_record.execution_id_ = session.get_current_execution_id();
    audit_record.client_addr_ = session.get_peer_addr();
    audit_record.user_client_addr_ = session.get_user_client_addr();
    audit_record.user_group_ = THIS_WORKER.get_group_id();
    MEMCPY(audit_record.sql_id_, ctx_.sql_id_, (int32_t)sizeof(audit_record.sql_id_));
    MEMCPY(audit_record.format_sql_id_, ctx_.format_sql_id_, (int32_t)sizeof(audit_record.format_sql_id_));
    audit_record.format_sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';

    if (OB_FAIL(ret) && audit_record.trans_id_ == 0) {
      // normally trans_id is set in the `start-stmt` phase,
      // if `start-stmt` hasn't run, set trans_id from session if an active txn exist
      if (session.is_in_transaction()) {
        audit_record.trans_id_ = session.get_tx_id();
      }
    }
    // for begin/commit/rollback, the following values are 0
    audit_record.affected_rows_ = 0;
    audit_record.return_rows_ = 0;
    audit_record.partition_cnt_ = 0;
    audit_record.expected_worker_cnt_ = 0;
    audit_record.used_worker_cnt_ = 0;

    audit_record.is_executor_rpc_ = false;
    audit_record.is_inner_sql_ = false;
    audit_record.is_hit_plan_cache_ = false;
    audit_record.is_multi_stmt_ = session.get_capability().cap_flags_.OB_CLIENT_MULTI_STATEMENTS;
    audit_record.is_batched_multi_stmt_ = ctx_.multi_stmt_item_.is_batched_multi_stmt();
    if (audit_record.params_value_ == nullptr) {
      audit_record.params_value_ = params_value_;
      audit_record.params_value_len_ = params_value_len_;
    }
    audit_record.is_perf_event_closed_ = !lib::is_diagnose_info_enabled();
    audit_record.plsql_exec_time_ = session.get_plsql_exec_time();
  }
  // reset thread waring buffer in sync mode
  if (!async_resp_used) {
    clear_wb_content(session);
  }

  need_disconnect = (need_disconnect && !is_query_killed_return(ret)); // Clearly it is a kill query, the connection should not be disconnected
  if (need_disconnect) {
    LOG_WARN("need disconnect", K(ret), K(need_disconnect));
  }
  bool is_need_retry = false;
  (void)ObSQLUtils::handle_audit_record(is_need_retry, EXECUTE_LOCAL, session, ctx_.is_sensitive_);

  return ret;
}

int ObMPQuery::process_trans_ctrl_cmd(ObSQLSessionInfo &session,
                                      bool &need_disconnect,
                                      bool &async_resp_used,
                                      const bool is_rollback,
                                      const bool force_sync_resp,
                                      stmt::StmtType stmt_type)
{
  int ret = OB_SUCCESS;
  if (stmt_type == stmt::T_START_TRANS) {
    bool read_only = session.get_tx_read_only();
    transaction::ObTxParam tx_param;
    TransState trans_state;
    // stmt is T_START_TRANS and not xa cmd, try to end trans before start trans
    if (OB_FAIL(ObSqlTransControl::end_trans_before_cmd_execute(session,
                                                                need_disconnect,
                                                                trans_state,
                                                                stmt_type))) {
      LOG_WARN("end trans before start fail", KR(ret), K(need_disconnect), K(read_only));
    }
    if (OB_SUCC(ret) && OB_FAIL(ObSqlTransControl::explicit_start_trans(&session,
                                                                        tx_param,
                                                                        need_disconnect,
                                                                        read_only))) {
      LOG_WARN("explicit start trans fail", KR(ret), K(need_disconnect), K(read_only));
    }
  } else if (stmt_type == stmt::T_END_TRANS) {
    bool is_async_end_trans = false;
    bool need_end_trans_callback = false;
    ObEndTransAsyncCallback *callback = nullptr;
    TransState trans_state;
    ObEndTransCbPacketParam pkt_param;

    if (session.get_has_temp_table_flag() || session.has_tx_level_temp_table()) {
      // temporary table will be committed synchronously, and then drop_temp_tables will be called to delete the data.
      need_end_trans_callback = false;
    } else {
      need_end_trans_callback = true;
    }

    bool need_trans_cb  = need_end_trans_callback && (!force_sync_resp);
    if (need_trans_cb) {
      is_async_end_trans = true;
      ObSqlEndTransCb &sql_end_cb = session.get_mysql_end_trans_cb();
      ObCurTraceId::TraceId *cur_trace_id = NULL;
      if (OB_ISNULL(cur_trace_id = ObCurTraceId::get_trace_id())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("current trace id is NULL", K(ret));
      } else if (OB_FAIL(sql_end_cb.init(packet_sender_, &session))) {
        LOG_WARN("failed to init sql end callback", K(ret));
      } else if (OB_FAIL(sql_end_cb.set_packet_param(pkt_param.fill("\0", // message
                                                                    0,  // affected_rows
                                                                    0,  // last_insert_id_to_client
                                                                    session.partition_hit().get_bool(),
                                                                    *cur_trace_id)))) {
        LOG_WARN("fail to set packet param", K(ret));
      } else {
        callback = &session.get_end_trans_cb();
      }
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(ObSqlTransControl::end_trans(&session,
                                                    need_disconnect,
                                                    trans_state,
                                                    is_rollback,
                                                    true, // is_explicit
                                                    callback))) {
      LOG_WARN("explicit end trans fail", K(ret));
    }
    if (trans_state.is_end_trans_executed() && trans_state.is_end_trans_success()) {
      async_resp_used = true;
    }
  }
  return ret;
}

OB_INLINE int ObMPQuery::do_process(ObSQLSessionInfo &session,
                                    bool has_more_result,
                                    bool force_sync_resp,
                                    bool &async_resp_used,
                                    bool &need_disconnect)
{
  int ret = OB_SUCCESS;
  ObAuditRecordData &audit_record = session.get_raw_audit_record();
  ObExecutingSqlStatRecord sqlstat_record;
  audit_record.try_cnt_++;
  bool is_diagnostics_stmt = false;
  bool need_response_error = true;
  const ObString &sql = ctx_.multi_stmt_item_.get_sql();
  const bool enable_perf_event = lib::is_diagnose_info_enabled();
  const bool enable_sql_audit =
    GCONF.enable_sql_audit && session.get_local_ob_enable_sql_audit();
  const bool enable_sqlstat = session.is_sqlstat_enabled();
  single_process_timestamp_ = ObTimeUtility::current_time();
  /* !!!
   * Note that req_timeinfo_guard must be placed before result
   * !!!
   */
  ObReqTimeGuard req_timeinfo_guard;
  ObPhysicalPlan *plan = nullptr;
  ObSchemaGetterGuard* schema_guard = nullptr;
  ObTenantCachedSchemaGuardInfo &cached_schema_info = session.get_cached_schema_guard_info();
  int64_t tenant_version = 0;
  int64_t sys_version = 0;
  SQL_INFO_GUARD(sql, session.get_cur_sql_id());
  ObIAllocator &allocator = CURRENT_CONTEXT->get_arena_allocator();
  SMART_VAR(ObMySQLResultSet, result, session, allocator) {
    if (OB_FAIL(get_tenant_schema_info_(session.get_effective_tenant_id(),
                                        &cached_schema_info,
                                        schema_guard,
                                        tenant_version,
                                        sys_version))) {
      LOG_WARN("get tenant schema info error", K(ret), K(session));
    } else if (OB_FAIL(session.update_query_sensitive_system_variable(*schema_guard))) {
      LOG_WARN("update query sensitive system vairable in session failed", K(ret));
    } else if (OB_FAIL(update_transmission_checksum_flag(session))) {
      LOG_WARN("update transmisson checksum flag failed", K(ret));
    } else if (OB_ISNULL(gctx_.sql_engine_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid sql engine", K(ret), K(gctx_));
    } else {
      session.set_current_execution_id(GCTX.sql_engine_->get_execution_id());
      session.reset_plsql_exec_time();
      session.reset_plsql_compile_time();
      session.set_stmt_type(stmt::T_NONE);
      result.get_exec_context().set_need_disconnect(true);
      ctx_.schema_guard_ = schema_guard;
      retry_ctrl_.set_tenant_local_schema_version(tenant_version);
      retry_ctrl_.set_sys_local_schema_version(sys_version);
    }

    ObWaitEventStat total_wait_desc;
    if (OB_SUCC(ret)) {
      ObMaxWaitGuard max_wait_guard(enable_perf_event ? &audit_record.exec_record_.max_wait_event_ : nullptr);
      ObTotalWaitGuard total_wait_guard(enable_perf_event ? &total_wait_desc : nullptr);
      if (enable_perf_event) {
        audit_record.exec_record_.record_start();
      }
      if (enable_sqlstat) {
        sqlstat_record.record_sqlstat_start_value();
        sqlstat_record.set_is_in_retry(session.get_is_in_retry());
        session.sql_sess_record_sql_stat_start_value(sqlstat_record);
      }
      result.set_has_more_result(has_more_result);
      ObTaskExecutorCtx &task_ctx = result.get_exec_context().get_task_exec_ctx();
      task_ctx.schema_service_ = gctx_.schema_service_;
      task_ctx.set_query_tenant_begin_schema_version(retry_ctrl_.get_tenant_local_schema_version());
      task_ctx.set_query_sys_begin_schema_version(retry_ctrl_.get_sys_local_schema_version());
      task_ctx.set_min_cluster_version(GET_MIN_CLUSTER_VERSION());
      ctx_.retry_times_ = retry_ctrl_.get_retry_times();
      ctx_.enable_sql_resource_manage_ = true;
      //storage::ObPartitionService* ps = static_cast<storage::ObPartitionService *> (GCTX.par_ser_);
      //bool is_read_only = false;
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (OB_ISNULL(ctx_.schema_guard_)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("newest schema is NULL", K(ret));
      } else if (OB_FAIL(set_session_active(sql, session, single_process_timestamp_))) {
        LOG_WARN("fail to set session active", K(ret));
      } else if (OB_FAIL(gctx_.sql_engine_->stmt_query(sql, ctx_, result))) {
        exec_start_timestamp_ = ObTimeUtility::current_time();
        if (!THIS_WORKER.need_retry()) {
          int cli_ret = OB_SUCCESS;
          retry_ctrl_.test_and_save_retry_state(gctx_, ctx_, result, ret, cli_ret);
          if (OB_ERR_PROXY_REROUTE == ret) {
            LOG_DEBUG("run stmt_query failed, check if need retry",
                      K(ret), K(cli_ret), K(retry_ctrl_.need_retry()), K(sql));
          } else {
            LOG_WARN("run stmt_query failed, check if need retry",
                     K(ret), K(cli_ret), K(retry_ctrl_.need_retry()),
                     "sql", ctx_.is_sensitive_ ? ObString(OB_MASKED_STR) : sql);
          }
          ret = cli_ret;
          if (OB_ERR_PROXY_REROUTE == ret) {
            // This error code is set by the compiler at the compilation stage, the async_resp_used flag must be false
            // So at this point, we can sync the response packet and set need_response_error
            // Return an error packet to the client indicating a secondary routing is required
            need_response_error = true;
          } else if (ctx_.multi_stmt_item_.is_batched_multi_stmt()) {
            // batch execute with error,should not response error packet
            need_response_error = false;
          } else if (OB_BATCHED_MULTI_STMT_ROLLBACK == ret) {
            need_response_error = false;
          }
        } else {
          retry_ctrl_.set_packet_retry(ret);
          session.get_retry_info_for_update().set_last_query_retry_err(ret);
          session.get_retry_info_for_update().inc_retry_cnt();
        }
      } else {
        //Monitoring item statistics start
        exec_start_timestamp_ = ObTimeUtility::current_time();
        result.get_exec_context().set_plan_start_time(exec_start_timestamp_);
        // All errors within this branch will be handled properly inside response_result
        // No need to handle the error response packet additionally
        need_response_error = false;
        is_diagnostics_stmt = ObStmt::is_diagnostic_stmt(result.get_literal_stmt_type());
        ctx_.is_show_trace_stmt_ = ObStmt::is_show_trace_stmt(result.get_literal_stmt_type());
        plan = result.get_physical_plan();

        if (get_is_com_filed_list()) {
          result.set_is_com_filed_list();
          result.set_wildcard_string(wild_str_);
        }

        //response_result
        if (OB_FAIL(ret)) {
        //TODO shengle, confirm whether 4.0 is required
        //} else if (OB_FAIL(fill_feedback_session_info(*result, session))) {
          //need_response_error = true;
          //LOG_WARN("failed to fill session info", K(ret));
        } else if (OB_FAIL(response_result(result,
                                           force_sync_resp,
                                           async_resp_used))) {
          ObPhysicalPlanCtx *plan_ctx = result.get_exec_context().get_physical_plan_ctx();
          if (OB_ISNULL(plan_ctx)) {
            // ignore ret
            LOG_ERROR("execute query fail, and plan_ctx is NULL", K(ret));
          } else {
            if (OB_TRANSACTION_SET_VIOLATION != ret && OB_REPLICA_NOT_READABLE != ret) {
              LOG_WARN("execute query fail", K(ret), "timeout_timestamp",
                       plan_ctx->get_timeout_timestamp());
            }
          }
        }
      }
      // Note: Do not use the sql_ member variable after calling the response_result interface, because the memory that sql_ points to comes from ObReqPacket
      // while in the transaction asynchronous commit response client process, the response client process is located in the ApplyService thread, and there is no timing guarantee with the SQL Worker thread
      //After starting the asynchronous response client process with response_result, it is possible that the response packet will be returned before executing the following logic. If the response packet is completed first, it will lead to
      //ObReqPacket memory is released, thus leading to sql_ pointing to an invalid memory address, accessing sql_ in the following logic will cause a crash
      // While the process of synchronously responding to the client does not have similar issues, this is because the flush action for the last packet in the synchronous response client is located at the very bottom of this interface,
      //but the asynchronous response process does not have this guarantee
      int tmp_ret = OB_SUCCESS;
      tmp_ret = OB_E(EventTable::EN_PRINT_QUERY_SQL) OB_SUCCESS;
      if (OB_SUCCESS != tmp_ret) {
        LOG_INFO("query info:",
              "sql", result.get_session().get_current_query_string(),
              "sess_id", result.get_session().get_server_sid(),
              "database_id", result.get_session().get_database_id(),
              "database_name", result.get_session().get_database_name(),
              "trans_id", audit_record.trans_id_);
      }
      //Monitoring item statistics end
      exec_end_timestamp_ = ObTimeUtility::current_time();

      // some statistics must be recorded for plan stat, even though sql audit disabled
      bool first_record = (1 == audit_record.try_cnt_);
      ObExecStatUtils::record_exec_timestamp(*this, first_record, audit_record.exec_timestamp_);
      audit_record.exec_timestamp_.update_stage_time();

      if (enable_perf_event && !THIS_THWORKER.need_retry()
        && OB_NOT_NULL(result.get_physical_plan())) {
        const int64_t time_cost = exec_end_timestamp_ - get_receive_timestamp();
        ObSQLUtils::record_execute_time(result.get_physical_plan()->get_plan_type(), time_cost);
      }
      // Retry needs to meet the following conditions:
      // 1. rs.open execution failed
      // 2. No result was returned to the client, this execution has no side effects
      // 3. need_retry(result, ret): schema or location cache invalidation
      // 4. less than retry count limit
      if (OB_UNLIKELY(retry_ctrl_.need_retry())) {
        if (OB_TRANSACTION_SET_VIOLATION != ret && OB_REPLICA_NOT_READABLE != ret && OB_TRY_LOCK_ROW_CONFLICT != ret) {
          //Lock conflict retry does not print log, to avoid screen flooding
          LOG_WARN("try to execute again",
                   K(ret),
                   N_TYPE, result.get_stmt_type(),
                   "retry_type", retry_ctrl_.get_retry_type(),
                   "timeout_remain", THIS_WORKER.get_timeout_remain());
        }
      } else {
        // Immediately freeze partition hit after the first plan execution completes
        // partition_hit once frozen, subsequent try_set_bool operations are ineffective
        if (OB_LIKELY(NULL != result.get_physical_plan())) {
          session.partition_hit().freeze();
        }

        // store the warning message from the most recent statement in the current session
        if ((OB_SUCC(ret) && is_diagnostics_stmt) || async_resp_used) {
          // If diagnostic stmt execute successfully, it dosen't clear the warning message.
          // Or if it response to client asynchronously, it doesn't clear the warning message here,
          // but will do it in the callback thread.
          session.update_show_warnings_buf();
        } else {
          session.set_show_warnings_buf(ret); // TODO: Move this to a better place, reduce some wb copy
        }

        if (OB_FAIL(ret) && !async_resp_used && need_response_error && is_conn_valid() && !THIS_WORKER.need_retry() &&
              !ctx_.multi_stmt_item_.is_batched_multi_stmt()) {
          if (OB_ERR_PROXY_REROUTE == ret) {
            LOG_DEBUG("query should be rerouted", K(ret), K(async_resp_used));
          } else {
            LOG_WARN("query failed", K(ret), K(session),
                     "sql", ctx_.is_sensitive_ ? ObString(OB_MASKED_STR) : sql,
                     K(retry_ctrl_.need_retry()));
          }
          // When need_retry=false, a packet may have been sent to the client, or no packets may have been sent at all.
          // However, it can be determined: this request has errored, and is not yet complete. If it has not already been handed over to asynchronous EndTrans for finalization,
          // then it is necessary to reply with an error_packet below as a conclusion. Otherwise, no one will help send the error packet to the client afterwards,
          // May cause the client to hang waiting for a response.
          bool is_partition_hit = session.get_err_final_partition_hit(ret);
          int err = send_error_packet(ret, NULL, is_partition_hit, (void *)ctx_.get_reroute_info());
          if (OB_SUCCESS != err) {  // send error packet
            LOG_WARN("send error packet failed", K(ret), K(err));
          }
        }
      }
    }

    if (enable_perf_event) {
      audit_record.exec_record_.record_end();
      record_stat(result.get_stmt_type(), exec_end_timestamp_, result.get_session(), ret, result.is_commit_cmd(), result.is_rollback_cmd());
      audit_record.stmt_type_ = result.get_stmt_type();
      audit_record.exec_record_.wait_time_end_ = total_wait_desc.time_waited_;
      audit_record.exec_record_.wait_count_end_ = total_wait_desc.total_waits_;
      audit_record.update_event_stage_state();
    }
    if (enable_sqlstat) {
      sqlstat_record.record_sqlstat_end_value();
      sqlstat_record.set_rows_processed(result.get_affected_rows() + result.get_return_rows());
      sqlstat_record.set_partition_cnt(result.get_exec_context().get_das_ctx().get_related_tablet_cnt());
      sqlstat_record.set_is_route_miss(result.get_session().partition_hit().get_bool()? 0 : 1);
      sqlstat_record.set_is_plan_cache_hit(ctx_.plan_cache_hit_);
      sqlstat_record.move_to_sqlstat_cache(result.get_session(),
                                                 ctx_.cur_sql_,
                                                 result.get_physical_plan());
    }

    audit_record.status_ = (0 == ret || OB_ITER_END == ret)
        ? REQUEST_SUCC : (ret);
    if (enable_sql_audit) {
      audit_record.seq_ = 0;  //don't use now
      audit_record.execution_id_ = session.get_current_execution_id();
      audit_record.client_addr_ = session.get_peer_addr();
      audit_record.user_client_addr_ = session.get_user_client_addr();
      audit_record.user_group_ = THIS_WORKER.get_group_id();
      MEMCPY(audit_record.sql_id_, ctx_.sql_id_, (int32_t)sizeof(audit_record.sql_id_));
      MEMCPY(audit_record.format_sql_id_, ctx_.format_sql_id_, (int32_t)sizeof(audit_record.format_sql_id_));
      audit_record.format_sql_id_[common::OB_MAX_SQL_ID_LENGTH] = '\0';
      audit_record.ccl_rule_id_ = ctx_.ccl_rule_id_;
      audit_record.ccl_match_time_ = ctx_.ccl_match_time_;

      if (NULL != plan) {
        audit_record.plan_type_ = plan->get_plan_type();
        audit_record.table_scan_ = plan->contain_table_scan();
        audit_record.plan_id_ = plan->get_plan_id();
        audit_record.plan_hash_ = plan->get_plan_hash_value();
        audit_record.rule_name_ = const_cast<char *>(plan->get_rule_name().ptr());
        audit_record.rule_name_len_ = plan->get_rule_name().length();
      }
      if (NULL != plan || result.is_pl_stmt(result.get_stmt_type())) {
        audit_record.partition_hit_ = session.partition_hit().get_bool();
      }
      if (OB_FAIL(ret) && audit_record.trans_id_ == 0) {
        // normally trans_id is set in the `start-stmt` phase,
        // if `start-stmt` hasn't run, set trans_id from session if an active txn exist
        if (session.is_in_transaction()) {
          audit_record.trans_id_ = session.get_tx_id();
        }
      }
      audit_record.affected_rows_ = result.get_affected_rows();
      audit_record.return_rows_ = result.get_return_rows();
      audit_record.partition_cnt_ = result.get_exec_context()
                                          .get_das_ctx()
                                          .get_related_tablet_cnt();
      audit_record.expected_worker_cnt_ = result.get_exec_context()
                                                .get_task_exec_ctx()
                                                .get_expected_worker_cnt();
      audit_record.used_worker_cnt_ = result.get_exec_context()
                                            .get_task_exec_ctx()
                                            .get_admited_worker_cnt();

      audit_record.is_executor_rpc_ = false;
      audit_record.is_inner_sql_ = false;
      audit_record.is_hit_plan_cache_ = result.get_is_from_plan_cache();
      audit_record.is_multi_stmt_ = session.get_capability().cap_flags_.OB_CLIENT_MULTI_STATEMENTS;
      audit_record.is_batched_multi_stmt_ = ctx_.multi_stmt_item_.is_batched_multi_stmt();

      if (audit_record.params_value_ == nullptr) {
        OZ (store_params_value_to_str(allocator, session, result.get_ps_params()));
        audit_record.params_value_ = params_value_;
        audit_record.params_value_len_ = params_value_len_;
      }
      audit_record.is_perf_event_closed_ = !lib::is_diagnose_info_enabled();
      audit_record.plsql_exec_time_ = session.get_plsql_exec_time();
      audit_record.plsql_compile_time_ = session.get_plsql_compile_time();
      if (result.is_pl_stmt(result.get_stmt_type()) && OB_NOT_NULL(ObCurTraceId::get_trace_id())) {
        audit_record.pl_trace_id_ = *ObCurTraceId::get_trace_id();
      }

      ObPhysicalPlanCtx *plan_ctx = result.get_exec_context().get_physical_plan_ctx();
      if (OB_ISNULL(plan_ctx)) {
        //do nothing
      } else {
        audit_record.consistency_level_ = plan_ctx->get_consistency_level();
        audit_record.total_memstore_read_row_count_ = plan_ctx->get_total_memstore_read_row_count();
        audit_record.total_ssstore_read_row_count_ = plan_ctx->get_total_ssstore_read_row_count();
      }
    }
      //update v$sql statistics
    if (session.get_local_ob_enable_plan_cache()
        && !retry_ctrl_.need_retry()) {
      ObIArray<ObTableRowCount> *table_row_count_list = NULL;
      ObPhysicalPlanCtx *plan_ctx = result.get_exec_context().get_physical_plan_ctx();
      if (OB_ISNULL(plan_ctx)) {
        // do nothing
      } else {
        table_row_count_list = &(plan_ctx->get_table_row_count_list());
        audit_record.table_scan_stat_ = plan_ctx->get_table_scan_stat();
      }
      if (NULL != plan) {
        AdaptivePCConf adpt_pc_conf;
        bool enable_adaptive_pc = plan_ctx->enable_adaptive_pc();
        if (enable_adaptive_pc) {
          adpt_pc_conf = session.get_adaptive_pc_conf();
        }
        if (!(ctx_.self_add_plan_) && ctx_.plan_cache_hit_) {
          plan->update_plan_stat(audit_record,
                                 false, // false mean not first update plan stat
                                 table_row_count_list,
                                 enable_adaptive_pc ? &adpt_pc_conf : nullptr);
          plan->update_cache_access_stat(audit_record.table_scan_stat_);
        } else if (ctx_.self_add_plan_ && !ctx_.plan_cache_hit_) {
          plan->update_plan_stat(audit_record,
                                 true,
                                 table_row_count_list,
                                 enable_adaptive_pc ? &adpt_pc_conf : nullptr);
          plan->update_cache_access_stat(audit_record.table_scan_stat_);
        } else if (ctx_.self_add_plan_ && ctx_.plan_cache_hit_) {
          // spm evolution plan first execute
          plan->update_plan_stat(audit_record,
                                 true,
                                 table_row_count_list,
                                 enable_adaptive_pc ? &adpt_pc_conf : nullptr);
          plan->update_cache_access_stat(audit_record.table_scan_stat_);
        }
      }
    }
    // reset thread waring buffer in sync mode
    if (!async_resp_used) {
      clear_wb_content(session);
    }

    need_disconnect = (result.get_exec_context().need_disconnect()
                       && !is_query_killed_return(ret));//Explicitly when it is a kill query, the connection should not be dropped
    if (need_disconnect) {
      LOG_WARN("need disconnect", K(ret), K(need_disconnect));
    }
    bool is_need_retry = THIS_THWORKER.need_retry() ||
        RETRY_TYPE_NONE != retry_ctrl_.get_retry_type();
    (void)ObSQLUtils::handle_audit_record(is_need_retry, EXECUTE_LOCAL, session,
        ctx_.is_sensitive_);
  }
  return ret;
}

int ObMPQuery::store_params_value_to_str(ObIAllocator &allocator,
                                         sql::ObSQLSessionInfo &session,
                                         common::ParamStore &params)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t length = OB_MAX_SQL_LENGTH;
  CK (OB_NOT_NULL(params_value_ = static_cast<char *>(allocator.alloc(OB_MAX_SQL_LENGTH))));
  for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
    const common::ObObjParam &param = params.at(i);
    if (param.is_ext()) {
      pos = 0;
      params_value_ = NULL;
      params_value_len_ = 0;
      break;
    } else {
      OZ (param.print_sql_literal(params_value_, length, pos, allocator, TZ_INFO(&session)));
      if (i != params.count() - 1) {
        OZ (databuff_printf(params_value_, length, pos, allocator, ","));
      }
    }
  }
  if (OB_FAIL(ret)) {
    params_value_ = NULL;
    params_value_len_ = 0;
    ret = OB_SUCCESS;
  } else {
    params_value_len_ = pos;
  }
  return ret;
}

//int ObMPQuery::fill_feedback_session_info(ObMySQLResultSet &result,
//                                          ObSQLSessionInfo &session)
//{
//  int ret = OB_SUCCESS;
//  ObPhysicalPlan *temp_plan = NULL;
//  ObTaskExecutorCtx *temp_task_ctx = NULL;
//  ObSchemaGetterGuard *schema_guard = NULL;
//  if (session.is_abundant_feedback_support() &&
//      NULL != (temp_plan = result.get_physical_plan()) &&
//      NULL != (temp_task_ctx = result.get_exec_context().get_task_executor_ctx()) &&
//      NULL != (schema_guard = ctx_.schema_guard_) &&
//      temp_plan->get_plan_type() == ObPhyPlanType::OB_PHY_PLAN_REMOTE &&
//      temp_plan->get_location_type() != ObPhyPlanType::OB_PHY_PLAN_UNCERTAIN &&
//      temp_task_ctx->get_table_locations().count() == 1 &&
//      temp_task_ctx->get_table_locations().at(0).get_partition_location_list().count() == 1) {
//    bool is_cache_hit = false;
//    ObFBPartitionParam param;
//    //FIXME: should remove ObPartitionKey
//    ObPartitionKey partition_key;
//    ObPartitionLocation partition_loc;
//    const ObTableSchema *table_schema = NULL;
//    ObPartitionReplicaLocationIArray &pl_array =
//        temp_task_ctx->get_table_locations().at(0).get_partition_location_list();
//    if (OB_FAIL(pl_array.at(0).get_partition_key(partition_key))) {
//      LOG_WARN("failed to get partition key", K(ret));
//    } else if (OB_FAIL(temp_cache->get(partition_key,
//                                       partition_loc,
//                                       0,
//                                       is_cache_hit))) {
//      LOG_WARN("failed to get partition location", K(ret));
//    } else if (OB_FAIL(schema_guard->get_table_schema(partition_key.get_tenant_id(),
//                                                      partition_key.get_table_id(),
//                                                      table_schema))) {
//      LOG_WARN("failed to get table schema", K(ret), K(partition_key));
//    } else if (OB_ISNULL(table_schema)) {
//      ret = OB_ERR_UNEXPECTED;
//      LOG_WARN("null table schema", K(ret));
//    } else if (OB_FAIL(build_fb_partition_param(*table_schema, partition_loc, param))) {
//      LOG_WARN("failed to build fb partition pararm", K(ret));
//    } else if (OB_FAIL(session.set_partition_location_feedback(param))) {
//      LOG_WARN("failed to set partition location feedback", K(param), K(ret));
//    } else { /*do nothing*/ }
//  } else { /*do nothing*/}
//  return ret;
//}

//int ObMPQuery::build_fb_partition_param(
//    const ObTableSchema &table_schema,
//    const ObPartitionLocation &partition_loc,
//    ObFBPartitionParam &param) {
//  INIT_SUCC(ret);
//  param.schema_version_ = table_schema.get_schema_version();
//  int64_t origin_partition_idx = OB_INVALID_ID;
//  if (OB_FAIL(param.pl_.assign(partition_loc))) {
//    LOG_WARN("fail to assign pl", K(partition_loc), K(ret));
//  }
//  // when table partition_id to client, we need convert it to
//  // real partition idx(e.g. hash partition split)
//  else if (OB_FAIL(table_schema.convert_partition_id_to_idx(
//          partition_loc.get_partition_id(), origin_partition_idx))) {
//    LOG_WARN("fail to convert partition id", K(partition_loc), K(ret));
//  } else {
//    param.original_partition_id_ = origin_partition_idx;
//  }
//
//  return ret;
//}

int ObMPQuery::check_readonly_stmt(ObMySQLResultSet &result)
{
  int ret = OB_SUCCESS;
  bool is_readonly = false;
  //In this phase, the show statement will be converted to a select statement,
  //if literal_stmt_type is not stmt::T_NONE, then it indicates the original type show
  const stmt::StmtType type = stmt::T_NONE == result.get_literal_stmt_type() ?
                              result.get_stmt_type() :
                              result.get_literal_stmt_type();
  ObConsistencyLevel consistency = INVALID_CONSISTENCY;
  if (OB_FAIL(is_readonly_stmt(result, is_readonly))) {
    LOG_WARN("check stmt is readonly fail", K(ret), K(result));
  } else if (!is_readonly) {
    ret = OB_ERR_READ_ONLY;
    LOG_WARN("stmt is not readonly", K(ret), K(result));
  } else if (stmt::T_SELECT == type) {
    //For the select statement, strong consistency read needs to be disabled
    //Through setting /*+read_consistency()*/ hint
    //or specify session-level ob_read_consistency = 2
    // to set weak consistency read
    const int64_t table_count = DAS_CTX(result.get_exec_context()).get_table_loc_list().size();
    if (0 == table_count) {
      //This case is special, jdbc will send a special statement select @@session.tx_read_only when sending the query statement;
      //For convenience of obtest testing, the restriction on the select statement for no table_locations needs to be removed
    } else if (OB_FAIL(result.get_read_consistency(consistency))) {
      LOG_WARN("get read consistency fail", K(ret));
    } else if (WEAK != consistency) {
      ret = OB_ERR_READ_ONLY;
      LOG_WARN("strong consistency read is not allowed", K(ret), K(type), K(consistency));
    }
  }
  return ret;
}

int ObMPQuery::is_readonly_stmt(ObMySQLResultSet &result, bool &is_readonly)
{
  int ret = OB_SUCCESS;
  is_readonly = false;
  const stmt::StmtType type = stmt::T_NONE == result.get_literal_stmt_type() ?
                              result.get_stmt_type() :
                              result.get_literal_stmt_type();
  switch (type) {
    case stmt::T_SELECT: {
      //Forbid the select...for update statement as well
      ObPhysicalPlan *physical_plan = result.get_physical_plan();
      if (NULL == physical_plan) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("physical_plan should not be null", K(ret));
      } else if (physical_plan->has_for_update()) {
        is_readonly = false;
      } else {
        is_readonly = true;
      }
      break;
    }
    case stmt::T_VARIABLE_SET: {
      //Prohibit set @@global.variable statement
      if (result.has_global_variable()) {
        is_readonly = false;
      } else {
        is_readonly = true;
      }
      break;
    }
    case stmt::T_EXPLAIN:
    case stmt::T_SHOW_TABLES:
    case stmt::T_SHOW_DATABASES:
    case stmt::T_SHOW_COLUMNS:
    case stmt::T_SHOW_VARIABLES:
    case stmt::T_SHOW_TABLE_STATUS:
    case stmt::T_SHOW_SCHEMA:
    case stmt::T_SHOW_CREATE_DATABASE:
    case stmt::T_SHOW_CREATE_TABLE:
    case stmt::T_SHOW_CREATE_VIEW:
    case stmt::T_SHOW_PARAMETERS:
    case stmt::T_SHOW_SERVER_STATUS:
    case stmt::T_SHOW_INDEXES:
    case stmt::T_SHOW_WARNINGS:
    case stmt::T_SHOW_ERRORS:
    case stmt::T_SHOW_PROCESSLIST:
    case stmt::T_SHOW_CHARSET:
    case stmt::T_SHOW_COLLATION:
    case stmt::T_SHOW_TABLEGROUPS:
    case stmt::T_SHOW_STATUS:
    case stmt::T_SHOW_TENANT:
    case stmt::T_SHOW_CREATE_TENANT:
    case stmt::T_SHOW_TRACE:
    case stmt::T_SHOW_TRIGGERS:
    case stmt::T_SHOW_ENGINES:
    case stmt::T_SHOW_PRIVILEGES:
    case stmt::T_SHOW_RESTORE_PREVIEW:
    case stmt::T_SHOW_GRANTS:
    case stmt::T_SHOW_QUERY_RESPONSE_TIME:
    case stmt::T_SHOW_RECYCLEBIN:
    case stmt::T_SHOW_PROFILE:
    case stmt::T_SHOW_SEQUENCES:
    case stmt::T_SHOW_ENGINE:
    case stmt::T_SHOW_OPEN_TABLES:
    case stmt::T_HELP:
    case stmt::T_USE_DATABASE:
    case stmt::T_SET_NAMES: //read only not restrict it
    case stmt::T_START_TRANS:
    case stmt::T_END_TRANS:
    case stmt::T_SHOW_CHECK_TABLE:
    case stmt::T_SHOW_CREATE_USER:
    case stmt::T_SET_CATALOG:
    case stmt::T_SHOW_CATALOGS:
    case stmt::T_SHOW_CREATE_CATALOG:
    case stmt::T_SHOW_LOCATIONS:
    case stmt::T_SHOW_CREATE_LOCATION:
    case stmt::T_LOCATION_UTILS_LIST: {
      is_readonly = true;
      break;
    }
    default: {
      is_readonly = false;
      break;
    }
  }
  return ret;
}
int ObMPQuery::deserialize()
{
  int ret = OB_SUCCESS;

  //OB_ASSERT(req_);
  //OB_ASSERT(req_->get_type() == ObRequest::OB_MYSQL);
  if ( (OB_ISNULL(req_)) || (req_->get_type() != ObRequest::OB_MYSQL)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid request", K(ret), K(req_));
  } else if (get_is_com_filed_list()) {
    if (OB_FAIL(deserialize_com_field_list())) {
      LOG_WARN("failed to deserialize com field list", K(ret));
    }
  } else {
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    sql_.assign_ptr(const_cast<char *>(pkt.get_cdata()), pkt.get_clen()-1);
  }

  return ret;
}

// return false only if send packet fail.
OB_INLINE int ObMPQuery::response_result(ObMySQLResultSet &result,
                                         bool force_sync_resp,
                                         bool &async_resp_used)
{
  int ret = OB_SUCCESS;
  FLTSpanGuard(sql_execute);
  // When ac = 1, starting a new transaction in the thread to clean up data in the Oracle temporary table will form a deadlock with the clog callback, so it is changed to a synchronous method here
  ObSQLSessionInfo &session = result.get_session();
  CHECK_COMPATIBILITY_MODE(&session);

  bool need_trans_cb  = result.need_end_trans_callback() && (!force_sync_resp);
  // Determine whether it is plan or cmd by checking if plan is null
  // Handle plan and cmd separately, the logic will be clearer.
  if (OB_LIKELY(NULL != result.get_physical_plan())) {
    if (need_trans_cb) {
      ObAsyncPlanDriver drv(gctx_, ctx_, session, retry_ctrl_, *this);
      // NOTE: sql_end_cb must be initialized before drv.response_result()
      ObSqlEndTransCb &sql_end_cb = session.get_mysql_end_trans_cb();
      if (OB_FAIL(sql_end_cb.init(packet_sender_, &session))) {
        LOG_WARN("failed to init sql end callback", K(ret));
      } else if (OB_FAIL(drv.response_result(result))) {
        LOG_WARN("fail response async result", K(ret));
      }
      async_resp_used = result.is_async_end_trans_submitted();
    } else {
      // Pilot ObQuerySyncDriver
      ObSyncPlanDriver drv(gctx_, ctx_, session, retry_ctrl_, *this);
      ret = drv.response_result(result);
    }
  } else {

#define CMD_EXEC \
    if (need_trans_cb) {\
      ObSqlEndTransCb &sql_end_cb = session.get_mysql_end_trans_cb(); \
      ObAsyncCmdDriver drv(gctx_, ctx_, session, retry_ctrl_, *this); \
      if (OB_FAIL(sql_end_cb.init(packet_sender_, &session))) { \
        LOG_WARN("failed to init sql end callback", K(ret)); \
      } else if (OB_FAIL(drv.response_result(result))) { \
        LOG_WARN("fail response async result", K(ret)); \
      } \
      async_resp_used = result.is_async_end_trans_submitted(); \
    } else { \
      ObSyncCmdDriver drv(gctx_, ctx_, session, retry_ctrl_, *this); \
      session.set_pl_query_sender(&drv); \
      session.set_ps_protocol(result.is_ps_protocol()); \
      ret = drv.response_result(result); \
      session.set_pl_query_sender(NULL); \
    }
  
    if (result.is_pl_stmt(result.get_stmt_type())) {
      CMD_EXEC;
    } else {
      CMD_EXEC;
    }

#undef CMD_EXEC

  }

  return ret;
}

inline void ObMPQuery::record_stat(const stmt::StmtType type, 
                                   const int64_t end_time,
                                   const sql::ObSQLSessionInfo& session,
                                   const int64_t ret,
                                   const bool is_commit_cmd,
                                   const bool is_rollback_cmd) const
{
#define ADD_STMT_STAT(type)                     \
  case stmt::T_##type:                          \
    if (!session.get_is_in_retry()) {           \
      EVENT_INC(SQL_##type##_COUNT);            \
      if (OB_SUCCESS != ret) {                  \
        EVENT_INC(SQL_FAIL_COUNT);              \
      }                                         \
    }                                           \
    EVENT_ADD(SQL_##type##_TIME, time_cost);    \
    break
  int64_t start_ts = 0;
  if (session.get_raw_audit_record().exec_timestamp_.multistmt_start_ts_ > 0) {
    // In the scenario of multi-query, use the start time of the current query
    start_ts = session.get_raw_audit_record().exec_timestamp_.multistmt_start_ts_;
  } else {
    start_ts = get_receive_timestamp();
  }
  const int64_t time_cost = end_time - start_ts;
  if (!THIS_THWORKER.need_retry())
  {
    switch (type)
    {
      ADD_STMT_STAT(SELECT);
      ADD_STMT_STAT(INSERT);
      ADD_STMT_STAT(REPLACE);
      ADD_STMT_STAT(UPDATE);
      ADD_STMT_STAT(DELETE);
      case stmt::T_END_TRANS:
        if (is_commit_cmd) {
          EVENT_ADD(SQL_COMMIT_TIME, time_cost);
          if (!session.get_is_in_retry()) {
            EVENT_INC(SQL_COMMIT_COUNT);
            if (OB_SUCCESS != ret) {
              EVENT_INC(SQL_FAIL_COUNT);
            }
          }
        } else if (is_rollback_cmd) {
          EVENT_ADD(SQL_ROLLBACK_TIME, time_cost);
          if (!session.get_is_in_retry()) {
            EVENT_INC(SQL_ROLLBACK_COUNT);
            if (OB_SUCCESS != ret) {
              EVENT_INC(SQL_FAIL_COUNT);
            }
          }
        }
        break;

    default:
    {
      EVENT_ADD(SQL_OTHER_TIME, time_cost);
      if (!session.get_is_in_retry()) {
        EVENT_INC(SQL_OTHER_COUNT);
        if (OB_SUCCESS != ret) {
          EVENT_INC(SQL_FAIL_COUNT);
        }
      }
    }
    }
  }
#undef ADD_STMT_STAT
}

int ObMPQuery::deserialize_com_field_list()
{
  int ret = OB_SUCCESS;
  //If only column definitions are to be returned, it means the client sent a COM_FIELD_LIST command
  /* mysql's COM_FIELD_LIST command is used to get the column definitions of a table, its packet from client to server is:
  *  1              [04] COM_FIELD_LIST
  *  string[NUL]    table
  *  string[EOF]    field wildcard
  *  First is the CMD type, then the table name, followed by the match condition
  *
  * The packet from server to client is one of the following:
  * 1. an ERR_Packet (returns an error packet)
  * 2. one or more Column Definition packets and a closing EOF_Packet (returns n column definitions + EOF)
  *
  * Since the result of a regular SELECT query already includes column definitions, and to maximize the reuse of the current code logic, the COM_FIELD_LIST command
  * can be considered equivalent to:
  * select * from table limit 0 ==> get field define ==> return Column Definition based on field wildcard as needed
  *
  * Reference: https://dev.mysql.com/doc/internals/en/com-field-list.html
   */
  ObIAllocator *alloc = &THIS_WORKER.get_sql_arena_allocator();
  if (OB_ISNULL(alloc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(alloc), K(ret));
  } else {
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    const char *str = pkt.get_cdata();
    uint32_t length = pkt.get_clen();
    const char *str1 = "select * from ";
    const char *str2 = " where 0";
    int64_t i = 0;
    const int64_t str1_len = strlen(str1);
    const int64_t str2_len = strlen(str2);
    const int pre_size = str1_len + str2_len;
    //Find the delimiter between client-provided table_name and field wildcard (table_name [NULL] field wildcard)
    for (; static_cast<int>(str[i]) != 0 && i < length; ++i) {}
    char *dest_str = static_cast<char *>(alloc->alloc(length + pre_size));
    if (OB_ISNULL(dest_str)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc", K(dest_str));
    } else {
      char *buf = dest_str;
      uint32_t real_len = 0;
      MEMSET(buf, 0, length + pre_size);
      MEMCPY(buf, str1, str1_len);
      buf = buf + str1_len;
      real_len = real_len + str1_len;
      MEMCPY(buf, str, i);
      buf = buf + i;
      real_len = real_len + i;
      MEMCPY(buf, str2, str2_len);
      real_len = real_len + str2_len;
      sql_.assign_ptr(dest_str, real_len);
      //extract wildcard
      if (i + 1 < length - 1) {
        wild_str_.assign_ptr(str + i + 1, (length - 1) - (i +1));
      }
    }
  }
  return ret;
}
