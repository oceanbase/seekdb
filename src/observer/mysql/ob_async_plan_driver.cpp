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

#include "ob_async_plan_driver.h"

#include "observer/mysql/obmp_query.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace obmysql;
namespace observer
{

ObAsyncPlanDriver::ObAsyncPlanDriver(const ObGlobalContext &gctx,
                                     const ObSqlCtx &ctx,
                                     sql::ObSQLSessionInfo &session,
                                     ObQueryRetryCtrl &retry_ctrl,
                                     ObIMPPacketSender &sender,
                                     bool is_prexecute)
    : ObQueryDriver(gctx, ctx, session, retry_ctrl, sender, is_prexecute)
{
}

ObAsyncPlanDriver::~ObAsyncPlanDriver()
{
}

int ObAsyncPlanDriver::response_result(ObMySQLResultSet &result)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_sql_execution);
  int ret = OB_SUCCESS;
  // After result.open, all required parameters such as last insert id for pkt_param have been calculated
  // For asynchronous add, delete, and modify operations, it is necessary to update the last insert id in advance to ensure the pkt_param parameter is correct in the callback
  // After the result set is closed, store_last_insert_id will be called again
  ObCurTraceId::TraceId *cur_trace_id = NULL;
  if (OB_ISNULL(cur_trace_id = ObCurTraceId::get_trace_id())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("current trace id is NULL", K(ret));
  } else if (OB_FAIL(result.open())) {
    LOG_WARN("failed to do result set open", K(ret));
  } else if (OB_FAIL(result.update_last_insert_id_to_client())) {
    LOG_WARN("failed to update last insert id after open", K(ret));
  } else {
    // open success, allow asynchronous response
    result.set_end_trans_async(true);
  }

  if (OB_SUCCESS != ret) {
    // If try_again is true, it means this SQL needs to be redone. Considering that we need to roll back the entire transaction before redoing, EndTransCb will be called
    // So here we set a flag to tell EndTransCb not to send a response to the client in this case.
    int cli_ret = OB_SUCCESS;
    retry_ctrl_.test_and_save_retry_state(gctx_, ctx_, result, ret, cli_ret);
    if (retry_ctrl_.need_retry()) {
      result.set_will_retry();
      result.set_end_trans_async(false);
    }
    // the story behind close:
    // if (try_again) {
    //   return here after result.close() ends, then run the retry logic
    // } else {
    //   After result.close() ends, the process flow should end cleanly, leave everything else to be done in the callback
    // }
    int cret = result.close();
    if (retry_ctrl_.need_retry()) {
      LOG_WARN("result set open failed, will retry",
               K(ret), K(cli_ret), K(cret), K(retry_ctrl_.need_retry()));
    } else {
      LOG_WARN("result set open failed, let's leave process(). EndTransCb will clean this mess",
               K(ret), K(cli_ret), K(cret), K(retry_ctrl_.need_retry()));
    }
    ret = cli_ret;
  } else if (is_prexecute_ && OB_FAIL(response_query_header(result, false, false, true))) {
    /*
    * prexecute only sends the header packet when execution is successful
    * There are two manifestations when execution fails
    *  1. Only an error packet is returned, at which point attention should be paid to the leakage of ps stmt
    *  2. Retry, local retries are directly handed over to the upper layer, package retries need to pay attention to the leakage of ps stmt
    *  3. response_query_header & flush_buffer will not produce errors that require retry, this position is one step before ObAsyncPlanDriver retries, with no other possible retry operations in between
    *  4. Cleaning up ps stmt needs to consider the situation of asynchronous responses, and cleanup may need to be done within the asynchronous packets
    */ 
    // need close result set
    int close_ret = OB_SUCCESS;
    if (OB_SUCCESS != (close_ret = result.close())) {
      LOG_WARN("close result failed", K(close_ret));
    }
    LOG_WARN("prexecute response query head fail. ", K(ret));
  } else if (result.is_with_rows()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("SELECT should not use async method. wrong code!!!", K(ret));
  } else if (OB_FAIL(result.close())) {
    LOG_WARN("result close failed, let's leave process(). EndTransCb will clean this mess", K(ret));
  } else {
    // async did not call ObSqlEndTransCb to reply OK package
    // So the OK packet of the combined protocol should also be handled here
    if (is_prexecute_) {
      if (stmt::T_SELECT == result.get_stmt_type()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("select stmt do not use async plan in prexecute.", K(ret));
      } else if (!result.is_async_end_trans_submitted()) {
        // is_async_end_trans_submitted indicates that the asynchronous response is ready
        ObOKPParam ok_param;
        ok_param.affected_rows_ = result.get_affected_rows();
        ok_param.is_partition_hit_ = session_.partition_hit().get_bool();
        ok_param.has_more_result_ = result.has_more_result();
        ok_param.send_last_row_ = true;
        if (OB_FAIL(sender_.send_ok_packet(session_, ok_param))) {
          LOG_WARN("fail to send ok packt", K(ok_param), K(ret));
        }
      }
    }
  }
  // Only set after end_trans is executed (regardless of success or failure), meaning a callback will definitely occur
  // The reason for designing this value is that open/close might "fall short" and never actually reach the end
  // end_trans() interface. This variable acts as a final confirmation.
  bool async_resp_used = result.is_async_end_trans_submitted();
  if (async_resp_used && retry_ctrl_.need_retry()) {
    LOG_ERROR("the async request is ok, couldn't send request again");
  }
  LOG_DEBUG("test if async end trans submitted",
            K(ret), K(async_resp_used), K(retry_ctrl_.need_retry()));

  //if the error code is ob_timeout, we add more error info msg for dml query.
  if (OB_TIMEOUT == ret && session_.is_user_session()) {
    LOG_USER_ERROR(OB_TIMEOUT, THIS_WORKER.get_timeout_ts() - session_.get_query_start_time());
  }
  // Error handling, responsible for returning error packets when not going asynchronous
  if (!OB_SUCC(ret) && !async_resp_used && !retry_ctrl_.need_retry()) {
    int sret = OB_SUCCESS;
    bool is_partition_hit = session_.get_err_final_partition_hit(ret);
    if (OB_SUCCESS != (sret = sender_.send_error_packet(ret, NULL, is_partition_hit))) {
      LOG_WARN("send error packet fail", K(sret), K(ret));
    }
    //According to the agreement with the transaction layer, regardless of whether end_participant and end_stmt succeed or not,
    //Determine whether the transaction commit or rollback is successful by only checking if the final end_trans is successful,
    // and SQL must ensure that end_trans is called, when calling end_trans it checks if the connection needs to be terminated,
    //So here there is no need to check if the connection needs to be terminated
  }
  return ret;
}


}/* ns observer*/
}/* ns oceanbase */


