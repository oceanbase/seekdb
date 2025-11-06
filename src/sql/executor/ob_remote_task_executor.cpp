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

#define USING_LOG_PREFIX SQL_EXE

#include "sql/engine/ob_exec_context.h"
#include "sql/executor/ob_remote_task_executor.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

ObRemoteTaskExecutor::ObRemoteTaskExecutor()
{
}

ObRemoteTaskExecutor::~ObRemoteTaskExecutor()
{
}

int ObRemoteTaskExecutor::execute(ObExecContext &query_ctx, ObJob *job, ObTaskInfo *task_info)
{
  int ret = OB_SUCCESS;
  RemoteExecuteStreamHandle *handler = NULL;
  ObSQLSessionInfo *session = query_ctx.get_my_session();
  ObPhysicalPlanCtx *plan_ctx = query_ctx.get_physical_plan_ctx();
  ObExecutorRpcImpl *rpc = NULL;
  ObQueryRetryInfo *retry_info = NULL;
  ObTask task;
  bool has_sent_task = false;
  bool has_transfer_err = false;

  if (OB_ISNULL(task_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("task info is NULL", K(ret));
  } else {
    if (OB_FAIL(ObTaskExecutorCtxUtil::get_stream_handler(query_ctx, handler))) {
      LOG_WARN("fail get task response handler", K(ret));
    } else if (OB_FAIL(ObTaskExecutorCtxUtil::get_task_executor_rpc(query_ctx, rpc))) {
      LOG_WARN("fail get executor rpc", K(ret));
    } else if (OB_ISNULL(session) || OB_ISNULL(plan_ctx) || OB_ISNULL(handler) || OB_ISNULL(rpc)
        || OB_ISNULL(retry_info = &session->get_retry_info_for_update())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("unexpected null ptr", K(ret), K(session), K(plan_ctx), K(handler), K(rpc), K(retry_info));
    } else if (OB_FAIL(build_task(query_ctx, *job, *task_info, task))) {
      LOG_WARN("fail build task", K(ret), K(job), K(task_info));
    } else if (OB_FAIL(handler->reset_and_init_result())) {
      LOG_WARN("fail to reset and init result", K(ret));
    } else {
      // Set task_info to OB_TASK_STATE_RUNNING state, which may be used for retries later
      task_info->set_state(OB_TASK_STATE_RUNNING);
      const int32_t group_id = OB_INVALID_ID == session->get_expect_group_id() ? 0 : session->get_expect_group_id();
      ObExecutorRpcCtx rpc_ctx(session->get_rpc_tenant_id(),
                               plan_ctx->get_timeout_timestamp(),
                               query_ctx.get_task_exec_ctx().get_min_cluster_version(),
                               retry_info,
                               query_ctx.get_my_session(),
                               plan_ctx->is_plain_select_stmt(),
                               group_id);
      if (OB_FAIL(rpc->task_execute(rpc_ctx,
                                    task,
                                    task_info->get_task_location().get_server(),
                                    *handler,
                                    has_sent_task,
                                    has_transfer_err))) {
        bool skip_failed_tasks = false;
        int check_ret = OB_SUCCESS;
        if (OB_SUCCESS != (check_ret = should_skip_failed_tasks(*task_info, skip_failed_tasks))) {
          // check fail, set ret to check_ret
          LOG_WARN("fail to check if it should skip failed tasks", K(ret), K(check_ret), K(*job));
          ret = check_ret;
        } else if (true == skip_failed_tasks) {
          // should skip failed tasks, log user warning and skip it, and set handler's error code to
          // OB_ERR_TASK_SKIPPED, than return OB_SUCCESS
          // Set task_info to OB_TASK_STATE_SKIPPED state, which may be used if a retry occurs later
          task_info->set_state(OB_TASK_STATE_SKIPPED);
          LOG_WARN("fail to do task on the remote server, log user warning and skip it",
                   K(ret), K(task_info->get_task_location().get_server()), K(*job));
          ObCStringHelper helper;
          LOG_USER_WARN(OB_ERR_TASK_SKIPPED,
                        helper.convert(task_info->get_task_location().get_server()),
                        common::ob_errpkt_errno(ret, false));
          handler->set_result_code(OB_ERR_TASK_SKIPPED);
          ret = OB_SUCCESS;
        } else {
          // let user see ret
          LOG_WARN("fail post task", K(ret));
        }
      }
      // handle tx relative info if plan involved in transaction
      const ObPhysicalPlan *plan = plan_ctx->get_phy_plan();
      if (plan && plan->is_need_trans()) {
        int tmp_ret = handle_tx_after_rpc(handler->get_result(),
                                        session,
                                        has_sent_task,
                                        has_transfer_err,
                                        plan,
                                          query_ctx);
        ret = COVER_SUCC(tmp_ret);
      }

      if (OB_SUCC(ret)) {
        ObExecFeedbackInfo &fb_info = handler->get_result()->get_feedback_info();
        if (OB_FAIL(query_ctx.get_feedback_info().merge_feedback_info(fb_info))) {
          LOG_WARN("fail to merge exec feedback info", K(ret));
        }
      }
      NG_TRACE_EXT(remote_task_completed, OB_ID(ret), ret,
                   OB_ID(runner_svr), task_info->get_task_location().get_server(),
                   OB_ID(task), task);
      // Description: After this function returns, control will eventually enter ObDirectReceive,
      // It obtains the current handler through get_stream_handler()
      // Then block waiting to receive the result on handler

      //
      // nothing more to do
      //
    }

    if (OB_FAIL(ret)) {
      // If failed, then set task_info to OB_TASK_STATE_FAILED state,
      // This way if a retry is needed later, it may use this status to retrieve the partition information from this task_info
      // Add to the partition that needs retry
      task_info->set_state(OB_TASK_STATE_FAILED);
    }
  }

  return ret;
}

int ObRemoteTaskExecutor::build_task(ObExecContext &query_ctx,
                                     ObJob &job,
                                     ObTaskInfo &task_info,
                                     ObTask &task)
{
  int ret = OB_SUCCESS;
  /* The content to be serialized includes:
   *  1. ObPhysicalPlanCtx
   *  2. ObPhyOperator Tree
   *  3. ObPhyOperator Tree Input
   */
  ObOpSpec *root_spec = NULL;
  const ObPhysicalPlan *phy_plan = NULL;
  share::ObLSArray ls_list;
  
  if (OB_ISNULL(root_spec = job.get_root_spec())) {
    ret = OB_NOT_INIT;
    LOG_WARN("root spec not init", K(ret));
  } else if (OB_UNLIKELY(NULL == (phy_plan = root_spec->get_phy_plan()))) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical plan is NULL", K(ret));
  } else if (OB_FAIL(build_task_op_input(query_ctx, task_info, *root_spec))) {
    LOG_WARN("fail build op inputs", K(ret));
  } else if (OB_FAIL(DAS_CTX(query_ctx).get_all_lsid(ls_list))) {
    LOG_WARN("get ls ids failed.", K(ret));
  } else if (OB_FAIL(query_ctx.get_my_session()->get_trans_result().add_touched_ls(ls_list))) {
    LOG_WARN("add touched ls failed.", K(ret));
  } else {
    const ObTaskInfo::ObRangeLocation &range_loc = task_info.get_range_location();
    for (int64_t i = 0; OB_SUCC(ret) && i < range_loc.part_locs_.count(); ++i) {
      if (OB_FAIL(task.assign_ranges(range_loc.part_locs_.at(i).scan_ranges_))) {
        LOG_WARN("assign range failed", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      task.set_ctrl_server(job.get_ob_job_id().get_server());
      task.set_runner_server(task_info.get_task_location().get_server());
      task.set_ob_task_id(task_info.get_task_location().get_ob_task_id());
      task.set_serialize_param(&query_ctx, root_spec, phy_plan);
      task.set_sql_string(ObSqlInfoGuard::get_tl_sql_info().sql_string_);
    }
  }
  return ret;
}

int ObRemoteTaskExecutor::handle_tx_after_rpc(ObScanner *scanner,
                                              ObSQLSessionInfo *session,
                                              const bool has_sent_task,
                                              const bool has_transfer_err,
                                              const ObPhysicalPlan *phy_plan,
                                              ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  auto tx_desc = session->get_tx_desc();
  bool remote_trans =
    session->get_local_autocommit() && !session->has_explicit_start_trans();
  if (remote_trans) {
  } else if (OB_ISNULL(tx_desc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dml need acquire transaction", K(ret), KPC(session));
  } else if (phy_plan->is_stmt_modify_trans() && has_sent_task) {
    if (has_transfer_err) {
    } else if (OB_ISNULL(scanner)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("task result is NULL", K(ret));
    } else if (OB_FAIL(MTL(transaction::ObTransService*)
                       ->add_tx_exec_result(*tx_desc,
                                            scanner->get_trans_result()))) {
      LOG_WARN("fail to report tx result", K(ret),
                 "scanner_trans_result", scanner->get_trans_result(),
               K(tx_desc));
    } else {
      LOG_TRACE("report tx result",
                "scanner_trans_result", scanner->get_trans_result(),
                K(tx_desc));
    }
    if (has_transfer_err || OB_FAIL(ret)) {
      if (exec_ctx.use_remote_sql()) {
        // ignore ret
        LOG_WARN("remote execute use sql fail with transfer_error, tx will rollback", K(ret));
        session->get_trans_result().set_incomplete();
      } else {
        ObDASCtx &das_ctx = DAS_CTX(exec_ctx);
        share::ObLSArray ls_ids;
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(das_ctx.get_all_lsid(ls_ids))) {
          LOG_WARN("get all ls_ids failed", K(tmp_ret));
        } else if (OB_TMP_FAIL(session->get_trans_result().add_touched_ls(ls_ids))) {
          LOG_WARN("add touched ls to txn failed", K(tmp_ret));
        } else {
         LOG_INFO("add touched ls succ", K(ls_ids));
        }
        if (OB_TMP_FAIL(tmp_ret)) {
          LOG_WARN("remote execute use plan fail with transfer_error and try add touched ls failed, tx will rollback", K(tmp_ret));
          session->get_trans_result().set_incomplete();
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
  }
  return ret;
}
