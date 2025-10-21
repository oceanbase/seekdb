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

#include "sql/executor/ob_direct_receive_op.h"
#include "sql/engine/ob_exec_context.h"
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
OB_SERIALIZE_MEMBER((ObDirectReceiveSpec, ObOpSpec), dynamic_const_exprs_);

ObDirectReceiveOp::ObDirectReceiveOp(ObExecContext &exec_ctx,
                                     const ObOpSpec &spec,
                                     ObOpInput *input)
    : ObReceiveOp(exec_ctx, spec, input),
      scanner_(NULL),
      scanner_iter_(),
      all_data_empty_(false),
      cur_data_empty_(true),
      first_request_received_(false),
      found_rows_(0)
{
}

int ObDirectReceiveOp::inner_open()
{
  int ret = OB_SUCCESS;
  all_data_empty_ = false; /* Whether all scanner data has been read */
  cur_data_empty_ = true;/* Whether the current scanner data has been fully read */
  first_request_received_ = false; /* Whether the plan has already been sent to the remote */
  // Receive the first scanner, for insert, update etc. DML, need to get affected_rows etc. at this time
  if (OB_FAIL(setup_next_scanner())) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      LOG_WARN("failed to setup first scanner", K(ret));
    } else {
      all_data_empty_ = true; /* No more scanners available */
    }
  } else {
    cur_data_empty_ = false; /* scanner is refilled, can continue reading */
  }
  return ret;
}
/*
 * State machine: cur_data_empty_, all_data_empty_
 */
int ObDirectReceiveOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  bool has_got_a_row = false;
  RemoteExecuteStreamHandle *resp_handler = NULL;
  //Sometimes we need send user variables to remote end to complete query successfully
  //And, of course, we will get the new value for user variable.
  //Scanner contains the updated values.
  //so we update the user variables in terms of scanners here.
  if (OB_FAIL(ObTaskExecutorCtxUtil::get_stream_handler(ctx_, resp_handler))) {
    LOG_WARN("fail get task response handler", K(ret));
  } else if (OB_ISNULL(resp_handler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("resp_handler is NULL", K(ret));
  } else if (OB_FAIL(THIS_WORKER.check_status())) {
    LOG_WARN("check physical plan status failed", K(ret));
  } else if (OB_ERR_TASK_SKIPPED == resp_handler->get_result_code()) {
    // skip
    ret = OB_ITER_END;
    LOG_WARN("this remote task is skipped", K(ret));
  }

  /* Use state machine thinking to understand the code below will be easier */
  while (OB_SUCC(ret) && false == has_got_a_row) {
    if (all_data_empty_) { /* All data has been read */
      ret = OB_ITER_END;
    } else if (cur_data_empty_) { /* Current scanner read complete */
      /* Send RPC request and remotely return a Scanner */
      if (OB_FAIL(setup_next_scanner())) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("fail to setup next scanner", K(ret));
        } else {
          all_data_empty_ = true; /* No more scanners available */
        }
      } else {
        cur_data_empty_ = false; /* scanner is refilled, can continue reading */
      }
    } else { /* current scanner readable */
      if (OB_FAIL(get_next_row_from_cur_scanner())) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("fail to get next row from cur scanner", K(ret));
        } else {
          // Current scanner has finished reading
          cur_data_empty_ = true;
          ret = OB_SUCCESS; // set ret to OB_SUCCESS to continue the loop
        }
      } else {
        // Got a line of data, exit the loop
        has_got_a_row = true;
      }
    }
  }
  if (OB_ITER_END == ret) {
    ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
    plan_ctx->set_found_rows(found_rows_);
  }
  return ret;
}

int ObDirectReceiveOp::inner_close()
{
  int ret = OB_SUCCESS;
  RemoteExecuteStreamHandle *resp_handler = NULL;
  if (OB_FAIL(ObTaskExecutorCtxUtil::get_stream_handler(ctx_, resp_handler))) {
    LOG_WARN("fail get task response handler", K(ret));
  } else if (OB_ISNULL(resp_handler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("resp_handler is NULL", K(ret));
  } else {
    if (resp_handler->has_more()) {
      if (OB_FAIL(resp_handler->abort())) {
        LOG_WARN("fail to abort", K(ret));
      } else {
        ObSQLSessionInfo *session = ctx_.get_my_session();
        ObPhysicalPlanCtx *plan_ctx = ctx_.get_physical_plan_ctx();
        ObExecutorRpcImpl *rpc = NULL;
        if (OB_FAIL(ObTaskExecutorCtxUtil::get_task_executor_rpc(ctx_, rpc))) {
          LOG_WARN("get task executor rpc failed", K(ret));
        } else if (OB_ISNULL(session) || OB_ISNULL(plan_ctx) || OB_ISNULL(rpc)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("session or plan ctx or rpc is NULL", K(ret));
        } else {
          ObQueryRetryInfo retry_info;
          const int32_t group_id = OB_INVALID_ID == session->get_expect_group_id() ? 0 : session->get_expect_group_id();
          ObExecutorRpcCtx rpc_ctx(session->get_effective_tenant_id(),
              plan_ctx->get_timeout_timestamp(),
              ctx_.get_task_exec_ctx().get_min_cluster_version(),
              &retry_info,
              session,
              plan_ctx->is_plain_select_stmt(),
              group_id);
          int tmp_ret = rpc->task_kill(rpc_ctx, resp_handler->get_task_id(), resp_handler->get_dst_addr());
          if (OB_SUCCESS != tmp_ret) {
            LOG_WARN("kill task failed", K(tmp_ret),
                K(resp_handler->get_task_id()), K(resp_handler->get_dst_addr()));
          }
        }
      }
    } else {}
  }
  return ret;
}

int ObDirectReceiveOp::setup_next_scanner()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  RemoteExecuteStreamHandle *resp_handler = NULL;
  ObSQLSessionInfo *my_session = GET_MY_SESSION(ctx_);
  if (OB_FAIL(ObTaskExecutorCtxUtil::get_stream_handler(ctx_, resp_handler))) {
    LOG_WARN("fail get task response handler", K(ret));
  } else if (OB_ISNULL(resp_handler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("resp_handler is NULL", K(ret));
  }

  if (OB_SUCC(ret)) {
    /* First read data, the result has already been obtained when Scheduler calls task_submit() */
    if (!first_request_received_) {
      ObScanner *scanner = resp_handler->get_result();
      if (OB_ISNULL(scanner)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        // set last_insert_id no matter success or fail
        plan_ctx->set_last_insert_id_to_client(scanner->get_last_insert_id_to_client());
        plan_ctx->set_last_insert_id_session(scanner->get_last_insert_id_session());
        plan_ctx->set_last_insert_id_changed(scanner->get_last_insert_id_changed());
        int tmp_ret = OB_SUCCESS;
        ObExecStatCollector &collector = ctx_.get_exec_stat_collector();
        if (OB_SUCCESS != (tmp_ret = collector.add_raw_stat(scanner->get_extend_info()))) {
          LOG_WARN("fail to collected raw extend info in scanner", K(tmp_ret));
        }
        if (OB_FAIL(scanner->get_err_code())) {
          int add_ret = OB_SUCCESS;
          const char* err_msg = scanner->get_err_msg();
          // After FORWARD_USER_ERROR(ret, err_msg), if the length of err_msg is greater than 0,

          // Then the error message returned to the user is err_msg, otherwise the error message returned to the user is the default error message corresponding to ret.
          // Therefore you can directly write FORWARD_USER_ERROR(ret, err_msg).
          FORWARD_USER_ERROR(ret, err_msg);
          LOG_WARN("error occurring in the remote sql execution, "
                   "please use the current TRACE_ID to grep the original error message on the remote_addr.",
                   K(ret), "remote_addr", resp_handler->get_dst_addr());
        } else {
          scanner_ = scanner;
          first_request_received_ = true;
          // For INSERT, UPDATE, DELETE, the first returned Scanner contains affected row
          plan_ctx->set_affected_rows(scanner->get_affected_rows());
          found_rows_ += scanner->get_found_rows();
          if (OB_FAIL(scanner->get_datum_store().begin(scanner_iter_))) {
            LOG_WARN("fail to init datum store iter", K(ret));
          } else if (OB_FAIL(plan_ctx->set_row_matched_count(scanner->get_row_matched_count()))) {
            LOG_WARN("fail to set row matched count", K(ret), K(scanner->get_row_matched_count()));
          } else if (OB_FAIL(plan_ctx->set_row_duplicated_count(
                      scanner->get_row_duplicated_count()))) {
            LOG_WARN("fail to set row duplicate count",
                     K(ret), K(scanner->get_row_duplicated_count()));
            /**
             * ObRemoteTaskExecutor::execute() has called merge_result() before here, that is a
             * better place to call merge_result(), especially when any operation failed between
             * there and here.
             * see 
             */
          } else if (OB_FAIL(plan_ctx->merge_implicit_cursors(scanner->get_implicit_cursors()))) {
            LOG_WARN("merge implicit cursors failed", K(ret), K(scanner->get_implicit_cursors()));
          }
        }
      }
    } else { /* Successive request, send SESSION_NEXT to remote via Handle */
      ObScanner *result_scanner = NULL;
      if (resp_handler->has_more()) {
        if (OB_FAIL(resp_handler->reset_and_init_result())) {
          LOG_WARN("fail reset and init result", K(ret));
        } else if (OB_ISNULL(result_scanner = resp_handler->get_result())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("succ to alloc result, but result scanner is NULL", K(ret));
        } else if (OB_FAIL(resp_handler->get_more(*result_scanner))) {
          ObCStringHelper helper;
          LOG_WARN("fail wait response",
                   K(ret), "dst_addr", helper.convert(resp_handler->get_dst_addr()));
        } else if (OB_FAIL(result_scanner->get_err_code())) {
          int add_ret = OB_SUCCESS;
          const char* err_msg = result_scanner->get_err_msg();
          FORWARD_USER_ERROR(ret, err_msg);
          ObCStringHelper helper;
          LOG_WARN("while getting more scanner, the remote rcode is not OB_SUCCESS",
                   K(ret), K(err_msg),
                   "dst_addr", helper.convert(resp_handler->get_dst_addr()));
        } else {
          scanner_ = result_scanner;
          found_rows_ += scanner_->get_found_rows();
          if (OB_FAIL(scanner_->get_datum_store().begin(scanner_iter_))) {
            LOG_WARN("fail to init datum store iter", K(ret));
          }
        }
      } else {
        ret = OB_ITER_END;
        plan_ctx->add_total_memstore_read_row_count(scanner_->get_memstore_read_row_count());
        plan_ctx->add_total_ssstore_read_row_count(scanner_->get_ssstore_read_row_count());
        // only successful select affect last_insert_id
        // for select, last_insert_id may changed because last_insert_id(#) called
        // last_insert_id values should be the last row calling last_insert_id(#)
        plan_ctx->set_last_insert_id_session(scanner_->get_last_insert_id_session());
        plan_ctx->set_last_insert_id_changed(scanner_->get_last_insert_id_changed());
        int tmp_ret = OB_SUCCESS;
        ObExecStatCollector &collector = ctx_.get_exec_stat_collector();
        if (OB_SUCCESS != (tmp_ret = collector.add_raw_stat(scanner_->get_extend_info()))) {
          LOG_WARN("fail to collected raw extend info in scanner", K(tmp_ret));
        }
        if (OB_SUCCESS != (tmp_ret = plan_ctx->get_table_row_count_list()
                                             .assign(scanner_->get_table_row_counts()))) {
          LOG_WARN("fail to set table row count", K(ret),
                    K(scanner_->get_table_row_counts()));
        }
        LOG_DEBUG("remote table row counts", K(scanner_->get_table_row_counts()),
                                             K(plan_ctx->get_table_row_count_list()));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(my_session->replace_user_variables(ctx_, scanner_->get_session_var_map()))) {
      LOG_WARN("replace user variables failed", K(ret));
    }
  }

  return ret;
}

int ObDirectReceiveOp::get_next_row_from_cur_scanner()
{
  int ret = OB_SUCCESS;
  const ObChunkDatumStore::StoredRow *tmp_sr = NULL;
  if (OB_FAIL(scanner_iter_.get_next_row(tmp_sr))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("get next stored row failed", K(ret));
    }
  } else if (OB_ISNULL(tmp_sr) || (tmp_sr->cnt_ != MY_SPEC.output_.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (uint32_t i = 0; i < tmp_sr->cnt_; ++i) {
      if (MY_SPEC.output_.at(i)->is_static_const_) {
        continue;
      } else {
        MY_SPEC.output_.at(i)->locate_expr_datum(eval_ctx_) = tmp_sr->cells()[i];
        MY_SPEC.output_.at(i)->set_evaluated_projected(eval_ctx_);
      }
    }
    // deep copy dynamic const expr datum
    clear_dynamic_const_parent_flag();
    if (MY_SPEC.dynamic_const_exprs_.count() > 0) {
      for (int64_t i = 0; OB_SUCC(ret) && i < MY_SPEC.dynamic_const_exprs_.count(); i++) {
        ObExpr *expr = MY_SPEC.dynamic_const_exprs_.at(i);
        if (0 == expr->res_buf_off_) {
          // for compat 4.0, do nothing
        } else if (OB_FAIL(expr->deep_copy_self_datum(eval_ctx_))) {
          LOG_WARN("fail to deep copy datum", K(ret), K(eval_ctx_), K(*expr));
        }
      }
    }
    LOG_DEBUG("direct receive next row", "row", ROWEXPR2STR(eval_ctx_, MY_SPEC.output_));
  }
  return ret;
}

int ObDirectReceiveOp::inner_rescan()
{
  // Not support rescan operation for remote operator
  int ret = OB_NOT_SUPPORTED;
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "Distributed rescan");
  return ret;
}

}/* ns sql*/
}/* ns oceanbase */
