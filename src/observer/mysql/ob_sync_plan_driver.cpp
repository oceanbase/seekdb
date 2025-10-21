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

#include "observer/mysql/ob_sync_plan_driver.h"
#include "rpc/obmysql/packet/ompk_eof.h"
#include "observer/mysql/obmp_query.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace obmysql;
namespace observer
{

ObSyncPlanDriver::ObSyncPlanDriver(const ObGlobalContext &gctx,
                                   const ObSqlCtx &ctx,
                                   sql::ObSQLSessionInfo &session,
                                   ObQueryRetryCtrl &retry_ctrl,
                                   ObIMPPacketSender &sender,
                                   bool is_prexecute,
                                   int32_t iteration_count)
    : ObQueryDriver(gctx, ctx, session, retry_ctrl, sender, is_prexecute),
    iteration_count_(iteration_count)
{
}

ObSyncPlanDriver::~ObSyncPlanDriver()
{
}

int ObSyncPlanDriver::response_result(ObMySQLResultSet &result)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_sql_execution);
  int ret = OB_SUCCESS;
  bool process_ok = false;
  // for select SQL
  bool ac = true;
  bool admission_fail_and_need_retry = false;
  if (OB_ISNULL(result.get_physical_plan())) {
    ret = OB_NOT_INIT;
    LOG_WARN("should have set plan to result set", K(ret));
  } else if (OB_FAIL(session_.get_autocommit(ac))) {
    LOG_WARN("fail to get autocommit", K(ret));
  } else if (OB_FAIL(result.open())) {
    int cret = OB_SUCCESS;
    int cli_ret = OB_SUCCESS;
    // move result.close() below, after test_and_save_retry_state().
    // open failed, decide whether to retry
    retry_ctrl_.test_and_save_retry_state(gctx_,
                                          ctx_,
                                          result,
                                          ret,
                                          cli_ret);
    if (OB_TRANSACTION_SET_VIOLATION != ret && OB_REPLICA_NOT_READABLE != ret) {
      if (OB_TRY_LOCK_ROW_CONFLICT == ret && retry_ctrl_.need_retry()) {
        //Lock conflict retry does not print logs to avoid screen flooding
      } else {
        LOG_WARN("result set open failed, check if need retry",
                 K(ret), K(cli_ret), K(retry_ctrl_.need_retry()));
      }
    }
    if (retry_ctrl_.need_retry()) {
      result.set_will_retry();
    }
    cret = result.close(cli_ret);
    if (cret != OB_SUCCESS &&
        cret != OB_TRANSACTION_SET_VIOLATION &&
        OB_TRY_LOCK_ROW_CONFLICT != cret) {
      LOG_WARN("close result set fail", K(cret));
    }
    ret = cli_ret;
  } else if (result.is_with_rows()) {
    // is the result set, no retries after starting to send data
    bool can_retry = false;
    if (OB_FAIL(response_query_result(result,
                                      result.is_ps_protocol(),
                                      result.has_more_result(),
                                      can_retry,
                                      is_prexecute_ && stmt::T_SELECT == result.get_stmt_type() ?
                                          iteration_count_ + 1 : OB_INVALID_COUNT))) {
      LOG_WARN("response query result fail", K(ret));
      // move result.close() below, after test_and_save_retry_state().
      if (can_retry) {
        // Can retry, check here if we need to retry
        int cli_ret = OB_SUCCESS;
        // response query result failed, decide whether to retry
        retry_ctrl_.test_and_save_retry_state(gctx_,
                                              ctx_,
                                              result,
                                              ret,
                                              cli_ret);
        LOG_WARN("result response failed, check if need retry",
                 K(ret), K(cli_ret), K(retry_ctrl_.need_retry()));
        ret = cli_ret;
      } else {
        result.refresh_location_cache_by_errno(true, ret);
      }
      if (retry_ctrl_.need_retry()) {
        result.set_will_retry();
      }
      int cret = result.close(ret);
      if (cret != OB_SUCCESS) {
        LOG_WARN("close result set fail", K(cret));
      }
    } else if (OB_FAIL(result.close())) {
      LOG_WARN("close result set fail", K(ret));
    } else {
      process_ok = true;

      OMPKEOF eofp;
      bool need_send_eof = false;
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
        = (session_.is_server_status_in_transaction() ? 1 : 0);
      flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT = (ac ? 1 : 0);
      flags.status_flags_.OB_SERVER_MORE_RESULTS_EXISTS = result.has_more_result();
      flags.status_flags_.OB_SERVER_STATUS_CURSOR_EXISTS = is_prexecute_ ? true : false;
      flags.status_flags_.OB_SERVER_STATUS_LAST_ROW_SENT = is_prexecute_ ? true : false;
      if (!session_.is_obproxy_mode()) {
        // in java client or others, use slow query bit to indicate partition hit or not
        flags.status_flags_.OB_SERVER_QUERY_WAS_SLOW = !session_.partition_hit().get_bool();
      }

      eofp.set_server_status(flags);

      // for proxy
      // in multi-stmt, send extra ok packet in the last stmt(has no more result)
      if (!is_prexecute_ && !result.has_more_result()
            && OB_FAIL(sender_.update_last_pkt_pos())) {
        LOG_WARN("failed to update last packet pos", K(ret));
      }
      if (OB_SUCC(ret) && !result.get_is_com_filed_list()) {
        need_send_eof = true;
      }

      if (OB_FAIL(ret)) {
        // do nothing
      } else if ((is_prexecute_ && stmt::T_SELECT != result.get_stmt_type()) 
        || (!is_prexecute_ && sender_.need_send_extra_ok_packet() && !result.has_more_result())) {
        // Two-in-one protocol select statement's OK packet is entirely sent at the protocol layer
        // sync plan At this time, a separate OK packet needs to be sent for the combined protocol, for obproxy,
        // in multi-stmt, send extra ok packet in the last stmt(has no more result)
        ObOKPParam ok_param;
        ok_param.affected_rows_ = result.get_affected_rows();
        ok_param.is_partition_hit_ = session_.partition_hit().get_bool();
        ok_param.has_more_result_ = result.has_more_result();
        ok_param.send_last_row_ = is_prexecute_ ? true : false;
        if (need_send_eof) {
          if (OB_FAIL(sender_.send_ok_packet(session_, ok_param, &eofp))) {
            LOG_WARN("fail to send ok packt", K(ok_param), K(ret));
          }
        } else {
          if (OB_FAIL(sender_.send_ok_packet(session_, ok_param))) {
            LOG_WARN("fail to send ok packt", K(ok_param), K(ret));
          }
        }
      } else {
        // Two-in-one protocol select statement result set EOF packets should be sent here
        // Not a combined protocol, the EOF packet without an additional OK packet needs to be sent here
        if (need_send_eof && OB_FAIL(sender_.response_packet(eofp, &result.get_session()))) {
          LOG_WARN("response packet fail", K(ret));
        }
      }
    }
  } else {
    if (is_prexecute_ && OB_FAIL(response_query_header(result, false, false, true))) {
      // need close result set
      int close_ret = OB_SUCCESS;
      if (OB_SUCCESS != (close_ret = result.close())) {
        LOG_WARN("close result failed", K(close_ret));
      }
      LOG_WARN("prexecute response query head fail. ", K(ret));
    } else if (OB_FAIL(result.close())) {
      LOG_WARN("close result set fail", K(ret));
    } else {
      if (!result.has_implicit_cursor()) {
        //no implicit cursor, send one ok packet to client
        ObOKPParam ok_param;
        ok_param.message_ = const_cast<char*>(result.get_message());
        ok_param.affected_rows_ = result.get_affected_rows();
        ok_param.lii_ = result.get_last_insert_id_to_client();
        const ObWarningBuffer *warnings_buf = common::ob_get_tsi_warning_buffer();
        if (OB_ISNULL(warnings_buf)) {
          // ignore ret
          LOG_WARN("can not get thread warnings buffer");
        } else {
          ok_param.warnings_count_ =
              static_cast<uint16_t>(warnings_buf->get_readable_warning_count());
        }
        ok_param.is_partition_hit_ = session_.partition_hit().get_bool();
        ok_param.has_more_result_ = result.has_more_result();
        process_ok = true;
        if (OB_FAIL(sender_.send_ok_packet(session_, ok_param))) {
          LOG_WARN("send ok packet fail", K(ok_param), K(ret));
        }
      } else {
        //has implicit cursor, send ok packet to client by implicit cursor
        result.reset_implicit_cursor_idx();
        int64_t curr_affected_row = 0;
        while (OB_SUCC(ret) && OB_SUCC(result.switch_implicit_cursor(curr_affected_row))) {
          ObOKPParam ok_param;
          ok_param.message_ = const_cast<char*>(result.get_message());
          ok_param.affected_rows_ = curr_affected_row;
          ok_param.is_partition_hit_ = session_.partition_hit().get_bool();
          ok_param.has_more_result_ = !result.is_cursor_end();
          ok_param.lii_ = result.get_last_insert_id_to_client();
          process_ok = true;
          if (OB_FAIL(sender_.send_ok_packet(session_, ok_param))) {
            LOG_WARN("send ok packet failed", K(ret), K(ok_param));
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        if (OB_FAIL(ret)) {
          LOG_WARN("send implicit cursor info to client failed", K(ret));
        }
      }
    }
  }
  //if the error code is ob_timeout, we add more error info msg for dml query.
  if (OB_TIMEOUT == ret && session_.is_user_session()) {
    LOG_USER_ERROR(OB_TIMEOUT, THIS_WORKER.get_timeout_ts() - session_.get_query_start_time());
  }

  if (OB_FAIL(ret) &&
      !process_ok &&
      !retry_ctrl_.need_retry() &&
      !admission_fail_and_need_retry
      && OB_BATCHED_MULTI_STMT_ROLLBACK != ret) {
    //OB_BATCHED_MULTI_STMT_ROLLBACK if it is a batch stmt rollback error, do not return to the client, fall back to mpquery for retry
    if (ctx_.multi_stmt_item_.is_batched_multi_stmt()) {
      // The error of batch optimization execution does not need to send error packet here,
      // because the upper layer will force a fallback to a single line execution retry
    } else {
      int sret = OB_SUCCESS;
      bool is_partition_hit = session_.get_err_final_partition_hit(ret);
      if (OB_SUCCESS != (sret = sender_.send_error_packet(ret, NULL, is_partition_hit, (void *)ctx_.get_reroute_info()))) {
        LOG_WARN("send error packet fail", K(sret), K(ret));
      }
    }
  }
  return ret;
}
}/* ns observer*/
}/* ns oceanbase */
