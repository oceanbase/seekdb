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
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "sql/engine/px/ob_px_admission.h"
#include "sql/dtl/ob_dtl_channel_group.h"
using namespace oceanbase::sql;
using namespace oceanbase::common;

namespace oceanbase
{
using namespace common;
namespace sql
{


int ObPxWorkNotifier::wait_all_worker_start()
{
  int ret = OB_SUCCESS;
  const int64_t wait_us = 30000;
  uint32_t wait_key = cond_.get_key();
  /**
   * This semaphore may miss signals;
   * if a signal is missed, then wait for a period of time,
   * if the signal is not missed, then it is directly awakened.
   */
  bool is_interrupted = false;
  int64_t cnt = 1;
  while (start_worker_count_ != expect_worker_count_ && !is_interrupted) {
    cond_.wait(wait_key, wait_us);
    // check status after at most 1 second.
    if (0 == (cnt++ % 32)) {
      is_interrupted = IS_INTERRUPTED();
    }
  }
  return ret;
}

void ObPxWorkNotifier::worker_start(int64_t tid)
{
  int64_t start_worker_count = ATOMIC_AAF(&start_worker_count_, 1);
  if (start_worker_count == expect_worker_count_) {
    // notify rpc worker to exit
    cond_.signal();
  }
  tid_array_[start_worker_count - 1] = tid;
}

void ObPxWorkNotifier::worker_end(bool &all_worker_finish)
{
  all_worker_finish = false;
  /**
   * Memory should not be accessed after ATOMIC_AAF execution, as data on the heap may have been recycled.
   * Therefore, a record was made in advance here.
   */
  const int64_t rpc_thread = 1;
  int64_t expect_end_count = expect_worker_count_ + rpc_thread;
  int64_t finish_worker_count = ATOMIC_AAF(&finish_worker_count_, 1);
  if (finish_worker_count == expect_end_count) {
    all_worker_finish = true;
  }
}

int ObPxWorkNotifier::set_expect_worker_count(int64_t worker_count)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tid_array_.prepare_allocate(worker_count))) {
    LOG_WARN("failed to prepare allocate worker", K(ret));
  } else {
    expect_worker_count_ = worker_count;
  }
  return ret;
}

int ObPxSqcHandler::worker_end_hook() {
  int ret = OB_SUCCESS;
  bool all_finish = false;
  notifier_->worker_end(all_finish);
  if (all_finish) {
    LOG_TRACE("all sqc finished, begin sqc end process");
    if (OB_FAIL(sub_coord_->end_process())) {
      LOG_WARN("failed to end sqc", K(ret));
    }
  }
  end_ret_ = ret;
  return ret;
}

int ObPxSqcHandler::pre_acquire_px_worker(int64_t &reserved_thread_count)
{
  int ret = OB_SUCCESS;
  int64_t max_thread_count = sqc_init_args_->sqc_.get_max_task_count();
  int64_t min_thread_count = sqc_init_args_->sqc_.get_min_task_count();
    // Pre-reserve thread count in the tenant for px worker execution
  ObPxSubAdmission::acquire(max_thread_count, min_thread_count, reserved_px_thread_count_);
  reserved_px_thread_count_ = reserved_px_thread_count_ < min_thread_count ? 0 : reserved_px_thread_count_;
  if (OB_FAIL(notifier_->set_expect_worker_count(reserved_px_thread_count_))) {
    LOG_WARN("failed to set expect worker count", K(ret), K(reserved_px_thread_count_));
  } else {
    sqc_init_args_->sqc_.set_task_count(reserved_px_thread_count_);
    reserved_thread_count = reserved_px_thread_count_;
  }
  if (OB_SUCC(ret)) {
    if (reserved_px_thread_count_ < max_thread_count &&
        reserved_px_thread_count_ >= min_thread_count) {
      LOG_INFO("Downgrade px thread allocation",
              K_(reserved_px_thread_count),
              K(max_thread_count),
              K(min_thread_count),
              K(reserved_thread_count),
              K(sqc_init_args_));
    }
    /**
     * sqc handler's reference count, 1 for rpc thread, at this time only the rpc thread references it.
     */
    reference_count_ = 1;
    LOG_TRACE("SQC acquire px worker", K(max_thread_count), K(min_thread_count),
        K(reserved_px_thread_count_), K(reserved_thread_count));
  }
  return ret;
}

ObPxSqcHandler *ObPxSqcHandler::get_sqc_handler()
{
  return op_reclaim_alloc(ObPxSqcHandler);
}

void ObPxSqcHandler::release_handler(ObPxSqcHandler *sqc_handler, int &report_ret)
{
  bool all_released = false;
  if (OB_ISNULL(sqc_handler)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "Get null sqc handler", K(sqc_handler));
  } else if (FALSE_IT(sqc_handler->release(all_released))) {
  } else if (all_released) {
    IGNORE_RETURN sqc_handler->check_rf_leak();
    IGNORE_RETURN sqc_handler->destroy_sqc(report_ret);
    sqc_handler->reset();
    op_reclaim_free(sqc_handler);
  }
}

void ObPxSqcHandler::check_rf_leak()
{
  IGNORE_RETURN sub_coord_->destroy_shared_rf_msgs();
}

int ObPxSqcHandler::init()
{
  int ret = OB_SUCCESS;
  tenant_id_ = MTL_ID();
  reserved_px_thread_count_ = 0;
  rpc_level_ = THIS_WORKER.get_curr_request_level();
  void *buf = nullptr;
  observer::ObGlobalContext &gctx = GCTX;
  lib::ContextParam param;
  param.set_mem_attr(MTL_ID(), "SqcHandlerParam")
    .set_parallel(4)
    .set_properties(lib::ALLOC_THREAD_SAFE);
  ObIAllocator *allocator = nullptr;
  if (OB_FAIL(ROOT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
    LOG_WARN("create memory entity failed", K(ret));
    } else if (OB_ISNULL(mem_context_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null memory entity returned", K(ret));
  } else {
    allocator = &mem_context_->get_arena_allocator();
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(buf = allocator->alloc(sizeof(ObPxWorkNotifier)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc px nofitier", K(ret));
  } else if (FALSE_IT(notifier_ = new(buf) ObPxWorkNotifier())) {
  } else if (OB_ISNULL(buf = allocator->alloc(sizeof(ObDesExecContext)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc des execontext", K(ret));
  } else if (FALSE_IT(exec_ctx_ = new(buf) ObDesExecContext(*allocator, gctx.session_mgr_))) {
  } else if (OB_ISNULL(buf = allocator->alloc(sizeof(ObPhysicalPlan)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc physical plan", K(ret));
  } else if (FALSE_IT(des_phy_plan_ = new(buf) ObPhysicalPlan(mem_context_))) {
  } else if (OB_ISNULL(buf = allocator->alloc(sizeof(ObPxRpcInitSqcArgs)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc sqc init args", K(ret));
  } else if (FALSE_IT(sqc_init_args_ = new(buf) ObPxRpcInitSqcArgs())) {
  } else if (OB_ISNULL(buf = allocator->alloc(sizeof(ObPxSubCoord)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc des px sub coord", K(ret));
  } else if (FALSE_IT(sub_coord_ = new(buf) ObPxSubCoord(gctx, *sqc_init_args_))) {
  } else {
    exec_ctx_->set_sqc_handler(this);
  }

#ifdef ERRSIM
  int errsim_code = EventTable::EN_PX_SQC_HANDLER_INIT_FAILED;
  if (OB_SUCC(ret) && errsim_code != OB_SUCCESS) {
    ret = errsim_code;
    LOG_TRACE("Force sqc hanler init failed", K(ret));
  }
#endif

  return ret;
}

// The factory will invoke this function.
void ObPxSqcHandler::reset()
{
  tenant_id_ = UINT64_MAX;
  reserved_px_thread_count_ = 0;
  process_flags_ = 0;
  end_ret_ = OB_SUCCESS;
  reference_count_ = 1;
  is_session_query_locked_ = false;
  part_ranges_.reset();
  call_dtor(sub_coord_);
  call_dtor(sqc_init_args_);
  call_dtor(des_phy_plan_);
  call_dtor(exec_ctx_);
  call_dtor(notifier_);
  if (nullptr != mem_context_) {
    DESTROY_CONTEXT(mem_context_);
    mem_context_ = NULL;
  }
}

int ObPxSqcHandler::copy_sqc_init_arg(int64_t &pos, const char *data_buf, int64_t data_len)
{
  int ret = OB_SUCCESS;
  ObIAllocator *allocator = nullptr;
  if (OB_ISNULL(mem_context_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Sqc handler need to be inited", K(ret));
  } else {
    allocator = &mem_context_->get_arena_allocator();
    WITH_CONTEXT(mem_context_) {
      sqc_init_args_->set_deserialize_param(*exec_ctx_, *des_phy_plan_, allocator);
      if (OB_FAIL(sqc_init_args_->do_deserialize(pos, data_buf, data_len))) {
        LOG_WARN("Failed to deserialize", K(ret));
      }
      sqc_init_args_->sqc_handler_ = this;
    }
  }
  return ret;
}

int ObPxSqcHandler::init_env()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *phy_plan_ctx = nullptr;
  ObSQLSessionInfo *session = NULL;
  int tmp_ret = OB_SUCCESS;
  if (OB_ISNULL(sqc_init_args_->exec_ctx_)
      || OB_ISNULL(sqc_init_args_->op_spec_root_)
      || OB_ISNULL(sqc_init_args_->des_phy_plan_)
      || OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(*sqc_init_args_->exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sqc args should not be NULL", K(ret));
  } else if (OB_ISNULL(session = GET_MY_SESSION(*sqc_init_args_->exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("deserialized exec ctx without phy plan session set. Unexpected", K(ret));
  } else if (OB_FAIL(sub_coord_->init_exec_env(*sqc_init_args_->exec_ctx_))) {
    LOG_WARN("Failed to init env", K(ret));
  } else if (OB_UNLIKELY(common::OB_SUCCESS != (tmp_ret = session->get_query_lock().lock()))) {
    LOG_ERROR("Fail to lock, ", K(tmp_ret));
  } else {
    is_session_query_locked_ = true;
    init_flt_content();
  }

  if (OB_SUCC(ret) && OB_UNLIKELY(session->is_zombie())) {
    // session has been killed some moment ago
    // we don't need explicitly cll unlock() here, we do it in
    // ObPxSqcHandler::destroy_sqc()
    ret = OB_ERR_SESSION_INTERRUPTED;
    LOG_WARN("session has been killed", K(session->get_session_state()), K(session->get_server_sid()),
             "proxy_sessid", session->get_proxy_sessid(), K(ret));
  }
  return ret;
}

void ObPxSqcHandler::init_flt_content()
{
  if (OBTRACE->is_inited()) {
    flt_ctx_.trace_id_ = OBTRACE->get_trace_id();
    flt_ctx_.span_id_ = OBTRACE->get_root_span_id();
    flt_ctx_.policy_ = OBTRACE->get_policy();
  }
  LOG_TRACE("init flt rpc content", K(flt_ctx_), K(OBTRACE->get_trace_id()));
}



int ObPxSqcHandler::destroy_sqc(int &report_ret)
{
  int ret = OB_SUCCESS;
  int end_ret = OB_SUCCESS;
  report_ret = OB_SUCCESS;
  // end_ret_records the error that occurred when SQC end process happened, at this moment the statement has already been executed, and the cleanup work has been done
  // Issue. Compared to the error codes of the transaction, the error codes at the end have a lower priority.
  end_ret = end_ret_;

  // clean up ddl context if needed
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = sub_coord_->end_ddl(false))) { //for ddl px, don't call close() again in close_direct_load()
    LOG_WARN("end ddl failed", K(tmp_ret));
    end_ret = OB_SUCCESS == end_ret ? tmp_ret : end_ret;
  }
  if (has_flag(OB_SQC_HANDLER_QC_SQC_LINKED)) {
    /**
     * The connection of the sqc-qc channel is the last step in rpc process. If the link is successful, this flag will be set.
     * That means once this flag is set, rpc will return normally, and qc will consider sqc as started normally,
     * qc will record in its own information that this sqc has started normally. qc's current termination is synchronous termination, every
     * marked started sqc must report to qc when it ends normally or abnormally. If any marked started sqc does not report, qc will keep waiting until timeout.
     */
    if (OB_FAIL(sub_coord_->report_sqc_finish(end_ret))) {
      LOG_WARN("fail report sqc to qc", K(ret), K(end_ret_));
      report_ret = ret;
    }
    ObPxSqcMeta &sqc = sqc_init_args_->sqc_;
    LOG_TRACE("sqc send report to qc", K(sqc));
  }
  if (has_flag(OB_SQC_HANDLER_QC_SQC_LINKED)) {
    get_sqc_ctx().sqc_proxy_.destroy();
    ObPxSqcMeta &sqc = sqc_init_args_->sqc_;
    dtl::ObDtlChannelInfo &ci = sqc.get_sqc_channel_info();
    dtl::ObDtlChannel *ch = sqc.get_sqc_channel();
    // Here ch may be 0, if it is normal execution,
    // It will set ch to 0 inside the unlink_sqc_qc_channel of ObPxSQCProxy.
    // If the normal termination process is not followed, then rely on here for release.
    if (OB_NOT_NULL(ch) && OB_FAIL(dtl::ObDtlChannelGroup::unlink_channel(ci))) {
      LOG_WARN("Failed to unlink channel", K(ret));
    }
  }

  ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(session = GET_MY_SESSION(*sqc_init_args_->exec_ctx_))) {
    LOG_WARN("session is null, which is unexpected!", K(ret));
  } else if (is_session_query_locked_) {
    if (OB_UNLIKELY(OB_SUCCESS != (tmp_ret = session->get_query_lock().unlock()))) {
      LOG_ERROR("Fail to unlock, ", K(tmp_ret));
    }
  }
  return ret;
}

int ObPxSqcHandler::link_qc_sqc_channel()
{
  int ret = OB_SUCCESS;
  ObPxSqcMeta &sqc = sqc_init_args_->sqc_;
  dtl::ObDtlChannelInfo &ci = sqc.get_sqc_channel_info();
  dtl::ObDtlChannel *ch = NULL;
  if (OB_FAIL(dtl::ObDtlChannelGroup::link_channel(ci, ch))) {
    LOG_WARN("Failed to link qc-sqc channel", K(ci), K(ret));
  } else {
    ch->set_sqc_owner();
    ch->set_thread_id(GETTID());
    sqc.set_sqc_channel(ch);
    add_flag(OB_SQC_HANDLER_QC_SQC_LINKED);
    OZ(get_sqc_ctx().sqc_proxy_.init());
  }
  return ret;
}

void ObPxSqcHandler::check_interrupt()
{
  if (OB_UNLIKELY(IS_INTERRUPTED())) {
    has_interrupted_ = true;
    // Interrupt error handling
    ObInterruptCode code = GET_INTERRUPT_CODE();
    int ret = code.code_;
    if (OB_NOT_NULL(sqc_init_args_)) {
      LOG_WARN("sqc interrupted", K(ret), K(code), K(sqc_init_args_->sqc_));
      ObPxSqcMeta &sqc = sqc_init_args_->sqc_;
      ObInterruptUtil::interrupt_tasks(sqc, ret);
    }
  }
}

int ObPxSqcHandler::thread_count_auto_scaling(int64_t &reserved_px_thread_count)
{
  int ret = OB_SUCCESS;
  /* strategy 1
   * if single table cannot be divided into much ranges,
   * the max thread cnt should be less than ranges cnt.
   */
  int64_t range_cnt = 0;
  ObGranulePump &pump = sub_coord_->get_sqc_ctx().gi_pump_;
  int64_t temp_cnt = reserved_px_thread_count;
  if (reserved_px_thread_count <= 1 || !sqc_init_args_->sqc_.is_single_tsc_leaf_dfo()) {
  } else if (OB_ISNULL(sub_coord_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("subcoord is null", K(ret));
  } else {
    ObGranulePump &pump = sub_coord_->get_sqc_ctx().gi_pump_;
    if (OB_FAIL(pump.get_first_tsc_range_cnt(range_cnt))) {
      LOG_WARN("fail to get first tsc range cnt", K(ret));
    } else if (0 == range_cnt) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("range cnt equal 0", K(ret));
    } else {
      reserved_px_thread_count = min(reserved_px_thread_count, range_cnt);
      reserved_px_thread_count_ = reserved_px_thread_count;
      if (temp_cnt > reserved_px_thread_count) {
        LOG_TRACE("sqc px worker auto-scaling worked", K(temp_cnt), K(range_cnt), K(reserved_px_thread_count));
      }
      if (OB_FAIL(notifier_->set_expect_worker_count(reserved_px_thread_count))) {
        LOG_WARN("failed to set expect worker count", K(ret), K(reserved_px_thread_count_));
      } else {
        sqc_init_args_->sqc_.set_task_count(reserved_px_thread_count);
      }
    }
  }
  return ret;
}

int ObPxSqcHandler::set_partition_ranges(const Ob2DArray<ObPxTabletRange> &part_ranges,
                                        char *buf, int64_t size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(part_ranges.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("part ranges is empty", K(ret), K(part_ranges.count()));
  } else {
    bool part_ranges_empty = false;
    {
      SpinRLockGuard rlock_guard(part_ranges_spin_lock_);
      part_ranges_empty = part_ranges_.empty();
    }
    // If the worker thread has already set the part_ranges_;
    // there is no need to repeat the setup.
    if (part_ranges_empty) {
      SpinWLockGuard wlock_guard(part_ranges_spin_lock_);
      if (part_ranges_.empty()) {
        int64_t pos = 0;
        ObPxTabletRange tmp_range;
        for (int64_t i = 0; OB_SUCC(ret) && i < part_ranges.count(); ++i) {
          const ObPxTabletRange &cur_range = part_ranges.at(i);
          if (0 == size && OB_FAIL(tmp_range.deep_copy_from<true>(cur_range, get_safe_allocator(), buf, size, pos))) {
            LOG_WARN("deep copy partition range failed", K(ret), K(cur_range));
          } else if (0 != size && OB_FAIL(tmp_range.deep_copy_from<false>(cur_range, get_safe_allocator(), buf, size, pos))) {
            LOG_WARN("deep copy partition range failed", K(ret), K(cur_range));
          } else if (OB_FAIL(part_ranges_.push_back(tmp_range))) {
            LOG_WARN("push back partition range failed", K(ret), K(tmp_range));
          }
        }
      }
    }
  }
  return ret;
}

} // sql
} // oceanbase
