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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_px_rpc_processor.h"
#include "ob_px_sqc_handler.h"
#include "sql/executor/ob_executor_rpc_processor.h"
#include "sql/engine/px/ob_px_target_mgr.h"
#include "sql/engine/px/ob_px_sqc_handler.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

int ObInitSqcP::init()
{
  int ret = OB_SUCCESS;
  ObPxSqcHandler *sqc_handler = nullptr;
  if (OB_ISNULL(sqc_handler = ObPxSqcHandler::get_sqc_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected sqc handler", K(ret));
  } else if (OB_FAIL(sqc_handler->init())) {
    LOG_WARN("Failed to init sqc handler", K(ret));
    sqc_handler->reset();
    op_reclaim_free(sqc_handler);
  } else {
    arg_.sqc_handler_ = sqc_handler;
    arg_.sqc_handler_->reset_reference_count(); // Set sqc_handler reference count to 1.
  }
  return ret;
}

void ObInitSqcP::destroy()
{
  obrpc::ObRpcProcessor<obrpc::ObPxRpcProxy::ObRpc<obrpc::OB_PX_ASYNC_INIT_SQC> >::destroy();
  /**
   * If the after process flow has been undergone, arg_.sqc_handler_ will be set to null.
   * If arg_.sqc_handler_ is not null here, it means that the after process flow has not been
   * performed after init, so the release should be done by itself.
   */
  if (OB_NOT_NULL(arg_.sqc_handler_)) {
    int report_ret = OB_SUCCESS;
    ObPxSqcHandler::release_handler(arg_.sqc_handler_, report_ret);
  }
}

int ObInitSqcP::process()
{
  GET_DIAGNOSTIC_INFO->get_ash_stat().in_px_execution_ = true;
  int ret = OB_SUCCESS;
  LOG_TRACE("receive dfo", K_(arg));
  ObPxSqcHandler *sqc_handler = arg_.sqc_handler_;
  result_.sqc_order_gi_tasks_ = true;
  /**
   * As long as it can enter process, after process will definitely be called, so interruption can cover the entire
   * SQC lifecycle.
   */
  if (OB_NOT_NULL(sqc_handler)) {
    ObPxRpcInitSqcArgs &arg = sqc_handler->get_sqc_init_arg();
    if (OB_FAIL(SET_INTERRUPTABLE(arg.sqc_.get_interrupt_id().px_interrupt_id_))) {
      LOG_WARN("sqc failed to SET_INTERRUPTABLE");
    } else {
      unregister_interrupt_ = true;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(sqc_handler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Sqc handler can't be nullptr", K(ret));
  } else if (OB_FAIL(sqc_handler->init_env())) {
    LOG_WARN("Failed to init sqc env", K(ret));
  } else if (OB_FAIL(sqc_handler->pre_acquire_px_worker(result_.reserved_thread_count_))) {
    LOG_WARN("Failed to pre acquire px worker", K(ret));
  } else if (OB_FAIL(pre_setup_op_input(*sqc_handler))) {
    LOG_WARN("pre setup op input failed", K(ret));
  } else if (OB_FAIL(sqc_handler->thread_count_auto_scaling(result_.reserved_thread_count_))) {
    LOG_WARN("fail to do thread auto scaling", K(ret), K(result_.reserved_thread_count_));
  } else if (result_.reserved_thread_count_ <= 0) {
    ret = OB_ERR_INSUFFICIENT_PX_WORKER;
    ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(dop_, sqc_handler->get_phy_plan().get_px_dop());
    ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(required_px_workers_number_, 1);
    ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(admitted_px_workers_number_, result_.reserved_thread_count_);
    LOG_WARN("Worker thread res not enough", K_(result));
  } else if (OB_FAIL(sqc_handler->link_qc_sqc_channel())) {
    LOG_WARN("Failed to link qc sqc channel", K(ret));
  } else {
    /*do nothing*/
  }

#ifdef ERRSIM
  int ecode = EventTable::EN_PX_SQC_INIT_PROCESS_FAILED;
  if (OB_SUCCESS != ecode && OB_SUCC(ret)) {
    LOG_WARN("match sqc execute errism", K(ecode));
    ret = ecode;
  }
#endif

  if (OB_FAIL(ret) && OB_NOT_NULL(sqc_handler)) {
    if (unregister_interrupt_) {
      ObPxRpcInitSqcArgs &arg = sqc_handler->get_sqc_init_arg();
      UNSET_INTERRUPTABLE(arg.sqc_.get_interrupt_id().px_interrupt_id_);
      unregister_interrupt_ = false;
    }

    // 
    if (is_schema_error(ret)) {
      ObInterruptUtil::update_schema_error_code(&(sqc_handler->get_exec_ctx()), ret);
    }

    int report_ret = OB_SUCCESS;
    // DO NOT use sqc_handler after release_handler!!!
    ObPxSqcHandler::release_handler(sqc_handler, report_ret);
    arg_.sqc_handler_ = nullptr;
  }
  // Non-rpc framework error content set to response message
  // rpc framework error code returns OB_SUCCESS in process
  result_.rc_ = ret;
  // Asynchronous logic processing interface, always returns OB_SUCCESS, error codes during logic processing
  // Return through result_.rc_
  return OB_SUCCESS;
}

int ObInitSqcP::pre_setup_op_input(ObPxSqcHandler &sqc_handler)
{
  int ret = OB_SUCCESS;
  ObPxSubCoord &sub_coord = sqc_handler.get_sub_coord();
  ObExecContext *ctx = sqc_handler.get_sqc_init_arg().exec_ctx_;
  ObOpSpec *root = sqc_handler.get_sqc_init_arg().op_spec_root_;
  ObPxSqcMeta &sqc = sqc_handler.get_sqc_init_arg().sqc_;
  sub_coord.set_is_single_tsc_leaf_dfo(sqc.is_single_tsc_leaf_dfo());
  CK(OB_NOT_NULL(ctx) && OB_NOT_NULL(root));
  if (sqc.is_single_tsc_leaf_dfo() &&
      OB_FAIL(sub_coord.rebuild_sqc_access_table_locations())) {
    LOG_WARN("fail to rebuild sqc access location", K(ret));
  } else if (OB_FAIL(sub_coord.pre_setup_op_input(*ctx, *root, sub_coord.get_sqc_ctx(),
      sqc.get_access_table_locations(),
      sqc.get_access_table_location_keys()))) {
    LOG_WARN("pre_setup_op_input failed", K(ret));
  }
  return ret;
}

int ObInitSqcP::startup_normal_sqc(ObPxSqcHandler &sqc_handler)
{
  int ret = OB_SUCCESS;
  int64_t dispatched_worker_count = 0;
  ObSQLSessionInfo *session = sqc_handler.get_exec_ctx().get_my_session();
  ObPxSubCoord &sub_coord = sqc_handler.get_sub_coord();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else {
    ObPxRpcInitSqcArgs &arg = sqc_handler.get_sqc_init_arg();
    SQL_INFO_GUARD(arg.sqc_.get_monitoring_info().cur_sql_, session->get_cur_sql_id());
    ObWorkerSessionGuard worker_session_guard(session);
    ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
    session->set_current_trace_id(ObCurTraceId::get_trace_id());
    session->set_peer_addr(arg.sqc_.get_qc_addr());
    /*
       Sometimes, PX workers may evaluate temporary expressions which alter the execution context's
       frames, while other workers are still in the process of deserializing task args. This
       could lead to a crash, so we retain the original frames to assist with
       deserialization for PX.
    */
    arg.exec_ctx_->set_ori_frames(arg.exec_ctx_->get_frames());
    arg.exec_ctx_->set_ori_frame_cnt(arg.exec_ctx_->get_frame_cnt());
    arg.exec_ctx_->set_ori_expr_op_size(arg.exec_ctx_->get_expr_op_size());
    if (OB_FAIL(session->store_query_string(ObString::make_string("PX SUB COORDINATOR")))) {
      LOG_WARN("store query string to session failed", K(ret));
    } else if (OB_FAIL(sub_coord.pre_process())) {
      LOG_WARN("fail process sqc", K(arg), K(ret));
    } else if (OB_FAIL(sub_coord.try_start_tasks(dispatched_worker_count))) {
      /**
       * When starting some workers fails, we proactively interrupt the already started workers.
       * This operation is blocking, and after successful interruption, the sqc handler is released directly.
       */
      LOG_WARN("Notity all dispatched worker to exit", K(ret), K(dispatched_worker_count));
      sub_coord.notify_dispatched_task_exit(dispatched_worker_count);
      LOG_WARN("All dispatched worker exit", K(ret), K(dispatched_worker_count));
    } else {
      sqc_handler.get_notifier().wait_all_worker_start();
      /**
       * Check interrupt, if an interrupt has been received on our side, pass it to each worker to avoid interrupt loss.
       * Once the process flow ends, sqc may receive an interrupt, but at this point, the workers may not have registered for interrupts,
       * so this is why sqc needs to pass the interrupt to each worker.
       */
      sqc_handler.check_interrupt();
      sqc_handler.worker_end_hook();
    }
  }
  return ret;
}

int ObInitSqcP::after_process(int error_code)
{
  int ret = OB_SUCCESS;
  UNUSED(error_code);
  ObSQLSessionInfo *session = nullptr;
  ObPxSqcHandler *sqc_handler = arg_.sqc_handler_;
  bool no_need_startup_normal_sqc = (OB_SUCCESS != result_.rc_);
  if (no_need_startup_normal_sqc) {
    /**
     *  rc_ not equal to OB_SUCCESS, no longer proceed with the sqc process, directly release the sqc at the end.
     */
  } else if (OB_ISNULL(sqc_handler = arg_.sqc_handler_)
             || !sqc_handler->valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid sqc handler", K(ret), KPC(sqc_handler));
  } else if (OB_ISNULL(session = sqc_handler->get_exec_ctx().get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Session can't be null", K(ret));
  } else {
    lib::CompatModeGuard g(session->get_compatibility_mode() == ORACLE_MODE ?
        lib::Worker::CompatMode::ORACLE : lib::Worker::CompatMode::MYSQL);

    sqc_handler->set_tenant_id(sqc_handler->get_exec_ctx().get_my_session()->get_effective_tenant_id());
    ObPxRpcInitSqcArgs &arg = sqc_handler->get_sqc_init_arg();
    /**
     * Get the local thread according to arg_ parameter and execute task
     */
    LOG_TRACE("process dfo", K(arg), K(session->get_compatibility_mode()), K(sqc_handler->get_reserved_px_thread_count()));
    ret = startup_normal_sqc(*sqc_handler);
    session->set_session_sleep();
  }

  GET_DIAGNOSTIC_INFO->get_ash_stat().in_px_execution_ = false;
  /**
   * Here we need to clean up interrupts and release the allocated number of threads and handler.
   * After the worker starts normally, its reference count is updated to
   * the number of workers plus rpc, release_handler will subtract one reference count, and the last one to hold a reference count
   * will truly release the sqc handler.
   * The last worker thread needs to release memory;
   */
  if (!no_need_startup_normal_sqc) {
    if (unregister_interrupt_) {
      if (OB_ISNULL(sqc_handler = arg_.sqc_handler_)
          || !sqc_handler->valid()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Invalid sqc handler", K(ret), KPC(sqc_handler));
      } else {
        ObPxRpcInitSqcArgs &arg = sqc_handler->get_sqc_init_arg();
        UNSET_INTERRUPTABLE(arg.sqc_.get_interrupt_id().px_interrupt_id_);
      }
    }
    if (OB_NOT_NULL(sqc_handler) && OB_SUCCESS == sqc_handler->get_end_ret()) {
      sqc_handler->set_end_ret(ret);
    }
    int report_ret = OB_SUCCESS;
    ObPxSqcHandler::release_handler(sqc_handler, report_ret);
    arg_.sqc_handler_ = nullptr;
  }
  return ret;
}
// Already unused, to be removed later
int ObInitTaskP::init()
{
  return OB_NOT_SUPPORTED;
}

int ObInitTaskP::process()
{
  // According to arg_ parameter to get the local thread and execute task
  return OB_NOT_SUPPORTED;
}

int ObInitTaskP::after_process(int error_code)
{
  UNUSED(error_code);
  return OB_NOT_SUPPORTED;
}

void ObFastInitSqcReportQCMessageCall::operator()(hash::HashMapPair<ObInterruptibleTaskID,
      ObInterruptCheckerNode *> &entry)
{
  UNUSED(entry);
  if (OB_NOT_NULL(sqc_)) {
    if (sqc_->is_ignore_vtable_error() && err_ != OB_SUCCESS
        && ObVirtualTableErrorWhitelist::should_ignore_vtable_error(err_)) {
      // When this SQC is a virtual table query, the RPC scheduling failure needs to ignore the error result.
      // and mock a sqc finish msg sending to the PX operator that is polling messages
      // This operation has been confirmed to be thread-safe.
      mock_sqc_finish_msg();
    } else {
      sqc_->set_need_report(false);
      if (need_set_not_alive_) {
        sqc_->set_server_not_alive(true);
      }
    }
  }
}

int ObFastInitSqcReportQCMessageCall::mock_sqc_finish_msg()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(sqc_)) {
    dtl::ObDtlBasicChannel *ch = reinterpret_cast<dtl::ObDtlBasicChannel *>(
        sqc_->get_qc_channel());
    if (OB_ISNULL(ch)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ch is unexpected", K(ret));
    } else {
      MTL_SWITCH(ch->get_tenant_id()) {
        ObPxFinishSqcResultMsg finish_msg;
        finish_msg.rc_ = err_;
        finish_msg.dfo_id_ = sqc_->get_dfo_id();
        finish_msg.sqc_id_ = sqc_->get_sqc_id();
        dtl::ObDtlMsgHeader header;
        header.nbody_ = static_cast<int32_t>(finish_msg.get_serialize_size());
        header.type_ = static_cast<int16_t>(finish_msg.get_type());
        int64_t need_size = header.get_serialize_size() + finish_msg.get_serialize_size();
        dtl::ObDtlLinkedBuffer *buffer = nullptr;
        if (OB_ISNULL(buffer = ch->alloc_buf(need_size))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc buffer failed", K(ret));
        } else {
          auto buf = buffer->buf();
          auto size = buffer->size();
          auto &pos = buffer->pos();
          buffer->set_data_msg(false);
          buffer->timeout_ts() = timeout_ts_;
          buffer->set_msg_type(dtl::ObDtlMsgType::FINISH_SQC_RESULT);
          const bool inc_recv_buf_cnt = false;
          if (OB_FAIL(common::serialization::encode(buf, size, pos, header))) {
            LOG_WARN("fail to encode buffer", K(ret));
          } else if (OB_FAIL(common::serialization::encode(buf, size, pos, finish_msg))) {
            LOG_WARN("serialize RPC channel message fail", K(ret));
          } else if (FALSE_IT(buffer->size() = pos)) {
          } else if (FALSE_IT(pos = 0)) {
          } else if (FALSE_IT(buffer->tenant_id() = ch->get_tenant_id())) {
          } else if (OB_FAIL(ch->attach(buffer, inc_recv_buf_cnt))) {
            LOG_WARN("fail to feedup buffer", K(ret));
          } else if (FALSE_IT(ch->free_buffer_count())) {
          } else {
            need_interrupt_ = false;
          }
        }
        if (NULL != buffer) {
          ch->free_buffer_count();
        }
      }
    }
  }
  return ret;
}
// ObInitFastSqcP related functions.
int ObInitFastSqcP::init()
{
  int ret = OB_SUCCESS;
  ObPxSqcHandler *sqc_handler = nullptr;
 if (OB_ISNULL(sqc_handler = ObPxSqcHandler::get_sqc_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected sqc handler", K(ret));
  } else if (OB_FAIL(sqc_handler->init())) {
    LOG_WARN("Failed to init sqc handler", K(ret));
    sqc_handler->reset();
    op_reclaim_free(sqc_handler);
  } else {
    arg_.sqc_handler_ = sqc_handler;
    arg_.sqc_handler_->reset_reference_count(); // Set sqc_handler reference count to 1.
  }
  return ret;
}

void ObInitFastSqcP::destroy()
{
  obrpc::ObRpcProcessor<obrpc::ObPxRpcProxy::ObRpc<obrpc::OB_PX_FAST_INIT_SQC> >::destroy();
  /**
   * If the after process flow has been undergone, arg_.sqc_handler_ will be set to null.
   * If arg_.sqc_handler_ is not null here, it means that the after process flow has not been
   * performed after init, so the release should be done by itself.
   */
  if (OB_NOT_NULL(arg_.sqc_handler_)) {
    int report_ret = OB_SUCCESS;
    ObPxSqcHandler::release_handler(arg_.sqc_handler_, report_ret);
  }
}

int ObInitFastSqcP::process()
{
  GET_DIAGNOSTIC_INFO->get_ash_stat().in_sql_execution_ = true;
  int ret = OB_SUCCESS;
  LOG_TRACE("receive dfo", K_(arg));
  ObPxSqcHandler *sqc_handler = arg_.sqc_handler_;
  ObSQLSessionInfo *session = nullptr;
  if (OB_ISNULL(sqc_handler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Sqc handler can't be nullptr", K(ret));
  } else if (OB_FAIL(sqc_handler->init_env())) {
    LOG_WARN("Failed to init sqc env", K(ret));
  } else if (OB_ISNULL(sqc_handler = arg_.sqc_handler_)
             || !sqc_handler->valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid sqc handler", K(ret), KPC(sqc_handler));
  } else if (OB_FAIL(OB_E(EventTable::EN_PX_SQC_EXECUTE_FAILED) OB_SUCCESS)) {
    LOG_WARN("match sqc execute errism", K(ret));
  } else if (OB_ISNULL(session = sqc_handler->get_exec_ctx().get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Session can't be null", K(ret));
  } else if (OB_FAIL(sqc_handler->link_qc_sqc_channel())) {
    LOG_WARN("fail to link qc sqc channel", K(ret));
  } else {
    ObPxRpcInitSqcArgs &arg = sqc_handler->get_sqc_init_arg();
    arg.sqc_.set_task_count(1);
    ObPxInterruptGuard px_int_guard(arg.sqc_.get_interrupt_id().px_interrupt_id_);
    if (OB_FAIL(px_int_guard.get_interrupt_reg_ret())) {
      LOG_WARN("fast sqc failed to SET_INTERRUPTABLE");
    } else {
      lib::CompatModeGuard g(session->get_compatibility_mode() == ORACLE_MODE ?
      lib::Worker::CompatMode::ORACLE : lib::Worker::CompatMode::MYSQL);
      sqc_handler->set_tenant_id(session->get_effective_tenant_id());
      LOG_TRACE("process dfo",
                K(arg),
                K(session->get_compatibility_mode()),
                K(sqc_handler->get_reserved_px_thread_count()));
      if (OB_FAIL(startup_normal_sqc(*sqc_handler))) {
        LOG_WARN("fail to startup normal sqc", K(ret));
      }
    }
  }

  // 
  if (OB_SUCCESS != ret && is_schema_error(ret) && OB_NOT_NULL(sqc_handler)) {
    ObInterruptUtil::update_schema_error_code(&(sqc_handler->get_exec_ctx()), ret);
  }

  GET_DIAGNOSTIC_INFO->get_ash_stat().in_sql_execution_ = false;
  if (OB_NOT_NULL(sqc_handler)) {
    // link channel before or during the link process may fail.
    // If sqc and qc do not have a link, the response will notify px of ret.
    // If sqc and qc are already linked, notify px by dtl msg report.
    sqc_handler->set_end_ret(ret);
    if (sqc_handler->has_flag(OB_SQC_HANDLER_QC_SQC_LINKED)) {
      ret = OB_SUCCESS;
    }
    sqc_handler->reset_reference_count();
    int report_ret = OB_SUCCESS;
    ObPxSqcHandler::release_handler(sqc_handler, report_ret);
    ret = OB_SUCCESS == ret ? report_ret : ret;
    arg_.sqc_handler_ = nullptr;
  }
  return ret;
}

int ObInitFastSqcP::startup_normal_sqc(ObPxSqcHandler &sqc_handler)
{
  int ret = OB_SUCCESS;
  int64_t dispatched_worker_count = 0;
  ObSQLSessionInfo *session = sqc_handler.get_exec_ctx().get_my_session();
  ObPxSubCoord &sub_coord = sqc_handler.get_sub_coord();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else {
    ObPxRpcInitSqcArgs &arg = sqc_handler.get_sqc_init_arg();
    SQL_INFO_GUARD(arg.sqc_.get_monitoring_info().cur_sql_, session->get_cur_sql_id());
    ObWorkerSessionGuard worker_session_guard(session);
    ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
    session->set_peer_addr(arg.sqc_.get_qc_addr());
    ObDIActionGuard action_guard("PX SUB COORDINATOR");
    if (OB_FAIL(session->store_query_string(ObString::make_string("PX SUB COORDINATOR")))) {
      LOG_WARN("store query string to session failed", K(ret));
    } else if (OB_FAIL(sub_coord.pre_process())) {
      LOG_WARN("fail process sqc", K(arg), K(ret));
    } else if (OB_FAIL(sub_coord.try_start_tasks(dispatched_worker_count, true))) {
      LOG_WARN("fail to start tasks", K(ret));
    }
  }

  return ret;
}

void ObFastInitSqcCB::on_timeout()
{
  int ret = OB_TIMEOUT;
  ret = deal_with_rpc_timeout_err_safely();
  interrupt_qc(ret);
}

void ObFastInitSqcCB::log_warn_sqc_fail(int ret)
{
  // Do not change the follow log about px_obdiag_sqc_addr, becacue it will use in obdiag tool
  LOG_WARN("init fast sqc cb async interrupt qc", K_(trace_id), K(timeout_ts_), K(interrupt_id_),
           K(ret), "px_obdiag_sqc_addr", addr_);
}

int ObFastInitSqcCB::process()
{
  // 
  int ret = rcode_.rcode_;
  if (OB_FAIL(ret)) {
    int64_t cur_timestamp = ::oceanbase::common::ObTimeUtility::current_time();
    if (timeout_ts_ - cur_timestamp > 0) {
      interrupt_qc(ret);
      log_warn_sqc_fail(ret);
    } else {
      LOG_WARN("init fast sqc cb async timeout", K_(trace_id),
               K(addr_), K(timeout_ts_), K(cur_timestamp), K(ret));
    }
  }
  return ret;
}

int ObFastInitSqcCB::deal_with_rpc_timeout_err_safely()

{
  int ret = OB_SUCCESS;
  ObDealWithRpcTimeoutCall call(addr_, retry_info_, timeout_ts_, trace_id_);
  call.ret_ = OB_TIMEOUT;
  ObGlobalInterruptManager *manager = ObGlobalInterruptManager::getInstance();
  if (OB_NOT_NULL(manager)) {
    if (OB_FAIL(manager->get_map().atomic_refactored(interrupt_id_, call))) {
      LOG_WARN("fail to deal with rpc timeout call", K(interrupt_id_));
    }
  }
  return call.ret_;
}

void ObFastInitSqcCB::interrupt_qc(int err)
{
  int ret = OB_SUCCESS;
  ObGlobalInterruptManager *manager = ObGlobalInterruptManager::getInstance();
  if (OB_NOT_NULL(manager)) {
    // if we are sure init_sqc msg is not sent to sqc successfully, we don't have to set sqc not alive.
    bool init_sqc_not_send_out = (get_error() == EASY_TIMEOUT_NOT_SENT_OUT
                                 || get_error() == EASY_DISCONNECT_NOT_SENT_OUT);
    const bool need_set_not_alive = !init_sqc_not_send_out;
    ObFastInitSqcReportQCMessageCall call(sqc_, err, timeout_ts_, need_set_not_alive);
    if (OB_FAIL(manager->get_map().atomic_refactored(interrupt_id_, call))) {
      LOG_WARN("fail to set need report", K(interrupt_id_));
    } else if (!call.need_interrupt_) {
      /* do nothing*/
      LOG_WARN("ignore virtual table error,no need interrupt qc", K(ret));
    } else {
      int tmp_ret = OB_SUCCESS;
      ObInterruptCode int_code(err,
                               GETTID(),
                               GCTX.self_addr(),
                               "RPC ABORT PX");
      if (OB_SUCCESS != (tmp_ret = manager->interrupt(interrupt_id_, int_code))) {
        LOG_WARN("fail to send interrupt message", K_(trace_id),
          K(tmp_ret), K(int_code), K(interrupt_id_));
      }
    }
  }
}

void ObDealWithRpcTimeoutCall::deal_with_rpc_timeout_err()
{
  if (OB_TIMEOUT == ret_) {
    int64_t cur_timestamp = ::oceanbase::common::ObTimeUtility::current_time();
    // Due to the time difference caused by inconsistent time precision, here we need to satisfy greater than 100ms to be considered not a timeout.
    // A fault-tolerant processing.
    if (timeout_ts_ - cur_timestamp > 100 * 1000) {
      LOG_DEBUG("rpc return OB_TIMEOUT, but it is actually not timeout, "
                "change error code to OB_CONNECT_ERROR", K(ret_),
                K(timeout_ts_), K(cur_timestamp));
      ret_ = OB_RPC_CONNECT_ERROR;
    } else {
      LOG_DEBUG("rpc return OB_TIMEOUT, and it is actually timeout, "
                "do not change error code", K(ret_),
                K(timeout_ts_), K(cur_timestamp));
      if (NULL != retry_info_) {
        retry_info_->set_is_rpc_timeout(true);
      }
    }
  }
}

void ObDealWithRpcTimeoutCall::operator() (hash::HashMapPair<ObInterruptibleTaskID,
      ObInterruptCheckerNode *> &entry)
{
  UNUSED(entry);
  deal_with_rpc_timeout_err();
}

int ObPxTenantTargetMonitorP::init()
{
  return OB_SUCCESS;
}

void ObPxTenantTargetMonitorP::destroy()
{

}
// leader receives resource reports from each follower, and returns the latest view seen by the leader as the result to the follower
int ObPxTenantTargetMonitorP::process()
{
  int ret = OB_SUCCESS;
  ObTimeGuard timeguard("px_target_request", 100000);
  const uint64_t tenant_id = arg_.get_tenant_id();
  const uint64_t follower_version = arg_.get_version();
  // server id of the leader that the follower sync with previously.
  const uint64_t prev_leader_server_index = ObPxTenantTargetMonitor::get_server_index(follower_version);
  const uint64_t leader_server_index  = GCTX.get_server_index();
  bool is_leader;
  uint64_t leader_version;
  result_.set_tenant_id(tenant_id);
  if (OB_FAIL(OB_PX_TARGET_MGR.is_leader(tenant_id, is_leader))) {
    LOG_ERROR("get is_leader failed", K(ret), K(tenant_id));
  } else if (!is_leader) {
    result_.set_status(MONITOR_NOT_MASTER);
  } else if (arg_.need_refresh_all_ || prev_leader_server_index != leader_server_index) {
    if (OB_FAIL(OB_PX_TARGET_MGR.reset_leader_statistics(tenant_id))) {
      LOG_ERROR("reset leader statistics failed", K(ret));
    } else if (OB_FAIL(OB_PX_TARGET_MGR.get_version(tenant_id, leader_version))) {
      LOG_WARN("get master_version failed", K(ret), K(tenant_id));
    } else {
      result_.set_status(MONITOR_VERSION_NOT_MATCH);
      result_.set_version(leader_version);
      LOG_INFO("need refresh all", K(tenant_id), K(arg_.need_refresh_all_),
               K(follower_version), K(prev_leader_server_index), K(leader_server_index));
    }
  } else if (OB_FAIL(OB_PX_TARGET_MGR.get_version(tenant_id, leader_version))) {
    LOG_WARN("get master_version failed", K(ret), K(tenant_id));
  } else {
    result_.set_version(leader_version);
    if (follower_version != leader_version) {
      result_.set_status(MONITOR_VERSION_NOT_MATCH);
    } else {
      result_.set_status(MONITOR_READY);
      for (int i = 0; OB_SUCC(ret) && i < arg_.addr_target_array_.count(); i++) {
        ObAddr &server = arg_.addr_target_array_.at(i).addr_;
        int64_t peer_used_inc = arg_.addr_target_array_.at(i).target_;
        if (OB_FAIL(OB_PX_TARGET_MGR.update_peer_target_used(tenant_id, server, peer_used_inc, leader_version))) {
          LOG_WARN("set thread count failed", K(ret), K(tenant_id), K(server), K(peer_used_inc));
        }
      }

      // A simple and rude exception handling, re-statistics
      if (OB_FAIL(ret)) {
        int tem_ret = OB_SUCCESS;
        if ((tem_ret = OB_PX_TARGET_MGR.reset_leader_statistics(tenant_id)) != OB_SUCCESS) {
          LOG_ERROR("reset statistics failed", K(tem_ret), K(tenant_id), K(leader_version));
        } else {
          LOG_INFO("reset statistics succeed", K(tenant_id), K(leader_version));
        }
      } else {
        ObPxGlobalResGather gather(result_);
        if (OB_FAIL(OB_PX_TARGET_MGR.gather_global_target_usage(tenant_id, gather))) {
          LOG_WARN("get global thread count failed", K(ret), K(tenant_id));
        }
      }
    }
  }
  return ret;
}

int ObPxCleanDtlIntermResP::process()
{
  int ret = OB_SUCCESS;
  dtl::ObDTLIntermResultKey key;
#ifdef ERRSIM
  int ecode = EventTable::EN_PX_SINGLE_DFO_NOT_ERASE_DTL_INTERM_RESULT;
  if (OB_SUCCESS != ecode && OB_SUCC(ret)) {
    LOG_WARN("rpc not erase_dtl_interm_result by design", K(ret));
    return OB_SUCCESS;
  }
#endif
  int64_t batch_size = 0 == arg_.batch_size_ ? 1 : arg_.batch_size_;
  for (int64_t i = 0; i < arg_.info_.count(); i++) {
    ObPxCleanDtlIntermResInfo &info = arg_.info_.at(i);
    for (int64_t task_id = 0; task_id < info.task_count_; task_id++) {
      ObPxTaskChSet ch_set;
      if (OB_FAIL(ObDtlChannelUtil::get_receive_dtl_channel_set(info.sqc_id_, task_id,
            info.ch_total_info_, ch_set))) {
        LOG_WARN("get receive dtl channel set failed", K(ret));
      } else {
        LOG_TRACE("ObPxCleanDtlIntermResP process", K(i), K(arg_.batch_size_), K(info), K(task_id), K(ch_set));
        for (int64_t ch_idx = 0; ch_idx < ch_set.count(); ch_idx++) {
          key.channel_id_ = ch_set.get_ch_info_set().at(ch_idx).chid_;
          for (int64_t batch_id = 0; batch_id < batch_size && OB_SUCC(ret); batch_id++) {
            key.batch_id_= batch_id;
            if (OB_FAIL(MTL(dtl::ObDTLIntermResultManager*)->erase_interm_result_info(key))) {
              if (OB_HASH_NOT_EXIST == ret) {
                // interm result is written from batch_id = 0 to batch_size,
                // if some errors happen when batch_id = i, no interm result of batch_id > i will be written.
                // so if erase failed, just break and continue to erase interm result of next channel.
                ret = OB_SUCCESS;
                break;
              } else {
                LOG_WARN("fail to release receive internal result", K(ret), K(ret));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}
