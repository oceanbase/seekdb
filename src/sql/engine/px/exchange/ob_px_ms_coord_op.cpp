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

#include "ob_px_ms_coord_op.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
using namespace sql::dtl;
namespace sql
{

OB_SERIALIZE_MEMBER((ObPxMSCoordOpInput, ObPxReceiveOpInput));

OB_SERIALIZE_MEMBER((ObPxMSCoordSpec, ObPxCoordSpec),
                    all_exprs_,
                    sort_collations_,
                    sort_cmp_funs_,
                    is_old_unblock_mode_);

int ObPxMSCoordOp::ObPxMSCoordOpEventListener::on_root_data_channel_setup()
{
  int ret = OB_SUCCESS;
  // root dfo's receive channel sets are used locally, no need to send through DTL
  // Directly register to msg_loop to receive datais sufficient
  int64_t cnt = px_coord_op_.task_ch_set_.count();
  // FIXME:  msg_loop_ do not unregister_channel, avoid this ...start_ index change ?
  // dtl data channel register when create channel, so msg_loop_ contains data channels
  px_coord_op_.receive_order_.set_data_channel_idx_range(
      px_coord_op_.msg_loop_.get_channel_count() - cnt,
      px_coord_op_.msg_loop_.get_channel_count());
  // Here you must initialize row heap here, because row heap can only be established after obtaining the data channel,
  // But px coord inner_get_next_row logic must start to run, i.e., must receive control msg etc.,
  // So data channel is a bit lagging
  if (OB_FAIL(px_coord_op_.init_row_heap(cnt))) {
    LOG_WARN("failed to init row heap", K(ret), K(cnt));
  } else {
    LOG_TRACE("px coord setup", K(cnt), K(px_coord_op_.msg_loop_.get_channel_count()));
  }
  return ret;
}

int ObPxMSCoordOp::receive_channel_root_dfo(ObExecContext &ctx,
                                            ObDfo &parent,
                                            ObPxTaskChSets &parent_ch_sets)
{
  int ret = OB_SUCCESS;
  OZ(ObPxCoordOp::receive_channel_root_dfo(ctx, parent, parent_ch_sets));
  OZ(setup_readers());
  return ret;
}

int ObPxMSCoordOp::receive_channel_root_dfo(ObExecContext &ctx,
                                            ObDfo &parent,
                                            dtl::ObDtlChTotalInfo &ch_info)
{
  int ret = OB_SUCCESS;
  OZ(ObPxCoordOp::receive_channel_root_dfo(ctx, parent, ch_info));
  OZ(setup_readers());
  return ret;
}

ObPxMSCoordOp::ObPxMSCoordOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
: ObPxCoordOp(exec_ctx, spec, input),
  listener_(*this),
  serial_scheduler_(coord_info_, *this, listener_),
  parallel_scheduler_(coord_info_, *this, listener_, msg_proc_),
  msg_proc_(coord_info_, listener_, *this),
  sqc_finish_msg_proc_(exec_ctx, msg_proc_),
  sqc_init_msg_proc_(exec_ctx, msg_proc_),
  barrier_piece_msg_proc_(exec_ctx, msg_proc_),
  winbuf_piece_msg_proc_(exec_ctx, msg_proc_),
  interrupt_proc_(exec_ctx, msg_proc_),
  sample_piece_msg_proc_(exec_ctx, msg_proc_),
  rollup_key_piece_msg_proc_(exec_ctx, msg_proc_),
  rd_wf_piece_msg_proc_(exec_ctx, msg_proc_),
  init_channel_piece_msg_proc_(exec_ctx, msg_proc_),
  reporting_wf_piece_msg_proc_(exec_ctx, msg_proc_),
  opt_stats_gather_piece_msg_proc_(exec_ctx, msg_proc_),
  sp_winfunc_px_piece_msg_proc_(exec_ctx, msg_proc_),
  rd_winfunc_px_piece_msg_proc_(exec_ctx, msg_proc_),
  join_filter_count_row_piece_msg_proc_(exec_ctx, msg_proc_),
  store_rows_(),
  last_pop_row_(nullptr),
  row_heap_(),
  receive_order_(row_heap_),
  finish_ch_cnt_(0),
  all_rows_finish_(false),
  readers_(NULL),
  reader_cnt_(0),
  alloc_()
{
}

void ObPxMSCoordOp::destroy()
{
  last_pop_row_ = nullptr;
  row_heap_.reset();
  store_rows_.reset();
  destroy_readers();
  alloc_.reset();
  // no need to reset receive_order_
  // no need to reset interrupt_proc_
  // no need to reset sqc_init_msg_proc_
  // no need to reset sqc_finish_msg_proc_
  // no need to reset msg_proc_
  // no need to reset listener_
  ObPxCoordOp::destroy();
}

int ObPxMSCoordOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObPxCoordOp::inner_open())) {
    LOG_WARN("fail close op", K(MY_SPEC.id_), K(ret));
  } else if (OB_FAIL(setup_loop_proc())) {
    LOG_WARN("fail setup loop proc", K(ret));
  } else {
    if (use_serial_scheduler_) {
      msg_proc_.set_scheduler(&serial_scheduler_);
    } else {
      msg_proc_.set_scheduler(&parallel_scheduler_);
    }
    alloc_.set_label(common::ObModIds::OB_SQL_PX);
    alloc_.set_tenant_id(ctx_.get_my_session()->get_effective_tenant_id());
    metric_.set_id(MY_SPEC.id_);
  }
  return ret;
}

int ObPxMSCoordOp::inner_rescan()
{
  px_row_msg_proc_.set_reader(NULL);
  finish_ch_cnt_ = 0;
  all_rows_finish_ = false;
  destroy_readers();
  row_heap_.reset_heap();
  int ret = ObPxCoordOp::inner_rescan();
  return ret;
}

int ObPxMSCoordOp::setup_loop_proc()
{
  int ret = OB_SUCCESS;
  // disable data read before readers setup.
  px_row_msg_proc_.set_reader(NULL);

  msg_loop_.clear_all_proc();
  (void)msg_loop_
      .register_processor(px_row_msg_proc_)
      .register_processor(sqc_init_msg_proc_)
      .register_processor(sqc_finish_msg_proc_)
      .register_processor(barrier_piece_msg_proc_)
      .register_processor(winbuf_piece_msg_proc_)
      .register_processor(sample_piece_msg_proc_)
      .register_processor(rollup_key_piece_msg_proc_)
      .register_processor(rd_wf_piece_msg_proc_)
      .register_processor(init_channel_piece_msg_proc_)
      .register_processor(reporting_wf_piece_msg_proc_)
      .register_processor(opt_stats_gather_piece_msg_proc_)
      .register_processor(sp_winfunc_px_piece_msg_proc_)
      .register_processor(rd_winfunc_px_piece_msg_proc_)
      .register_processor(join_filter_count_row_piece_msg_proc_)
      .register_interrupt_processor(interrupt_proc_);
  msg_loop_.set_tenant_id(ctx_.get_my_session()->get_effective_tenant_id());
  return ret;
}

int ObPxMSCoordOp::init_store_rows(int64_t n_ways)
{
  int ret = OB_SUCCESS;
  if (0 < store_rows_.count() || 0 == n_ways) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected status: store rows is not empty", K(ret), K(n_ways));
  }
  for (int64_t i = 0; i < n_ways && OB_SUCC(ret); ++i) {
    void *buf = alloc_.alloc(sizeof(ObChunkDatumStore::LastStoredRow));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloca memory", K(ret));
    } else {
      ObChunkDatumStore::LastStoredRow *store_row = new(buf)ObChunkDatumStore::LastStoredRow(alloc_);
      store_row->reuse_ = true;
      if (OB_FAIL(store_rows_.push_back(store_row))) {
        LOG_WARN("failed to push back", K(ret));
      }
    }
  }
  return ret;
}

int ObPxMSCoordOp::init_row_heap(int64_t n_ways)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_store_rows(n_ways))) {
    LOG_WARN("failed to init store rows", K(ret));
  } else if (OB_FAIL(row_heap_.init(n_ways,
      &MY_SPEC.sort_collations_,
      &MY_SPEC.sort_cmp_funs_))) {
    LOG_WARN("fail init row heap for root dfo", "heap", row_heap_, K(ret));
  }
  return ret;
}

int ObPxMSCoordOp::setup_readers()
{
  int ret = OB_SUCCESS;
  CK(task_channels_.count() > 0);
  CK(NULL == readers_);
  if (OB_SUCC(ret)) {
    readers_ = static_cast<ObReceiveRowReader *>(alloc_.alloc(
            sizeof(*readers_) * task_channels_.count()));
    if (NULL == readers_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else {
      reader_cnt_ = task_channels_.count();
      bool reorder_fixed_expr = true;
      common::ObIAllocator *allocator = &ctx_.get_allocator();
      for (int64_t i = 0; i < reader_cnt_; i++) {
        new (&readers_[i]) ObReceiveRowReader(get_spec().id_,
              &(static_cast<const ObPxReceiveSpec &>(spec_).child_exprs_),
              reorder_fixed_expr,
              allocator);
      }
    }
  }
  if (OB_SUCC(ret)) {
    int64_t idx = row_heap_.writable_channel_idx();
    if (idx < 0 || idx >= reader_cnt_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid heap writeable channel idx", K(ret));
    } else {
      px_row_msg_proc_.set_reader(readers_ + idx);
    }
  }
  return ret;
}

void ObPxMSCoordOp::destroy_readers()
{
  if (NULL != readers_) {
    for (int64_t i = 0; i < reader_cnt_; i++) {
      readers_[i].~ObReceiveRowReader();
    }
    alloc_.free(readers_);
    readers_ = NULL;
    reader_cnt_ = 0;
  }
  px_row_msg_proc_.set_reader(NULL);
}

void ObPxMSCoordOp::reset_readers()
{
  if (NULL != readers_) {
    int64_t idx = row_heap_.writable_channel_idx();
    for (int64_t i = 0; i < reader_cnt_; i++) {
      readers_[i].reset();
    }
    px_row_msg_proc_.set_reader(readers_ + idx);
  }
}

int ObPxMSCoordOp::free_allocator()
{
  int ret = OB_SUCCESS;
  last_pop_row_ = nullptr;
  //
  //the heap shoud be empty before store_rows_.reset();
  while(OB_SUCC(ret) && row_heap_.count() > 0) {
     const ObChunkDatumStore::LastStoredRow *pop_row = nullptr;
    if (OB_SUCC(row_heap_.raw_pop(pop_row))) {
      row_heap_.shrink();
    } else {
      LOG_WARN("pop data fail", K(row_heap_), K(ret));
    }
  }
  store_rows_.reset();
  alloc_.reset();
  return ret;
}

int ObPxMSCoordOp::inner_close()
{
  int ret = OB_SUCCESS;
  finish_ch_cnt_ = 0;
  all_rows_finish_ = false;
  if (OB_FAIL(ObPxCoordOp::inner_close())) {
    LOG_WARN("fail close op", K(MY_SPEC.id_), K(ret));
  }
  destroy_readers();
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = free_allocator())) {
    LOG_WARN("failed to free allcator", K(ret), K(tmp_ret), K(ret));
    ret = tmp_ret;
  }
  LOG_TRACE("byebye. exit MergeSort QC Coord");
  return ret;
}

int ObPxMSCoordOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  return wrap_get_next_batch(max_row_cnt);
}

int ObPxMSCoordOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *phy_plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  if (iter_end_) {
    ret = OB_ITER_END;
  } else if (OB_UNLIKELY(!first_row_fetched_)) {
    // Drive initial DFO distribution
    if (OB_FAIL(msg_proc_.startup_msg_loop(ctx_))) {
      LOG_WARN("initial dfos NOT dispatched successfully", K(ret));
    }
    first_row_fetched_ = true; // control no longer actively calling startup_msg_loop, subsequent loops are message triggered
  }
  bool wait_next_msg = true; // Control whether to exit the while loop, return row to the upper operator
  while (OB_SUCC(ret) && wait_next_msg) {
    // loop registered the SQC-QC control channel, as well as TASKs-QC data channel
    // To achieve orderly receive, TASKs-QC channel needs to be added one by one to the loop
    int64_t timeout_us = 0;
    int64_t nth_channel = OB_INVALID_INDEX_INT64;
    // Note:
    //   ObPxMSCoordOp::inner_get_next_row is invoked in two pathes (batch vs
    //   non-batch). The eval flag should be cleared with separated flags
    //   under each invoke path (batch vs non-batch). Therefore call the overriding
    //   API do_clear_datum_eval_flag() to replace clear_evaluated_flag
    // TODO qubin.qb: Implement separated ObPxMSCoordOp::inner_get_next_batch to
    // isolate them
    do_clear_datum_eval_flag();
    clear_dynamic_const_parent_flag();
    if (row_heap_.capacity() > 0) {
      int64_t idx = row_heap_.writable_channel_idx();
      ObReceiveRowReader *reader = (NULL != readers_ && idx < reader_cnt_) ? &readers_[idx] : NULL;
      ObDtlChannel *ch = msg_loop_.get_channel(idx + receive_order_.get_data_channel_start_idx());
      if (NULL == ch || NULL == reader) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("reader or channel is NULL");
      } else if (OB_FAIL(ctx_.fast_check_status())) {
        LOG_WARN("failed to check status", K(ret));
      } else {
        if (reader->has_more() || ch->is_eof()) {
          ret = next_row(*reader, wait_next_msg);
          if (!first_row_sent_) {
            first_row_sent_ = true;
            LOG_TRACE("TIMERECORD ",
                     "reserve:=1 name:=RQC dfoid:=-1 sqcid:=-1 taskid:=-1 start:",
                     ObTimeUtility::current_time());
          }
          continue;
        } else {
          // fetch row for writable index reader
          px_row_msg_proc_.set_reader(reader);
        }
      }
    }

    // process end condition:
    // 1. all rows returned
    // 2. all SQC report worker execution finish
    if (OB_SUCCESS == ret) {
      if (all_rows_finish_ && coord_info_.all_threads_finish_) {
        (void) msg_proc_.on_process_end(ctx_);
        ret = OB_ITER_END;
        LOG_TRACE("all rows received, all sqcs reported, qc says: byebye!", K(ret));
        LOG_TRACE("TIMERECORD ",
                 "reserve:=1 name:=RQC dfoid:=-1 sqcid:=-1 taskid:=-1 end:",
                 ObTimeUtility::current_time());
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ctx_.fast_check_status())) {
      LOG_WARN("fail check status, maybe px query timeout", K(ret));
    } else if (OB_FAIL(msg_loop_.process_one_if(&receive_order_, nth_channel))) {
      if (OB_DTL_WAIT_EAGAIN == ret) {
        LOG_TRACE("no message, try again", K(ret));
        ret = OB_SUCCESS;
        // if no data, then unblock blocked data channel, if not, dtl maybe hang
        // bug#28253162
        if (0 < row_heap_.capacity() && first_row_sent_) {
          if (MY_SPEC.is_old_unblock_mode_) {
            if (OB_FAIL(msg_loop_.unblock_channels(receive_order_.get_data_channel_start_idx()))) {
              LOG_WARN("failed to unblock channels", K(ret));
            } else {
              LOG_DEBUG("debug old unblock_channels", K(ret));
            }
          } else {
            if (OB_FAIL(msg_loop_.unblock_channel(receive_order_.get_data_channel_start_idx(),
                                                  row_heap_.writable_channel_idx()))) {
              LOG_WARN("failed to unblock channels", K(ret));
            } else {
              LOG_DEBUG("debug old unblock_channel", K(ret));
            }
          }
        }
      } else if (OB_ITER_END != ret) {
        LOG_WARN("fail process message", K(ret));
      }
    } else {
      ObDtlMsgType msg_type = msg_loop_.get_last_msg_type();
      switch (msg_type) {
        case ObDtlMsgType::PX_NEW_ROW:
          // got rows
          break;
        case ObDtlMsgType::INIT_SQC_RESULT:
        case ObDtlMsgType::FINISH_SQC_RESULT:
        case ObDtlMsgType::DH_BARRIER_PIECE_MSG:
        case ObDtlMsgType::DH_WINBUF_PIECE_MSG:
        case ObDtlMsgType::DH_DYNAMIC_SAMPLE_PIECE_MSG:
        case ObDtlMsgType::DH_ROLLUP_KEY_PIECE_MSG:
        case ObDtlMsgType::DH_RANGE_DIST_WF_PIECE_MSG:
        case ObDtlMsgType::DH_INIT_CHANNEL_PIECE_MSG:
        case ObDtlMsgType::DH_SECOND_STAGE_REPORTING_WF_PIECE_MSG:
        case ObDtlMsgType::DH_OPT_STATS_GATHER_PIECE_MSG:
        case ObDtlMsgType::DH_SP_WINFUNC_PX_PIECE_MSG:
        case ObDtlMsgType::DH_RD_WINFUNC_PX_PIECE_MSG:
        case ObDtlMsgType::DH_JOIN_FILTER_COUNT_ROW_PIECE_MSG:
          // These messages are all handled in the process callback function
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Unexpected msg type", K(msg_type));
          break;
      }
    }
  }
  if (ret == OB_ITER_END) {
    if (!iter_end_ && all_rows_finish_) {
      iter_end_ = true;
      LOG_TRACE("RECORDTIME", K(time_recorder_));
    }
  } else if (OB_UNLIKELY(OB_SUCCESS != ret)) {
    int ret_terminate = terminate_running_dfos(coord_info_.dfo_mgr_);
    LOG_WARN("QC get error code", K(ret), K(ret_terminate));
    if ((OB_ERR_SIGNALED_IN_PARALLEL_QUERY_SERVER == ret
        || OB_GOT_SIGNAL_ABORTING == ret)
        && OB_SUCCESS != ret_terminate) {
      ret = ret_terminate;
    }
  }
  return ret;
}

int ObPxMSCoordOp::next_row(ObReceiveRowReader &reader, bool &wait_next_msg)
{
  int ret = OB_SUCCESS;
  wait_next_msg = true;
  LOG_TRACE("Begin next_row");
  metric_.mark_interval_start();
  ret = reader.get_next_row(MY_SPEC.child_exprs_, MY_SPEC.dynamic_const_exprs_, eval_ctx_);
  metric_.mark_interval_end(&time_recorder_);
  if (OB_ITER_END == ret) {
    finish_ch_cnt_++;
    row_heap_.shrink();
    // Received an EOF line, handle in two cases:
    //  - Not the last EOF, then discard and return SUCCESS, enter the message loop to receive the next message
    //  - last EOF, then discard and return ITER_END, by PX clean up execution environment and return ITER_END to upper operator

    if (OB_LIKELY(finish_ch_cnt_ < task_channels_.count())) {
      ret = OB_SUCCESS;
    } else if (get_batch_id() + 1 < get_rescan_param_count()) {
       // this iterate_end needs to be passed to the upper layer
      row_heap_.reuse_heap(task_channels_.count(), alloc_);
      last_pop_row_ = NULL;
    } else if (OB_UNLIKELY(finish_ch_cnt_ > task_channels_.count())) {
      // This branch is a defensive branch
      // All data on all channels has been successfully received
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("All data received. SHOULD NOT see more rows comming",
               "finish_task_cnt", finish_ch_cnt_,
               "total_task_chan_cnt", task_channels_.count(),
               K(ret));
    } else {
      LOG_TRACE("All channel finish", "finish_ch_cnt", finish_ch_cnt_, K(ret));
      all_rows_finish_ = true;
      ret = OB_SUCCESS;
      // Here ret = OB_ITER_END, represents all channels have received, but do not exit the msg_loop loop yet
      // Next, we need to wait for SQC to report the execution status of each task, notify each SQC
      // Release resources, then return ITER_END to the upper-level operator
    }
  } else if (OB_SUCCESS == ret) {
    if (nullptr != last_pop_row_) {
      if (OB_FAIL(last_pop_row_->save_store_row(MY_SPEC.all_exprs_, eval_ctx_, 0))) {
        LOG_WARN("failed to save store row", K(ret));
      } else if (OB_FAIL(row_heap_.push(last_pop_row_))) {
        LOG_WARN("fail push row to heap", K(ret));
      }
    } else {
      ObChunkDatumStore::LastStoredRow *cur_row = nullptr;
      if (OB_FAIL(store_rows_.at(row_heap_.writable_channel_idx(), cur_row))) {
        LOG_WARN("failed to get store row", K(ret), K(row_heap_.writable_channel_idx()));
      } else if (OB_FAIL(cur_row->save_store_row(MY_SPEC.all_exprs_, eval_ctx_, 0))) {
        LOG_WARN("failed to save store row", K(ret));
      } else if (OB_FAIL(row_heap_.push(cur_row))) {
        LOG_WARN("fail push row to heap", K(ret));
      }
    }
  } else {
    LOG_WARN("fail get row from row store", K(ret));
  }
  // (2) Pop the maximum value from the heap
  if (OB_SUCC(ret)) {
    if (0 == row_heap_.capacity()) {
      if (GCONF.enable_sql_audit) {
        op_monitor_info_.otherstat_2_id_ = ObSqlMonitorStatIds::EXCHANGE_EOF_TIMESTAMP;
        op_monitor_info_.otherstat_2_value_ = oceanbase::common::ObClockGenerator::getClock();
      }
      all_rows_finish_ = true;
      metric_.mark_eof();
    } else if (row_heap_.capacity() == row_heap_.count()) {
      const ObChunkDatumStore::LastStoredRow *pop_row = nullptr;
      if (OB_FAIL(row_heap_.pop(pop_row))) {
        LOG_WARN("failed to pop row", K(ret));
      } else if (OB_FAIL(pop_row->store_row_->to_expr(MY_SPEC.all_exprs_, eval_ctx_))) {
        LOG_WARN("failed to to exprs", K(ret));
      } else {
        wait_next_msg = false;
        last_pop_row_ = const_cast<ObChunkDatumStore::LastStoredRow*>(pop_row);
      }
      metric_.count();
      metric_.mark_first_out();
      metric_.set_last_out_ts(::oceanbase::common::ObTimeUtility::current_time());
    } else if (row_heap_.capacity() > row_heap_.count()) {
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid row heap state", K(row_heap_), K(ret));
    }
  }

  LOG_DEBUG("Inner_get_next_row px merge sort coord receive done",
           K(ret),
           "finish_task_cnt", finish_ch_cnt_,
           "total_task_ch_cnt", task_channels_.count(),
           K(wait_next_msg));
  return ret;
}


} // end namespace sql
} // end namespace oceanbase
