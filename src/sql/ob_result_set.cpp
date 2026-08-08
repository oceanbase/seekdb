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

#define USING_LOG_PREFIX SQL
#include "ob_result_set.h"
#include "data_plane/transaction/ob_tx_desc_access.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/resolver/dml/ob_del_upd_stmt.h"
#include "rpc/obmysql/ob_mysql_field.h"
#include "sql/engine/px/ob_px_admission.h"
#include "sql/engine/ob_physical_plan.h"
#include "src/sql/plan_cache/ob_plan_cache.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/monitor/show_trace/ob_show_trace.h"
#include "sql/engine/table/ob_table_scan_op.h"
#include "share/ob_ddl_checksum.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::storage;
using namespace oceanbase::transaction;

namespace oceanbase
{
namespace sql
{
namespace
{
int find_table_scan_table_id(const ObOpSpec *spec, uint64_t &table_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(spec) || OB_INVALID_ID != table_id) {
    // The first table scan determines the checksum identity.
  } else if (spec->is_table_scan()) {
    table_id = static_cast<const ObTableScanSpec *>(spec)->get_ref_table_id();
  } else {
    for (uint32_t i = 0; OB_SUCC(ret) && i < spec->get_child_cnt(); ++i) {
      if (OB_FAIL(SMART_CALL(find_table_scan_table_id(spec->get_child(i), table_id)))) {
        LOG_WARN("failed to find table scan in DDL plan", K(ret));
      }
    }
  }
  return ret;
}
}

int ObResultSet::clear_ddl_checksum(ObPhysicalPlan *physical_plan)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(physical_plan) || OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("DDL plan or SQL proxy is null",
             K(ret), KP(physical_plan), KP(GCTX.sql_proxy_));
  } else {
    uint64_t table_scan_table_id = OB_INVALID_ID;
    if (OB_FAIL(find_table_scan_table_id(
            physical_plan->get_root_op_spec(), table_scan_table_id))) {
      LOG_WARN("failed to get DDL table scan id", K(ret));
    } else if (OB_FAIL(ObDDLChecksumOperator::delete_checksum(
                   physical_plan->get_ddl_execution_id(),
                   table_scan_table_id,
                   physical_plan->get_ddl_table_id(),
                   physical_plan->get_ddl_task_id(),
                   *GCTX.sql_proxy_))) {
      LOG_WARN("failed to clear DDL checksum", K(ret));
    }
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase

ObPhysicalPlan *ObResultSet::get_physical_plan()
{
  return static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
}

void ObResultSet::reset_implicit_cursor_idx()
{
  if (get_exec_context().get_physical_plan_ctx() != nullptr) {
    get_exec_context().get_physical_plan_ctx()->set_cur_stmt_id(0);
  }
}

ObResultSet::~ObResultSet()
{
  // The execution context owns the explicit plan-cache dependency. Preserve it
  // before destroying the inline context because the cache-object guard still
  // needs it to release the guarded plan.
  ObPlanCache *pc = get_exec_context().get_plan_cache();
  // when ObExecContext is destroyed, it also depends on the physical plan, so need to ensure
  // that inner_exec_ctx_ is destroyed before cache_obj_guard_
  if (NULL != inner_exec_ctx_) {
    inner_exec_ctx_->~ObExecContext();
    inner_exec_ctx_ = NULL;
  }
  if (OB_NOT_NULL(pc)) {
    cache_obj_guard_.force_early_release(pc);
  }
  // Always called at the end of the ObResultSet destructor
  update_end_time();
  is_init_ = false;
}

int ObResultSet::open_cmd()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cmd_)) {
    LOG_ERROR("cmd and physical_plan both not init", K(stmt_type_));
    ret = common::OB_NOT_INIT;
  } else if (OB_FAIL(init_cmd_exec_context(get_exec_context()))) {
    LOG_WARN("fail init exec context", K(ret), K_(stmt_type));
  } else if (OB_FAIL(on_cmd_execute())) {
    LOG_WARN("fail start cmd trans", K(ret), K_(stmt_type));
  } else if (OB_FAIL(ObCmdExecutor::execute(get_exec_context(), *cmd_))) {
    SQL_LOG(WARN, "execute cmd failed", K(ret));
  }

  return ret;
}

OB_INLINE int ObResultSet::open_plan()
{
  int ret = OB_SUCCESS;
  //ObLimit *limit_opt = NULL;
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  if (OB_ISNULL(physical_plan_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid physical plan", K(physical_plan_));
  } else {
    has_top_limit_ = physical_plan_->has_top_limit();
    // PX admission is done in do_open_plan (right before execute_plan), not here.
    if (OB_SUCC(ret)) {
      if (THIS_WORKER.is_timeout()) {
        // packet may have stayed in the queue for too long, by here it has already timed out,
        // If here is not checked, it will continue to run, likely entering other modules,
        // Other modules detect a timeout, causing confusion in other modules, therefore adding a timeout check here.
        // This is checked at this position to consider the timeout time in hint,
        // Because the hint timeout is set into THIS_WORKER inside the init_plan_exec_context function.
        ret = OB_TIMEOUT;
        LOG_WARN("query is timeout", K(ret),
                 "timeout_ts", THIS_WORKER.get_timeout_ts(),
                 "start_time", my_session_.get_query_start_time());
      } else if (stmt::T_PREPARE != stmt_type_) {
        int64_t retry = 0;
        if (OB_UNLIKELY(my_session_.is_zombie())) {
          //session has been killed some moment ago
          ret = OB_ERR_SESSION_INTERRUPTED;
          LOG_WARN("session has been killed", K(ret), K(my_session_.get_session_state()),
                  K(my_session_.get_server_sid()));
        } else {
          if (OB_SUCC(ret)) {
            do {
              ret = do_open_plan(get_exec_context());
            } while (transaction_set_violation_and_retry(ret, retry));
          }
        }
      }
    }
  }

  return ret;
}

int ObResultSet::open()
{
  int ret = OB_SUCCESS;
  my_session_.set_process_query_time(ObClockGenerator::getClock());
  LinkExecCtxGuard link_guard(my_session_, get_exec_context());
  ObTraceSpanGuard open_span(&my_session_, TRACE_OPEN);
  if (OB_FAIL(execute())) {
    LOG_WARN("execute plan failed", K(ret));
  } else if (OB_FAIL(open_result())) {
    LOG_WARN("open result set failed", K(ret));
  }

  if (OB_SUCC(ret)) {
    ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
    if (OB_NOT_NULL(cmd_)) {
      // cmd not set
    } else if (ret != OB_NOT_INIT &&
        OB_ISNULL(physical_plan_)) {
      LOG_WARN("empty physical plan", K(ret));
    } else if (OB_FAIL(ret)) {
      physical_plan_->set_is_last_exec_succ(false);
    } else {
      physical_plan_->set_is_last_exec_succ(true);
    }
  }

  return ret;
}

int ObResultSet::execute()
{
  int ret = common::OB_SUCCESS;
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
//  if (stmt::T_PREPARE != stmt_type_
//      && stmt::T_DEALLOCATE != stmt_type_) {
    if (NULL != physical_plan_) {
      ret = open_plan();
    } else if (NULL != cmd_) {
      ret = open_cmd();
    } else {
      // inner sql executor, no plan or cmd. do nothing.
    }
//  } else {
//    //T_PREPARE//T_DEALLOCATE do nothing
//  }
  set_errcode(ret);

  return ret;
}

int ObResultSet::open_result()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  if (NULL != physical_plan_) {
    if (OB_ISNULL(exec_result_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("exec result is null", K(ret));
    } else if (OB_FAIL(exec_result_->open(get_exec_context()))) {
      if (OB_TRANSACTION_SET_VIOLATION != ret && OB_TRY_LOCK_ROW_CONFLICT != ret) {
        SQL_LOG(WARN, "fail open main query", K(ret));
      }
    } else if (OB_FAIL(drive_dml_query())) {
      LOG_WARN("fail to drive dml query", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (!is_inner_result_set_ && OB_FAIL(set_mysql_info())) {
      SQL_LOG(WARN, "fail to get mysql info", K(ret));
    } else if (NULL != get_exec_context().get_physical_plan_ctx()) {
      SQL_LOG(DEBUG, "get affected row", K(get_stmt_type()),
              K(get_exec_context().get_physical_plan_ctx()->get_affected_rows()));
        set_affected_rows(get_exec_context().get_physical_plan_ctx()->get_affected_rows());
    }
    if (OB_SUCC(ret) && get_stmt_type() == stmt::T_ANONYMOUS_BLOCK) {
      // Anonymous block execution reports one affected row.
      set_affected_rows(1);
    }
  }
  set_errcode(ret);
  return ret;
}

/* on_cmd_execute - prepare before start execute cmd
 *
 * some command implicit commit current transaction state,
 * include release savepoints
 */
int ObResultSet::on_cmd_execute()
{
  int ret = OB_SUCCESS;

  bool ac = true;
  if (OB_ISNULL(cmd_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid inner state", K(cmd_));
  } else if (cmd_->cause_implicit_commit()) {
    if (OB_FAIL(implicit_commit_before_cmd_execute(my_session_,
                                                   get_exec_context(),
                                                   cmd_->get_cmd_type()))) {
      LOG_WARN("failed to implicit commit before cmd execute", K(ret));
    }
  }
  return ret;
}

int ObResultSet::implicit_commit_before_cmd_execute(ObSQLSessionInfo &session_info,
                                                    ObExecContext &exec_ctx,
                                                    const int cmd_type)
{
  int ret = OB_SUCCESS;
  ret = ObSqlTransControl::end_trans_before_cmd_execute(session_info,
                                                        exec_ctx.get_need_disconnect_for_update(),
                                                        exec_ctx.get_trans_state(),
                                                        cmd_type);
  return ret;
}

// open transaction if need (eg. ac=1 DML)
int ObResultSet::start_stmt()
{
  NG_TRACE(sql_start_stmt_begin);
  int ret = OB_SUCCESS;
  ObPhysicalPlan* phy_plan = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(get_exec_context());
  if (OB_ISNULL(phy_plan) || OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid inner state", KP(phy_plan), KP(plan_ctx));
  } else if (OB_ISNULL(phy_plan->get_root_op_spec())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("root_op_spec of phy_plan is NULL", K(phy_plan), K(ret));
  } else {
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_LIKELY(phy_plan->is_need_trans())) {
      if (get_trans_state().is_start_stmt_executed()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid transaction state", K(get_trans_state().is_start_stmt_executed()));
      } else if (OB_FAIL(ObSqlTransControl::start_stmt(get_exec_context()))) {
        SQL_LOG(WARN, "fail to start stmt", K(ret),
                K(phy_plan->get_dependency_table()));
      } else {
        stmt::StmtType literal_stmt_type = literal_stmt_type_ != stmt::T_NONE ? literal_stmt_type_ : stmt_type_;
        my_session_.set_first_need_txn_stmt_type(literal_stmt_type);
      }
      get_trans_state().set_start_stmt_executed(OB_SUCC(ret));
    }
  }
  NG_TRACE(sql_start_stmt_end);
  return ret;
}

int ObResultSet::end_stmt(const bool is_rollback)
{
  int ret = OB_SUCCESS;
  NG_TRACE(start_end_stmt);
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(get_exec_context());
  if (OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid inner state", K(plan_ctx));
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (get_trans_state().is_start_stmt_executed()
      && get_trans_state().is_start_stmt_success()) {
    ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
    if (OB_ISNULL(physical_plan_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("invalid inner state", K(physical_plan_));
    } else if (physical_plan_->is_need_trans()) {
      if (OB_FAIL(ObSqlTransControl::end_stmt(get_exec_context(), is_rollback, is_will_retry_()))) {
        SQL_LOG(WARN, "fail to end stmt", K(ret), K(is_rollback));
      }
    }
    get_trans_state().clear_start_stmt_executed();
  } else {
    // do nothing
  }
  NG_TRACE(end_stmt);
  return ret;
}

//@notice: this interface can not be used in the interface of ObResultSet
//otherwise, will cause the exec context reference mistake in session info
//see the call reference in LinkExecCtxGuard
int ObResultSet::get_next_row(const common::ObNewRow *&row)
{
  LinkExecCtxGuard link_guard(my_session_, get_exec_context());
  return inner_get_next_row(row);
}

OB_INLINE int ObResultSet::inner_get_next_row(const common::ObNewRow *&row)
{
  int &ret = errcode_;
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  // last_exec_succ default values is true
  if (OB_LIKELY(NULL != physical_plan_)) { // take this branch more frequently
    if (OB_ISNULL(exec_result_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("exec result is null", K(ret));
    } else if (OB_FAIL(exec_result_->get_next_row(get_exec_context(), row))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next row from exec result failed", K(ret));
        // marked last execute status
        physical_plan_->set_is_last_exec_succ(false);
      }
    } else {
      return_rows_++;
    }
  } else if (NULL != cmd_) {
    if (is_pl_stmt(static_cast<stmt::StmtType>(cmd_->get_cmd_type()))) {
      if (return_rows_ > 0) {
        errcode_ = OB_ITER_END;
      } else {
        row = get_exec_context().get_output_row();
        return_rows_++;
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      _OB_LOG(ERROR, "should not call get_next_row in CMD SQL");
    }
  } else {
    _OB_LOG(ERROR, "phy_plan not init");
    ret = OB_NOT_INIT;
  }
  //Save the current execution state to determine whether to refresh location
  //and perform other necessary cleanup operations when the statement exits.
  DAS_CTX(get_exec_context()).save_cur_exec_status(ret);

  return ret;
}
// Trigger this error condition: A, B two SQLs, simultaneously modified some rows of data (modification content has an intersection).
// Microscopically, the modification operation needs to read out the rows that meet the conditions first, and then update them. When reading, a version number will be recorded,
// Update will check if the version number has changed. If it has changed, it indicates that the data was modified by others after reading but before writing.
// Update operation modified. At this point, if a forced update is performed, it will violate consistency. For example:
// tbl contains one row of data 0, hoping to make it 2 through two concurrent operations A and B:
// A: update tbl set a = a + 1;
// B: update tbl set a = a + 1;
// If B completes the entire update operation after A reads a but before A updates a, then B's update will be lost, and the final result will be a=1
//
// Solution: Check the version number before updating the data, if the version number is inconsistent, then throw OB_TRANSACTION_SET_VIOLATION
// Error, retried by SQL layer here to ensure consistent read and write version numbers.
//
// Note: Since this retry does not involve refreshing Schema or updating Location Cache, so there is no need for a full statement-level retry,
// Execute level retries are sufficient.
bool ObResultSet::transaction_set_violation_and_retry(int &err, int64_t &retry_times)
{
  bool bret = false;
  ObSqlCtx *sql_ctx = get_exec_context().get_sql_ctx();
  bool is_batched_stmt = false;
  if (sql_ctx != nullptr) {
    is_batched_stmt = sql_ctx->is_batch_params_execute();
  }
  if ((OB_SNAPSHOT_DISCARDED == err
       || OB_TRANSACTION_SET_VIOLATION == err)
      && retry_times < TRANSACTION_SET_VIOLATION_MAX_RETRY
      && !is_batched_stmt) {
    // batched stmt itself is an optimization, where cursor state cannot be maintained in the fast retry path, does not take the fast retry path
    ObTxIsolationLevel isolation = my_session_.get_tx_isolation();
    bool is_isolation_RR_or_SE = (isolation == ObTxIsolationLevel::RR
                                  || isolation == ObTxIsolationLevel::SERIAL);
    // bug#6361189  pass err to force rollback stmt in do_close_plan()
    if (OB_TRANSACTION_SET_VIOLATION == err && 0 == retry_times && !is_isolation_RR_or_SE) {
      // TSC error retry, only print WARN log on the first attempt
      LOG_WARN_RET(err, "transaction set consistency violation, will retry");
    }
    int ret = do_close_plan(err, get_exec_context());
    ObPhysicalPlanCtx *plan_ctx = get_exec_context().get_physical_plan_ctx();
    if (OB_NOT_NULL(plan_ctx)) {
      plan_ctx->reset_for_quick_retry();
    }
    if (OB_SUCCESS != ret) {
      // Note: If do_close fails, it will affect TSC conflict retry, leading to a situation where a retry should occur but actually does not.
      // Here if a bug occurs, then you need to check the close implementation of each involved Operator
      LOG_WARN("failed to close plan", K(err), K(ret));
    } else {
      // OB_SNAPSHOT_DISCARDED should not retry now, see:
      // 
      // so we remove this condition: OB_TRANSACTION_SET_VIOLATION == err
      if (/*OB_TRANSACTION_SET_VIOLATION == err &&*/ is_isolation_RR_or_SE) {
        // rewrite err in ObQueryRetryCtrl::test_and_save_retry_state().
        // err = OB_TRANS_CANNOT_SERIALIZE;
        bret = false;
      } else {
        ++retry_times;
        bret = true;
      }
    }
    LOG_DEBUG("transaction set consistency violation and retry",
              "retry", bret, K(retry_times), K(err));
  }
  return bret;
}

OB_INLINE int ObResultSet::do_open_plan(ObExecContext &ctx)
{
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  NG_TRACE_EXT(do_open_plan_begin, OB_ID(plan_id), physical_plan_->get_plan_id());
  int ret = OB_SUCCESS;
  ctx.reset_op_env();
  exec_result_ = &(ctx.get_sql_exec_ctx().get_execute_result());
  if (stmt::T_PREPARE != stmt_type_) {
    if (OB_FAIL(ctx.init_phy_op(physical_plan_->get_phy_operator_size()))) {
      LOG_WARN("fail init exec phy op ctx", K(ret));
    } else if (OB_FAIL(ctx.init_expr_op(physical_plan_->get_expr_operator_size()))) {
      LOG_WARN("fail init exec expr op ctx", K(ret));
    }
  }

  if (OB_SUCC(ret) && my_session_.get_ddl_info().is_ddl() && stmt::T_INSERT == get_stmt_type()) {
    if (OB_FAIL(clear_ddl_checksum(physical_plan_))) {
      LOG_WARN("fail to clear ddl checksum", K(ret));
    }
  }


  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(start_stmt())) {
    LOG_WARN("fail start stmt", K(ret));
  } else {
    /* Set exec_result_ to the executor's runtime environment for returning data */
    /* execute plan,
     * For local and distributed plans, all jobs except RootJob finish before execute_plan returns.
     * exec_result_ is responsible for executing the last Job: RootJob
     **/
    // Admit PX here after open-phase work, just before execute_plan,
    // so it is never held idle across pre-execution. Guard makes it idempotent across the open_plan
    // transaction_set_violation retry; no-ops for non-PX / dop==1 / EXPLAIN.
    if OB_FAIL(ret) {
    } else if (!ctx.get_admission_acquired()
               && OB_FAIL(ObPxAdmission::enter_query_admission(my_session_, ctx,
                                                               get_stmt_type(), *physical_plan_))) {
      LOG_WARN("fail to enter px admission", KR(ret));
    } else if (OB_FAIL(executor_.init(physical_plan_))) {
      SQL_LOG(WARN, "fail to init executor", K(ret), K(physical_plan_));
    } else if (OB_FAIL(executor_.execute_plan(ctx))) {
      SQL_LOG(WARN, "fail execute plan", K(ret));
    }
  }
  NG_TRACE(do_open_plan_end);
  return ret;
}

int ObResultSet::set_mysql_info()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = get_exec_context().get_physical_plan_ctx();
  int64_t pos = 0;
  if (OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_LOG(WARN, "fail to get physical plan ctx");
  } else if (stmt::T_UPDATE == get_stmt_type()) {
    int result_len = snprintf(message_ + pos, MSG_SIZE - pos, OB_UPDATE_MSG_FMT, plan_ctx->get_row_matched_count(),
                              plan_ctx->get_row_duplicated_count(), warning_count_);
    if (OB_UNLIKELY(result_len < 0) || OB_UNLIKELY(result_len >= MSG_SIZE - pos)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to snprintf to buff", K(ret));
    }
  } else if (stmt::T_REPLACE == get_stmt_type()
             || stmt::T_INSERT == get_stmt_type()) {
    if (plan_ctx->get_row_matched_count() <= 1) {
      //nothing to do
    } else {
      int result_len = snprintf(message_ + pos, MSG_SIZE - pos, OB_INSERT_MSG_FMT, plan_ctx->get_row_matched_count(),
                                plan_ctx->get_row_duplicated_count(), warning_count_);
      if (OB_UNLIKELY(result_len < 0) || OB_UNLIKELY(result_len >= MSG_SIZE - pos)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to snprintf to buff", K(ret));
      }
    }
  } else if (stmt::T_LOAD_DATA == get_stmt_type()) {
    int64_t warning_cnt = warning_count_;
    ObWarningBuffer *buffer = ob_get_tsi_warning_buffer();
    if (OB_NOT_NULL(buffer)) {
      warning_cnt = buffer->get_total_warning_count();
    }
    int result_len = snprintf(message_ + pos, MSG_SIZE - pos, OB_LOAD_DATA_MSG_FMT, 
                              plan_ctx->get_row_matched_count(), plan_ctx->get_row_deleted_count(),
                              plan_ctx->get_row_duplicated_count(), warning_cnt);
    if (OB_UNLIKELY(result_len < 0) || OB_UNLIKELY(result_len >= MSG_SIZE - pos)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to snprintf to buff", K(ret));
    }
  } else {
    //nothing to do
  }
  return ret;
}

OB_INLINE void ObResultSet::store_affected_rows(ObPhysicalPlanCtx &plan_ctx)
{
  int64_t affected_row = 0;
  if (!ObStmt::is_dml_stmt(get_stmt_type())
      && (!is_pl_stmt(get_stmt_type()))) {
    affected_row = 0;
  } else if (stmt::T_SELECT == get_stmt_type()) {
    affected_row = plan_ctx.get_affected_rows();
    if (0 == affected_row) {
      affected_row = -1;
    }
  } else {
    affected_row = get_affected_rows();
  }
  NG_TRACE_EXT(affected_rows, OB_ID(affected_rows), affected_row);
  my_session_.set_affected_rows(affected_row);
}

OB_INLINE void ObResultSet::store_found_rows(ObPhysicalPlanCtx &plan_ctx)
{
  int64_t rows = 1;
  // If it is an execute statement, then you need to determine the type of the corresponding prepare statement, if it is a select, then it will affect the value of found_rows;
  //to do by rongxuan.lc 20151127
  if (plan_ctx.is_affect_found_row()) {
    if (OB_UNLIKELY(stmt::T_EXPLAIN == get_stmt_type())) {
      rows = 0;
      my_session_.set_found_rows(rows);
    } else {
      int64_t found_rows = -1;
      found_rows = plan_ctx.get_found_rows();
      rows = found_rows == 0 ? return_rows_ : found_rows;
      my_session_.set_found_rows(rows);
      NG_TRACE_EXT(store_found_rows,
                   OB_ID(found_rows), found_rows,
                   OB_ID(return_rows), return_rows_);
    }
  }
  return;
}



int ObResultSet::update_last_insert_id_to_client()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = get_exec_context().get_physical_plan_ctx();
  if (OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_LOG(WARN, "get plan ctx is NULL", K(ret));
  } else {
    set_last_insert_id_to_client(plan_ctx->calc_last_insert_id_to_client());
  }
  return ret;
}

int ObResultSet::update_is_result_accurate()
{
  int ret = OB_SUCCESS;
  if (stmt::T_SELECT == stmt_type_) {
    ObPhysicalPlanCtx *plan_ctx = get_exec_context().get_physical_plan_ctx();
    bool old_is_result_accurate = true;
    if (OB_ISNULL(plan_ctx)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_LOG(WARN, "get plan ctx is NULL", K(ret));
    } else if (OB_FAIL(my_session_.get_is_result_accurate(old_is_result_accurate))) {
      SQL_LOG(WARN, "faile to get is_result_accurate", K(ret));
    } else {
      bool is_result_accurate = plan_ctx->is_result_accurate();
      SQL_LOG(DEBUG, "debug is_result_accurate for session", K(is_result_accurate),
              K(old_is_result_accurate), K(ret));
      if (is_result_accurate != old_is_result_accurate) {
        // FIXME @qianfu temporarily written as update_sys_variable function, to be implemented with an additional one that can accept ObSysVarClassType and
        // and set system variable statement follow the same logic function, call here
        if (OB_FAIL(my_session_.update_sys_variable(SYS_VAR_IS_RESULT_ACCURATE, is_result_accurate ? 1 : 0))) {
          LOG_WARN("fail to update result accurate", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObResultSet::store_last_insert_id(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  if (ObStmt::is_dml_stmt(stmt_type_)
      && OB_LIKELY(NULL != physical_plan_)
      && OB_LIKELY(!physical_plan_->is_affected_last_insert_id())) {
    //nothing to do
  } else {
    ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
    if (OB_ISNULL(plan_ctx)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_LOG(WARN, "get plan ctx is NULL", K(plan_ctx));
    } else {
      uint64_t last_insert_id_session = plan_ctx->calc_last_insert_id_session();
      SQL_LOG(DEBUG, "debug last_insert_id for session", K(last_insert_id_session), K(ret));
      SQL_LOG(DEBUG, "debug last_insert_id changed",  K(ret),
              "last_insert_id_changed", plan_ctx->get_last_insert_id_changed());

      if (plan_ctx->get_last_insert_id_changed()) {
        ObObj last_insert_id;
        last_insert_id.set_uint64(last_insert_id_session);
        // FIXME @qianfu temporarily written as update_sys_variable function, to be implemented with an ability to pass ObSysVarClassType and
        // and set system variable statement follow the same logic function, call here
        if (OB_FAIL(my_session_.update_sys_variable(SYS_VAR_LAST_INSERT_ID, last_insert_id))) {
          LOG_WARN("fail to update last_insert_id", K(ret));
        } else if (OB_FAIL(my_session_.update_sys_variable(SYS_VAR_IDENTITY, last_insert_id))) {
          LOG_WARN("succ update last_insert_id, but fail to update identity", K(ret));
        } else {
          NG_TRACE_EXT(last_insert_id, OB_ID(last_insert_id), last_insert_id_session);
        }
      }

      if (OB_SUCC(ret)) {
        // TODO when does observer should return last_insert_id to client?
        set_last_insert_id_to_client(plan_ctx->calc_last_insert_id_to_client());
        SQL_LOG(DEBUG, "zixu debug", K(ret),
                "last_insert_id_to_client", get_last_insert_id_to_client());
      }
    }
  }
  return ret;
}
bool ObResultSet::need_rollback(int ret, int errcode, bool is_error_ignored) const
{
  bool bret = false;
  if (OB_SUCCESS != ret // Current close execution statement error
      || (errcode != OB_SUCCESS && errcode != OB_ITER_END)  // errcode is the error deliberately saved during the open phase
      || is_error_ignored) {  // An error occurred, but it was ignored;
    bret = true;
  }
  return bret;
}

int ObResultSet::deal_feedback_info(ObPhysicalPlan *physical_plan, bool is_rollback, ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *physical_ctx = get_exec_context().get_physical_plan_ctx();
  if (ctx.get_feedback_info().is_valid() && physical_plan->get_logical_plan().is_valid()) {
    if (physical_ctx != nullptr && !is_rollback && physical_ctx->get_check_pdml_affected_rows()) {
      if (OB_FAIL(physical_plan->check_pdml_affected_rows(ctx))) {
        LOG_WARN("fail to check pdml affected_rows", K(ret));
      }
    }
    if (physical_plan->try_record_plan_info()) {
      if (OB_FAIL(SMART_CALL(physical_plan->set_feedback_info(ctx)))) {
        LOG_WARN("fail to set feed_back info", K(ret));
      } else {
        physical_plan->set_record_plan_info(false);
      }
    }
  }

  return ret;
}

OB_INLINE int ObResultSet::do_close_plan(int errcode, ObExecContext &ctx)
{
  int ret = common::OB_SUCCESS;
  int pret = OB_SUCCESS;
  int sret = OB_SUCCESS;
  NG_TRACE(close_plan_begin);
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  if (OB_LIKELY(NULL != physical_plan_)) {
    // executor_.close should be after exec_result_.close, because the main thread get_next_row will hold the lock,
    // exec_result_.close will release the lock, while executor_.close needs to acquire a write lock to delete this scheduler.
    // FIXME qianfu.zpf Here the exec_result_.close should be called only after the scheduling thread ends. The scheduling thread pushes the last one to the main thread
    // After task result, the main thread will continue, it's possible that the main thread calls exec_result_.close before the scheduling thread ends,
    // But since the scheduler thread does not use the variables released by exec_result_.close after pushing the last task result to the main thread,
    // Therefore, writing it this way for now is not a problem.
    // The logic here needs to be corrected later.

    int old_errcode = ctx.get_errcode();
    if (OB_SUCCESS == old_errcode) {
      // record error code generated in open-phase for after stmt trigger
      ctx.set_errcode(errcode);
    }
    if (OB_ISNULL(exec_result_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("exec result is null", K(ret));
    } else if (OB_FAIL(exec_result_->close(ctx))) {
      SQL_LOG(WARN, "fail close main query", K(ret));
    }
    // whether `close` is successful or not, restore ctx.errcode_
    ctx.set_errcode(old_errcode);
    // Notify all tasks of this plan to delete the corresponding intermediate results
    int close_ret = OB_SUCCESS;
    ObPhysicalPlanCtx *plan_ctx = NULL;
    if (OB_ISNULL(plan_ctx = get_exec_context().get_physical_plan_ctx())) {
      close_ret = OB_ERR_UNEXPECTED;
      LOG_WARN("physical plan ctx is null");
    } else if (OB_SUCCESS != (close_ret = executor_.close(ctx))) { // executor_.close will wait for the scheduling thread to finish before returning.
      SQL_LOG(WARN, "fail to close executor", K(ret), K(close_ret));
    }

    ObPxAdmission::exit_query_admission(my_session_, get_exec_context(), get_stmt_type(), *get_physical_plan());
//    // Must be called after executor_.execute_plan runs to call a series of functions on exec_result_.
//    if (OB_FAIL(exec_result_.close(ctx))) {
//      SQL_LOG(WARN, "fail close main query", K(ret));
//    }
    // Regardless of how, reset_op_ctx
    bool err_ignored = false;
    if (OB_ISNULL(plan_ctx)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("plan is not NULL, but plan ctx is NULL", K(ret), K(errcode));
    } else {
      err_ignored = plan_ctx->is_error_ignored();
    }
    bool rollback = need_rollback(ret, errcode, err_ignored);
    get_exec_context().set_errcode(errcode);
    sret = end_stmt(rollback || OB_SUCCESS != pret);
    // SQL_LOG(INFO, "end_stmt err code", K_(errcode), K(ret), K(pret), K(sret));
    if (OB_FAIL(ret)) {
      // nop
    } else if (OB_SUCCESS != pret) {
      ret = pret;
    } else if (OB_SUCCESS != sret) {
      ret = sret;
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(deal_feedback_info(physical_plan_, rollback, ctx))) {
        LOG_WARN("fail to deal feedback info", K(ret));
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  // Regardless of how, reset executor_, otherwise it may report init twice when executor_.init is called previously
  executor_.reset();

  NG_TRACE(close_plan_end);
  return ret;
}

int ObResultSet::do_close(int *client_ret)
{
  int ret = OB_SUCCESS;
  LinkExecCtxGuard link_guard(my_session_, get_exec_context());

  ObTraceSpanGuard close_span(&my_session_, TRACE_CLOSE);
  const bool is_tx_active = my_session_.is_in_transaction();
  int do_close_plan_ret = OB_SUCCESS;
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  if (OB_LIKELY(NULL != physical_plan_)) {
    my_session_.set_last_plan_id(physical_plan_->get_plan_id());
    // Regardless of how, do_close_plan must be executed
    if (OB_UNLIKELY(OB_SUCCESS != (do_close_plan_ret = do_close_plan(errcode_,
                                                                     get_exec_context())))) {
      SQL_LOG(WARN, "fail close main query", K(ret), K(do_close_plan_ret));
    }
    if (OB_SUCC(ret)) {
      ret = do_close_plan_ret;
    }
  } else if (NULL != cmd_) {
    ret = OB_SUCCESS; // cmd mode always return true in close phase
  } else {
    // inner sql executor, no plan or cmd. do nothing.
  }
  // open, get_next_row regardless of success, close must always be called
  // The following logic ensures that the error code output externally is the first one that appears. by xiaochu.yh
  // Save the error code before close, used to determine if this stmt needs to be rolled back; by rongxuan.lc
  if (OB_SUCCESS != errcode_ && OB_ITER_END != errcode_) {
    ret = errcode_;
  }
  if (OB_SUCC(ret)) {
    ObPhysicalPlanCtx *plan_ctx = get_exec_context().get_physical_plan_ctx();
    if (OB_ISNULL(plan_ctx)) {
      ret = OB_NOT_INIT;
      LOG_WARN("result set isn't init", K(ret));
    } else {
      store_affected_rows(*plan_ctx);
      store_found_rows(*plan_ctx);
    }
  }

  if (OB_SUCC(ret) && get_stmt_type() == stmt::T_SELECT) {
    if (OB_FAIL(update_is_result_accurate())) {
      SQL_LOG(WARN, "failed to update is result_accurate", K(ret));
    }
  }
  // set last_insert_id
  int ins_ret = OB_SUCCESS;
  if (OB_SUCCESS != ret
      && get_stmt_type() != stmt::T_INSERT
      && get_stmt_type() != stmt::T_REPLACE) {
    // ignore when OB_SUCCESS != ret and stmt like select/update/delete... executed
  } else if (OB_SUCCESS != (ins_ret = store_last_insert_id(get_exec_context()))) {
      SQL_LOG(WARN, "failed to store last_insert_id", K(ret), K(ins_ret));
  }
  if (OB_SUCC(ret)) {
    ret = ins_ret;
  }

  int prev_ret = ret;
  bool async = false; // for debug purpose
  if (OB_NOT_NULL(physical_plan_)) {
    if (is_end_trans_async()) {
      ObCurTraceId::TraceId *cur_trace_id = NULL;
      if (OB_ISNULL(cur_trace_id = ObCurTraceId::get_trace_id())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("current trace id is NULL", K(ret));
        set_end_trans_async(false);
      } else {
        observer::ObSqlEndTransCb &sql_end_cb = my_session_.get_mysql_end_trans_cb();
        ObEndTransCbPacketParam pkt_param;
        int fill_ret = OB_SUCCESS;
        fill_ret = sql_end_cb.set_packet_param(pkt_param.fill(*this, my_session_, *cur_trace_id));
        if (OB_SUCCESS != fill_ret) {
          LOG_WARN("fail set packet param", K(ret));
          set_end_trans_async(false);
        }
      }
    }
    ret = auto_end_plan_trans(*physical_plan_, ret, is_tx_active, async);
  }

  if (is_user_sql_ && my_session_.need_reset_package()) {
    // need_reset_package is set, it must be reset package, wether exec succ or not.
    int tmp_ret = my_session_.reset_all_package_state_by_dbms_session();
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("reset all package fail. ", K(tmp_ret), K(ret));
      ret = OB_SUCCESS == ret ? tmp_ret : ret;
    }
  }
  // notify close fail to listener
  int err = OB_SUCCESS != do_close_plan_ret ? do_close_plan_ret : ret;
  if (client_ret != NULL
      && OB_SUCCESS != err && err != errcode_ && close_fail_cb_.is_valid()) {
    close_fail_cb_(err, *client_ret);
  }
  //Save the current execution state to determine whether to refresh location
  //and perform other necessary cleanup operations when the statement exits.
  DAS_CTX(get_exec_context()).save_cur_exec_status(ret);
  //NG_TRACE_EXT(result_set_close, OB_ID(ret), ret, OB_ID(arg1), prev_ret,
               //OB_ID(arg2), ins_ret, OB_ID(arg3), errcode_, OB_ID(async), async);
  return ret;  // All subsequent operations are completed through callback
}
// Asynchronous call end_trans
// 1. Only for phy plan with AC=1, not for cmd (except unimplemented commit/rollback)
// 2. TODO: For commit/rollback this cmd, behind also needs to go through this path. Now it still goes through synchronous Callback.
OB_INLINE int ObResultSet::auto_end_plan_trans(ObPhysicalPlan& plan,
                                               int ret,
                                               bool is_tx_active,
                                               bool &async)
{
  NG_TRACE(auto_end_plan_begin);
  bool in_trans = my_session_.is_in_transaction();
  bool ac = true;
  bool explicit_trans = my_session_.has_explicit_start_trans();
  bool is_rollback = false;
  my_session_.get_autocommit(ac);
  async = false;
  LOG_DEBUG("auto_end_plan_trans.start", K(ret),
            K(in_trans), K(ac), K(explicit_trans),
            K(is_async_end_trans_submitted()));
  // explicit start trans will disable auto-commit
  if (!explicit_trans && ac) {
    // Query like `select 1` will keep next scope set transaction xxx valid
    // for example:
    // set session transaction read only;
    // set @@session.autocommit=1;
    // set transaction read write;
    // select 1;
    // insert into t values(1); -- this will be success
    //
    // so, can not reset these transaction variables
    //
    // must always commit/rollback the transactional state in `ObTxDesc`
    // for example:
    // set session transaction isolation level SERIALIZABLE
    // -- UDF with: select count(1) from t1;
    // select UDF1() from dual; -- PL will remain transctional state after run UDF1
    //
    // after execute UDF1, snapshot is kept in ObTxDesc, must cleanup before run
    // other Query
    bool reset_tx_variable = plan.is_need_trans();
    ObPhysicalPlanCtx *plan_ctx = NULL;
    if (OB_ISNULL(plan_ctx = get_exec_context().get_physical_plan_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("physical plan ctx is null, won't call end trans!!!", K(ret));
    } else {
      //bool is_rollback = (OB_FAIL(ret) || plan_ctx->is_force_rollback());
      is_rollback = need_rollback(OB_SUCCESS, ret, plan_ctx->is_error_ignored());
      bool lock_conflict_skip_end_trans = false;
      // if err is lock conflict retry, do not rollback transaction, but cleanup transaction
      // state, keep transaction id unchanged, easy for deadlock detection and $OB_LOCKS view
      if (is_rollback && ret == OB_TRY_LOCK_ROW_CONFLICT && is_will_retry_()) {
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(ObSqlTransControl::reset_trans_for_autocommit_lock_conflict(get_exec_context()))) {
          LOG_WARN("cleanup trans state for lock conflict fail, fallback to end trans", K(tmp_ret));
        } else {
          lock_conflict_skip_end_trans = true;
        }
      }
      if (lock_conflict_skip_end_trans) {
      } else if (OB_LIKELY(false == is_end_trans_async()) || OB_LIKELY(false == is_user_sql_)) {
        // For UPDATE and other asynchronously submitted statements, if retries are needed, then the rollback in between also goes through the synchronous interface
        // If end_trans_cb is not set, use the synchronous interface. This is mainly provided for InnerSQL.
        // Because InnerSQL did not go through the Obmp_query interface, but directly operated on ResultSet
        int save_ret = ret;
        if (OB_FAIL(ObSqlTransControl::implicit_end_trans(get_exec_context(),
                                                          is_rollback,
                                                          NULL,
                                                          reset_tx_variable))) {
          if (OB_REPLICA_NOT_READABLE != ret) {
              LOG_WARN("sync end trans callback return an error!", K(ret),
                       K(is_rollback), "tx_desc",
                       data_plane::ObTxDescLogView(my_session_.get_tx_desc()));
            }
        }
        ret = OB_SUCCESS != save_ret? save_ret : ret;
        async = false;
      } else {
        int save_ret = ret;
        my_session_.get_end_trans_cb().set_last_error(ret);
        ret = ObSqlTransControl::implicit_end_trans(get_exec_context(),
                                                    is_rollback,
                                                    &my_session_.get_end_trans_cb(),
                                                    reset_tx_variable);
        // NOTE: async callback client will not issued if:
        // 1) it is a rollback, which will succeed immediately
        // 2) the commit submit/starting failed, in this case
        //    the connection will be released
        // so the error code should not override, and return to plan-driver
        // to decide whether a packet should be sent to client
        ret = save_ret == OB_SUCCESS ? ret : save_ret;
        async = true;
      }
      get_trans_state().clear_start_trans_executed();
    }
  }
  NG_TRACE(auto_end_plan_end);
  LOG_DEBUG("auto_end_plan_trans.end", K(ret),
            K(in_trans), K(ac), K(explicit_trans), K(plan.is_need_trans()),
            K(is_rollback),  K(async),
            K(is_async_end_trans_submitted()));
  return ret;
}


int ObResultSet::from_plan(const ObPhysicalPlan &phy_plan, const ObIArray<ObPCParam *> &raw_params)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  ObSQLSessionInfo *session_info = NULL;
  if (OB_ISNULL(plan_ctx = get_exec_context().get_physical_plan_ctx()) ||
      OB_ISNULL(session_info = get_exec_context().get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("physical plan ctx is null or ref handle is invalid",
             K(ret), K(plan_ctx));
  } else if (phy_plan.contain_paramed_column_field()
             && OB_FAIL(copy_field_columns(phy_plan))) {
    // Because it will modify cname_ in ObField, so here we deep copy field_columns_ in the plan
    LOG_WARN("failed to copy field columns", K(ret));
  } else if (phy_plan.contain_paramed_column_field()
             && OB_FAIL(construct_field_name(raw_params, false, *session_info))) {
    LOG_WARN("failed to construct field name", K(ret));
  } else {
    int64_t ps_param_count = plan_ctx->get_orig_question_mark_cnt();
    p_field_columns_ = phy_plan.contain_paramed_column_field()
                                  ? &field_columns_
                                  : &phy_plan.get_field_columns();
    stmt_type_ = phy_plan.get_stmt_type();
    literal_stmt_type_ = phy_plan.get_literal_stmt_type();
    plan_ctx->set_is_affect_found_row(phy_plan.is_affect_found_row());
    if (is_ps_protocol() && ps_param_count != phy_plan.get_param_fields().count()) {
      if (OB_FAIL(reserve_param_columns(ps_param_count))) {
        LOG_WARN("reserve param columns failed", K(ret), K(ps_param_count));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < ps_param_count; ++i) {
        ObField param_field;
        param_field.type_.set_type(ObIntType); // @bug
        param_field.cname_ = ObString::make_string("?");
        OZ (add_param_column(param_field), K(param_field), K(i), K(ps_param_count));
      }
      LOG_DEBUG("reset param count ", K(ps_param_count), K(plan_ctx->get_orig_question_mark_cnt()),
        K(phy_plan.get_param_fields().count()));
    } else {
      p_param_columns_ = &phy_plan.get_param_fields();
    }
  }
  return ret;
}

int ObResultSet::to_plan(const PlanCacheMode mode, ObPhysicalPlan *phy_plan)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(phy_plan)) {
    LOG_ERROR("invalid argument", K(phy_plan));
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_FAIL(phy_plan->set_field_columns(field_columns_))) {
      LOG_WARN("Failed to copy field info to plan", K(ret));
    } else if ((PC_PS_MODE == mode || PC_PL_MODE == mode)
               && OB_FAIL(phy_plan->set_param_fields(param_columns_))) {
      // param fields is only needed ps mode
      LOG_WARN("failed to copy param field to plan", K(ret));
    }
  }

  return ret;
}

int ObResultSet::get_read_consistency(ObConsistencyLevel &consistency)
{
  consistency = INVALID_CONSISTENCY;
  int ret = OB_SUCCESS;
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  if (OB_ISNULL(physical_plan_)
      || OB_ISNULL(exec_ctx_)
      || OB_ISNULL(exec_ctx_->get_sql_ctx())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("physical_plan", K_(physical_plan), K(exec_ctx_->get_sql_ctx()), K(ret));
  } else {
    const ObPhyPlanHint &phy_hint = physical_plan_->get_phy_plan_hint();
    if (stmt::T_SELECT == stmt_type_) { // select has weak
      if (OB_UNLIKELY(phy_hint.read_consistency_ != INVALID_CONSISTENCY)) {
        consistency = phy_hint.read_consistency_;
      } else {
        consistency = my_session_.get_consistency_level();
      }
    } else {
      consistency = STRONG;
    }
  }
  return ret;
}

int ObResultSet::init_cmd_exec_context(ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(exec_ctx);
  void *buf = NULL;
  if (OB_ISNULL(cmd_) || OB_ISNULL(plan_ctx)) {
    ret = OB_NOT_INIT;
    LOG_WARN("cmd or ctx is NULL", K(ret), K(cmd_), K(plan_ctx));
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(buf = get_mem_pool().alloc(sizeof(ObNewRow)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(sizeof(ObNewRow)), K(ret));
  } else {
    exec_ctx.set_output_row(new(buf)ObNewRow());
    exec_ctx.set_field_columns(&field_columns_);
    int64_t plan_timeout = 0;
    if (OB_FAIL(my_session_.get_query_timeout(plan_timeout))) {
      LOG_WARN("fail to get query timeout", K(ret));
    } else {
      int64_t start_time = my_session_.get_query_start_time();
      plan_ctx->set_timeout_timestamp(start_time + plan_timeout);
      THIS_WORKER.set_timeout_ts(plan_ctx->get_timeout_timestamp());
    }
  }
  return ret;
}
// obmp_query before retrying the entire SQL, it may be necessary to call this interface to refresh Location, to avoid always sending to the wrong server
void ObResultSet::refresh_location_cache_by_errno(bool is_nonblock, int err)
{
  DAS_CTX(get_exec_context()).refresh_location_cache_by_errno(is_nonblock, err);
}

void ObResultSet::force_refresh_location_cache(bool is_nonblock, int err)
{
  DAS_CTX(get_exec_context()).force_refresh_location_cache(is_nonblock, err);
}
// Tell mysql whether to pass an EndTransCallback
bool ObResultSet::need_end_trans_callback() const
{
  int ret = OB_SUCCESS;
  bool need = false;
  ObPhysicalPlan* physical_plan_ = static_cast<ObPhysicalPlan*>(cache_obj_guard_.get_cache_obj());
  if (stmt::T_SELECT == get_stmt_type()) {
    // For the select statement, the callback is never taken, regardless of the transaction status
    need = false;
  } else if (stmt::T_END_TRANS == get_stmt_type()) {
    need = true;
  } else {
    bool explicit_start_trans = my_session_.has_explicit_start_trans();
    bool ac = true;
    if (OB_FAIL(my_session_.get_autocommit(ac))) {
      LOG_ERROR("fail to get autocommit", K(ret));
    } else {}
    if (OB_LIKELY(NULL != physical_plan_) && 
               OB_LIKELY(physical_plan_->is_need_trans())) {
      need = ObSqlTransUtil::plan_can_end_trans(ac, explicit_start_trans);
    }
  }
  return need;
}

int ObResultSet::ExternalRetrieveInfo::build_into_exprs(
        ObStmt &stmt, pl::ObPLBlockNS *ns, bool is_dynamic_sql)
{
  int ret = OB_SUCCESS;
  if (stmt.is_select_stmt()) {
    ObSelectIntoItem *into_item = (static_cast<ObSelectStmt&>(stmt)).get_select_into();
    if (OB_NOT_NULL(into_item)) {
      OZ (into_exprs_.assign(into_item->pl_vars_));
    }
    is_select_for_update_ = (static_cast<ObSelectStmt&>(stmt)).has_for_update();
    has_hidden_rowid_ = (static_cast<ObSelectStmt&>(stmt)).has_hidden_rowid();
    is_skip_locked_ = (static_cast<ObSelectStmt&>(stmt)).is_skip_locked();
  }

  return ret;
}


int ObResultSet::ExternalRetrieveInfo::recount_dynamic_param_info(
  common::ObIArray<ExternalParamInfo> &param_info)
{
  int ret = OB_SUCCESS;
  int64_t current_position = INT64_MAX;
  for (int64_t  i = 0; OB_SUCC(ret) && i < into_exprs_.count(); ++i) {
    ObRawExpr *into = into_exprs_.at(i);
    if (OB_NOT_NULL(into)
        && T_QUESTIONMARK == into->get_expr_type()
        && static_cast<ObConstRawExpr *>(into)->get_value().get_unknown() < current_position) {
      current_position = static_cast<ObConstRawExpr *>(into)->get_value().get_unknown();
    }
  }

  // sort
  ObSEArray<ObConstRawExpr *, 4> recount_params;
  for (int64_t i = 0;
       current_position != INT64_MAX && OB_SUCC(ret) && i < param_info.count(); ++i) {
    ObRawExpr *param = param_info.at(i).element<0>();
    if (OB_NOT_NULL(param)
        && T_QUESTIONMARK == param->get_expr_type()
        && static_cast<ObConstRawExpr *>(param)->get_value().get_unknown() > current_position) {
      OZ (recount_params.push_back(static_cast<ObConstRawExpr *>(param)));
    }
  }
  if (OB_SUCC(ret) && recount_params.count() >= 2) {
    lib::ob_sort(recount_params.begin(), recount_params.end(),
        [](ObConstRawExpr *a, ObConstRawExpr *b) {
          return a->get_value().get_unknown() < b->get_value().get_unknown();
        });
  }

  for (int64_t i = 0;
      current_position != INT64_MAX && OB_SUCC(ret) && i < recount_params.count(); ++i) {
    recount_params.at(i)->get_value().set_unknown(current_position);
    current_position ++;
  }
  return ret;
}

int ObResultSet::ExternalRetrieveInfo::build(
  ObStmt &stmt,
  ObSQLSessionInfo &session_info,
  pl::ObPLBlockNS *ns,
  bool is_dynamic_sql,
  common::ObIArray<ExternalParamInfo> &param_info)
{
  int ret = OB_SUCCESS;
  OZ (build_into_exprs(stmt, ns, is_dynamic_sql));
  CK (OB_NOT_NULL(session_info.get_cur_exec_ctx()));
  CK (OB_NOT_NULL(session_info.get_cur_exec_ctx()->get_sql_ctx()));
  if (stmt.is_dml_stmt()) {
  }
  if (OB_SUCC(ret)) {
    ObSchemaGetterGuard *schema_guard = NULL;
    if (OB_ISNULL(stmt.get_query_ctx()) ||
        OB_ISNULL(schema_guard = stmt.get_query_ctx()->sql_schema_guard_.get_schema_guard())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("query_ctx is null", K(ret));
    } else if (param_info.empty() && into_exprs_.empty()) {
      if (stmt.is_dml_stmt()) {
        OZ (ObSQLUtils::reconstruct_sql(allocator_, &stmt, stmt.get_query_ctx()->get_sql_stmt(),
                                        schema_guard, session_info.create_obj_print_params()));
      } else {
        // other stmt do not need reconstruct.
      }
    } else {
      if (is_dynamic_sql && !into_exprs_.empty()) {
        OZ (recount_dynamic_param_info(param_info));
      }
      OZ (ObSQLUtils::reconstruct_sql(allocator_, &stmt, stmt.get_query_ctx()->get_sql_stmt(),
                                      schema_guard, session_info.create_obj_print_params()));
      /*
       * The ? in stmt is numbered according to the order of expression resolution, which this order depends on during the prepare phase
       * However, the ? in route_sql needs to be numbered according to the index of the parameters in the symbol table, this numbering is relied upon by proxy when doing routing, so we need to change the value of QUESTIONMARK in stmt
       */
      int64_t cnt = 0;
      for (int64_t i = param_info.count() - 1; OB_SUCC(ret) && i >= 0; i--) {
        if (0 == param_info.at(i).element<2>()) {
          cnt++;
        } else {
          break;
        }
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
        OX (param_info.pop_back());
      }
      OX (external_params_.set_capacity(param_info.count()));
      for (int64_t i = 0; OB_SUCC(ret) && i < param_info.count(); ++i) {
        if (OB_ISNULL(param_info.at(i).element<0>()) || OB_ISNULL(param_info.at(i).element<1>())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("param expr is NULL", K(i), K(param_info.at(i).element<0>()), K(param_info.at(i).element<1>()), K(param_info.at(i).element<2>()), K(ret));
        } else if (OB_FAIL(external_params_.push_back(param_info.at(i).element<0>()))) {
          LOG_WARN("push back error", K(i), K(param_info.at(i).element<0>()), K(ret));
        } else if (T_QUESTIONMARK == param_info.at(i).element<0>()->get_expr_type()) {
          ObConstRawExpr *const_expr = static_cast<ObConstRawExpr *>(param_info.at(i).element<0>());
          if (OB_FAIL(param_info.at(i).element<1>()->get_value().apply(const_expr->get_value()))) {
            LOG_WARN("apply error", K(const_expr->get_value()), K(param_info.at(i).element<1>()->get_value()), K(ret));
          }
        } else {
          // If it is not a local variable of PL, the value of QUESTIONMARK needs to be changed to an invalid value to prevent the proxy from being confused
          param_info.at(i).element<1>()->get_value().set_unknown(OB_INVALID_INDEX);
          param_info.at(i).element<1>()->set_expr_obj_meta(
                              param_info.at(i).element<1>()->get_value().get_meta());
          if (stmt_sql_.empty()) {
            OZ (ob_write_string(allocator_, stmt.get_query_ctx()->get_sql_stmt(), stmt_sql_));
          }
        }
      }
      OZ (ObSQLUtils::reconstruct_sql(allocator_, &stmt, route_sql_,
                                      schema_guard, session_info.create_obj_print_params()));
    }
  }
  LOG_INFO("reconstruct sql:", K(stmt.get_query_ctx()->get_prepare_param_count()),
                               K(stmt.get_query_ctx()->get_sql_stmt()), K(route_sql_), K(ret));
  return ret;
}

int ObResultSet::drive_dml_query()
{
  // DML using PX may need to be driven proactively through get_next_row().
  int ret = OB_SUCCESS;
  if (get_physical_plan()->is_local_plan() && !get_physical_plan()->need_drive_dml_query_) {
    // Partial local DML may be driven while opening the operator.
  } else if (get_physical_plan()->need_drive_dml_query_) {
    const ObNewRow *row = nullptr;
    if (OB_LIKELY(OB_ITER_END == (ret = inner_get_next_row(row)))) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("do dml query failed", K(ret));
    }
  } else if (get_physical_plan()->is_use_px() &&
             !get_physical_plan()->is_use_pdml()) {
    // DML + PX situation, need to call get_next_row to drive the PX framework
    stmt::StmtType type = get_physical_plan()->get_stmt_type();
    if (type == stmt::T_INSERT ||
        type == stmt::T_DELETE ||
        type == stmt::T_UPDATE ||
        type == stmt::T_REPLACE) {
      const ObNewRow *row = nullptr;
      // Non-returning type PX+DML driver needs to proactively call get_next_row
      if (OB_LIKELY(OB_ITER_END == (ret = inner_get_next_row(row)))) {
        // Outer call has already processed the corresponding affected rows
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("do dml query failed", K(ret));
      }
    }
  } else if (get_physical_plan()->is_use_pdml()) {
    const ObNewRow *row = nullptr;
    // pdml+non-returning situation, need to call get_next_row to drive the PX framework
    if (OB_LIKELY(OB_ITER_END == (ret = inner_get_next_row(row)))) {
      // Outer call has already processed the corresponding affected rows
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("do pdml query failed", K(ret));
    }
  }

  return ret;
}

int ObResultSet::copy_field_columns(const ObPhysicalPlan &plan)
{
  int ret = OB_SUCCESS;
  field_columns_.reset();

  int64_t N = plan.get_field_columns().count();
  ObField field;
  if (N > 0 && OB_FAIL(field_columns_.reserve(N))) {
    LOG_WARN("failed to reserve field column array", K(ret), K(N));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {
    const ObField &ofield = plan.get_field_columns().at(i);
    if (OB_FAIL(field.deep_copy(ofield, &get_mem_pool()))) {
      LOG_WARN("deep copy field failed", K(ret));
    } else if (OB_FAIL(field_columns_.push_back(field))) {
      LOG_WARN("push back field column failed", K(ret));
    } else {
      LOG_DEBUG("success to copy field", K(field));
    }
  }
  return ret;
}

int ObResultSet::construct_field_name(const common::ObIArray<ObPCParam *> &raw_params,
                                      const bool is_first_parse,
                                      const ObSQLSessionInfo &session_info)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < field_columns_.count(); i++) {
    if (OB_FAIL(construct_display_field_name(field_columns_.at(i), 
                                             raw_params, 
                                             is_first_parse, 
                                             session_info))) {
      LOG_WARN("failed to construct display name", K(ret), K(field_columns_.at(i)));
    } else {
      // do nothing
    }
  }
  return ret;
}

int ObResultSet::construct_display_field_name(common::ObField &field,
                                              const ObIArray<ObPCParam *> &raw_params,
                                              const bool is_first_parse,
                                              const ObSQLSessionInfo &session_info)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  // mysql mode, select '\t\t\t\t aaa' will remove the leading escape characters and spaces, the final length cannot be greater than MAX_COLUMN_CHAR_LENGTH
  // So here we give some extra buffer to handle this situation
  // If there are too many escape characters, there will still be a problem
  int32_t buf_len = MAX_COLUMN_CHAR_LENGTH * 2;
  int32_t pos = 0;
  int32_t name_pos = 0;
  if (!field.is_paramed_select_item_ || NULL == field.paramed_ctx_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(field.is_paramed_select_item_), K(field.paramed_ctx_));
  } else if (0 == field.paramed_ctx_->paramed_cname_.length()) {
    // 1. Parameterized cname length is 0, indicating that column names are specified
    // 2. Specified an alias, the alias exists in cname_, use it directly
    // do nothing
  } else if (OB_ISNULL(buf = static_cast<char *>(get_mem_pool().alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", K(ret), K(buf_len));
  } else {
    LOG_DEBUG("construct display field name", K(field));
    #define PARAM_CTX field.paramed_ctx_
    for (int64_t i = 0; OB_SUCC(ret) && pos <= buf_len && i < PARAM_CTX->param_idxs_.count(); i++) {
      int64_t idx = PARAM_CTX->param_idxs_.at(i);
      if (idx >= raw_params.count()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid index", K(i), K(raw_params.count()));
      } else if (OB_ISNULL(raw_params.at(idx)) || OB_ISNULL(raw_params.at(idx)->node_)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(raw_params.at(idx)), K(raw_params.at(idx)->node_));
      } else {
        int32_t len = (int32_t)PARAM_CTX->param_str_offsets_.at(i) - name_pos;
        len = std::min(buf_len - pos, len);
        if (len > 0) {
          MEMCPY(buf + pos, PARAM_CTX->paramed_cname_.ptr() + name_pos, len);
        }
        name_pos = (int32_t)PARAM_CTX->param_str_offsets_.at(i) + 1; // skip '?'
        pos += len;

        if (is_first_parse && PARAM_CTX->neg_param_idxs_.has_member(idx) && pos < buf_len) {
          buf[pos++] = '-'; // insert `-` for negative value
        }
        int64_t copy_str_len = 0;
        const char *copy_str = NULL;

        // 1.
        // If there is a constant node obtained from lexical analysis with the is_copy_raw_text_ flag, then it is a constant with a prefix, for example
        // select date'2012-12-12' from dual, column displays as date'2012-12-12',
        // The raw_text of the constant node obtained from lexical analysis is date'2012-12-12',
        // So directly copy raw_text after the required normalization.
        // 2.
        // If esc_str_flag_ is marked, then the projection column is a constant string, its internal string needs to be escaped,
        // The str_value of constant nodes is an escaped string, ready to use,
        // ObResultSet::make_final_field_name handles display trimming for leading escape characters.
        // Quoted string field names keep their quotes.
        // For example: select '\'hello' from dual, column name displays 'hello'
        //                select '\thello' from dual, column name displays hello (removed the leading escape character)
        //              select 'hello' from dual, column name displays 'hello' (with quotes)
        //              select '''hello' from dual, column name displays ''hello''
        // 3.
        // mysql mode, the following for the following sql:
        // select 'a' 'abc' from dual
        // Behavior is a as column name, 'abc' as value, but parameterized sql is select ? from dual
        // In lexical node two strings are concat, as a whole it is treated as a varchar node, but this node has a T_CONCAT_STRING sub-node, the node's str_val saves the column name
        // So in this case, directly use the str_val of T_CONCAT_STRING as the column name
        if (1 == raw_params.at(idx)->node_->is_copy_raw_text_) {
          copy_str_len = raw_params.at(idx)->node_->text_len_;
          copy_str = raw_params.at(idx)->node_->raw_text_;
        } else if (PARAM_CTX->esc_str_flag_) {
          if (1 == raw_params.at(idx)->node_->num_child_) {
            LOG_DEBUG("concat str node");
            if (OB_ISNULL(raw_params.at(idx)->node_->children_)
                || OB_ISNULL(raw_params.at(idx)->node_->children_[0])
                || T_CONCAT_STRING != raw_params.at(idx)->node_->children_[0]->type_) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("invalid argument", K(ret));
            } else {
              copy_str_len = raw_params.at(idx)->node_->children_[0]->str_len_;
              copy_str = raw_params.at(idx)->node_->children_[0]->str_value_;
            }
          } else {
            copy_str_len = raw_params.at(idx)->node_->str_len_;
            copy_str = raw_params.at(idx)->node_->str_value_;
          }
        } else if (0 == field.paramed_ctx_->paramed_cname_.compare("?") &&
                   1 == PARAM_CTX->param_idxs_.count() &&
                   T_NULL == raw_params.at(idx)->node_->type_) {
          // MySQL sets the alias of standalone null value("\N","null"...) to "NULL" during projection.
          copy_str_len = strlen("NULL");
          copy_str = "NULL";
        } else {
          copy_str_len = raw_params.at(idx)->node_->text_len_;
          copy_str = raw_params.at(idx)->node_->raw_text_;
        }

        if (OB_SUCC(ret)) {
          len = std::min(buf_len - pos, (int32_t)copy_str_len);
          if (len > 0) {
            MEMCPY(buf + pos, copy_str, len);
          }

          pos += len;
        }
      }
    } // for end

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (name_pos < PARAM_CTX->paramed_cname_.length()) { // If there is a string after the last constant
      int32_t len = std::min((int32_t)PARAM_CTX->paramed_cname_.length() - name_pos, buf_len - pos);
      if (len > 0) {
        MEMCPY(buf + pos, PARAM_CTX->paramed_cname_.ptr() + name_pos, len);
      }
      pos += len;
    }

    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(make_final_field_name(buf, pos, field.cname_))) {
      LOG_WARN("failed to make final field name", K(ret));
    } else {
      // do nothing
    }
    #undef PARAM_CTX
  }
  return ret;
}


void ObResultSet::replace_lob_type(obmysql::ObMySQLField &mfield)
{
  // mysql mode
  // issue: 52728955, 52735855, 52731784, 52734963, 52729976
  // compat mysql .net driver 5.7, longblob, json, gis length is max u32
  if (mfield.type_ == obmysql::EMySQLFieldType::MYSQL_TYPE_LONG_BLOB
      || mfield.type_ == obmysql::EMySQLFieldType::MYSQL_TYPE_JSON
      || mfield.type_ == obmysql::EMySQLFieldType::MYSQL_TYPE_GEOMETRY) {
    mfield.length_ = UINT32_MAX;
  }

  if (mfield.type_ == obmysql::EMySQLFieldType::MYSQL_TYPE_TINY_BLOB ||
      mfield.type_ == obmysql::EMySQLFieldType::MYSQL_TYPE_MEDIUM_BLOB ||
      mfield.type_ == obmysql::EMySQLFieldType::MYSQL_TYPE_LONG_BLOB) {
    // compat mysql-jdbc
    // for 5.x, always return MYSQL_TYPE_BLOB
    // for 8.x always return MYSQL_TYPE_BLOB, and do text type judge in mysql-jdbc by length
    mfield.type_ = obmysql::EMySQLFieldType::MYSQL_TYPE_BLOB;
  } else if (mfield.type_ == obmysql::EMySQLFieldType::MYSQL_TYPE_JSON) {
    // for mysql 5.x json response as plain text not binary, but the charset always binary 
    mfield.charsetnr_ = common::CS_TYPE_BINARY;
  }
}

int ObResultSet::make_final_field_name(char *buf, int64_t len, common::ObString &field_name)
{
  int ret = OB_SUCCESS;
  ObCollationType cs_type = common::CS_TYPE_INVALID;
  if (OB_ISNULL(buf) || 0 == len) {
    field_name.assign(buf, static_cast<int32_t>(len));
  } else if (OB_FAIL(my_session_.get_collation_connection(cs_type))) {
    LOG_WARN("failed to get collation_connection", K(ret));
  } else {
    while (len && !ObCharset::is_graph(cs_type, *buf)) {
      buf++;
      len--;
    }
    len = len < MAX_COLUMN_CHAR_LENGTH ? len : MAX_COLUMN_CHAR_LENGTH;
    field_name.assign(buf, static_cast<int32_t>(len));
  }
  return ret;
}

uint64_t ObResultSet::get_field_cnt() const
{
  int64_t cnt = 0;
  uint64_t ret = 0;
  if (OB_ISNULL(get_field_columns())) {
    // ignore ret
    LOG_ERROR("unexpected error. field columns is null");
    right_to_die_or_duty_to_live();
  }
  cnt = get_field_columns()->count();
  if (cnt >= 0) {
    ret = static_cast<uint64_t>(cnt);
  }
  return ret;
}

bool ObResultSet::has_implicit_cursor() const
{
  bool bret = false;
  if (get_exec_context().get_physical_plan_ctx() != nullptr) {
    bret = !get_exec_context().get_physical_plan_ctx()->get_implicit_cursor_infos().empty();
  }
  return bret;
}

int ObResultSet::switch_implicit_cursor(int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  ObPhysicalPlanCtx *plan_ctx = get_exec_context().get_physical_plan_ctx();
  if (OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("plan_ctx is null", K(ret));
  } else if (OB_FAIL(plan_ctx->switch_implicit_cursor())) {
    if (OB_ITER_END != ret) {
      LOG_WARN("cursor_idx is invalid", K(ret));
    }
  } else {
    affected_rows = plan_ctx->get_affected_rows();
    set_last_insert_id_to_client(plan_ctx->get_last_insert_id_to_client());
    memset(message_, 0, sizeof(message_));
    if (OB_FAIL(set_mysql_info())) {
      LOG_WARN("set mysql info failed", K(ret));
    }
  }
  return ret;
}

bool ObResultSet::is_cursor_end() const
{
  bool bret = false;
  ObPhysicalPlanCtx *plan_ctx = get_exec_context().get_physical_plan_ctx();
  if (plan_ctx != nullptr) {
    bret = (plan_ctx->get_cur_stmt_id() >= plan_ctx->get_implicit_cursor_infos().count());
  }
  return bret;
}
