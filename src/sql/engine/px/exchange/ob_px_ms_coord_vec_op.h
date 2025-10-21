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

#ifndef OCEANBASE_ENGINE_PX_EXCHANGE_OB_PX_MS_COORD_VEC_OP_H_
#define OCEANBASE_ENGINE_PX_EXCHANGE_OB_PX_MS_COORD_VEC_OP_H_

#include "sql/engine/px/ob_px_coord_op.h"
#include "sql/engine/px/ob_dfo_mgr.h"
#include "sql/engine/px/ob_px_rpc_proxy.h"
#include "sql/engine/px/ob_px_data_ch_provider.h"
#include "sql/engine/px/exchange/ob_row_heap.h"
#include "sql/engine/px/ob_px_dtl_proc.h"
#include "sql/engine/px/ob_px_coord_msg_proc.h"
#include "sql/dtl/ob_dtl_channel_loop.h"
#include "sql/engine/px/ob_px_scheduler.h"
#include "sql/engine/px/ob_dfo_scheduler.h"
#include "sql/engine/px/datahub/components/ob_dh_barrier.h"
#include "sql/engine/px/datahub/components/ob_dh_winbuf.h"
#include "sql/engine/px/datahub/components/ob_dh_rollup_key.h"
#include "sql/engine/px/datahub/components/ob_dh_sample.h"
#include "lib/container/ob_iarray.h"
#include "sql/engine/px/datahub/components/ob_dh_init_channel.h"
#include "sql/engine/px/datahub/components/ob_dh_second_stage_reporting_wf.h"
#include "sql/engine/basic/ob_compact_row.h"
#include "sql/engine/basic/ob_temp_row_store.h"
#include "sql/engine/px/datahub/components/ob_dh_join_filter_count_row.h"

namespace oceanbase
{
namespace sql
{

class ObPxMSCoordVecOpInput : public ObPxReceiveOpInput
{
public:
  OB_UNIS_VERSION_V(1);
public:
  ObPxMSCoordVecOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObPxReceiveOpInput(ctx, spec)
  {}
  virtual ~ObPxMSCoordVecOpInput()
  {}
};

class ObPxMSCoordVecSpec : public ObPxCoordSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObPxMSCoordVecSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
  : ObPxCoordSpec(alloc, type),
    all_exprs_(alloc),
    sort_collations_(alloc),
    sort_cmp_funs_(alloc)
  {}
  ~ObPxMSCoordVecSpec() {}
  virtual const common::ObIArray<ObExpr *> *get_all_exprs() const override { return &all_exprs_; }

  ExprFixedArray all_exprs_;
  ObSortCollations sort_collations_;
  ObSortFuncs sort_cmp_funs_;
};

class ObPxMSCoordVecOp : public ObPxCoordOp
{
public:
  ObPxMSCoordVecOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);
  virtual ~ObPxMSCoordVecOp() {}
public:
  class ObPxMSCoordVecOpEventListener : public ObIPxCoordEventListener
  {
  public:
    ObPxMSCoordVecOpEventListener(ObPxMSCoordVecOp &px_coord_op)
    : px_coord_op_(px_coord_op)
    {}
    int on_root_data_channel_setup();
  private:
      ObPxMSCoordVecOp &px_coord_op_;
  };
  class ObMsgReceiveFilter : public dtl::ObIDltChannelLoopPred
  {
  public:
    ObMsgReceiveFilter(ObRowHeap<ObLastCompactRowCompare, LastCompactRow> &heap)
        : data_ch_idx_start_(-1), data_ch_idx_end_(-1), heap_(heap) {}
    ~ObMsgReceiveFilter() = default;
    // idx range range left closed right open: [start_idx, end_idx)
    void set_data_channel_idx_range(int64_t start_idx, int64_t end_idx)
    {
      data_ch_idx_start_ = start_idx;
      data_ch_idx_end_ = end_idx;
    }
    bool pred_process(int64_t ch_idx, dtl::ObDtlChannel *ch) override
    {
      UNUSED(ch);
      // NOTE: The control information channel creation time for multiple DFOs is different, some may be scheduled later than the ROOT DFO
      //       so heap's range may be a segment in the middle
      return (-1 == data_ch_idx_start_) || /* Haven't received ROOT DFO data yet, only accepting control messages phase */
          (ch_idx < data_ch_idx_start_) || /* control message */
          (ch_idx >= data_ch_idx_end_)  || /* control message */
          (heap_.writable_channel_idx() + data_ch_idx_start_ == ch_idx); /* expected data message */
    }
    OB_INLINE int64_t get_data_channel_start_idx() { return data_ch_idx_start_; }
  private:
    int64_t data_ch_idx_start_;
    int64_t data_ch_idx_end_;
    ObRowHeap<ObLastCompactRowCompare, LastCompactRow> &heap_;
  };
public:
  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual void destroy() override;
  virtual int inner_close() override;
  virtual int inner_get_next_row() override {
    return next_row(false);
  }
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override;

  virtual ObIPxCoordEventListener &get_listenner() override { return listener_; }
  int init_row_heap(int64_t n_ways);

  // initialize readers after receive channel root DFO.
  virtual int receive_channel_root_dfo(ObExecContext &ctx,
                                       ObDfo &parent,
                                       ObPxTaskChSets &parent_ch_sets) override;
  // initialize readers after receive channel root DFO.
  virtual int receive_channel_root_dfo(ObExecContext &ctx,
                                       ObDfo &parent,
                                       dtl::ObDtlChTotalInfo &ch_info) override;
  void reset_finish_ch_cnt() { finish_ch_cnt_ = 0; }
  void reset_readers();
  void reuse_heap() {
    row_heap_.reuse_heap(task_channels_.count(), alloc_);
    last_pop_row_ = NULL;
  }
private:
  virtual int free_allocator();
  
  int next_row(const bool need_store_output);
  // fetch row from reader
  int next_row_from_heap(ObReceiveRowReader &reader, bool &wait_next_msg, const bool need_store_output);
  virtual int setup_loop_proc() override;
  int init_store_rows(int64_t n_ways);
  int setup_readers();
  void destroy_readers();
  virtual void clean_dfos_dtl_interm_result() override
  {
    msg_proc_.clean_dtl_interm_result(ctx_);
  }
private:
  ObPxMSCoordVecOpEventListener listener_;
  ObSerialDfoScheduler serial_scheduler_;
  ObParallelDfoScheduler parallel_scheduler_;
  ObPxMsgProc msg_proc_; // msg_loop processing message callback function
  ObPxFinishSqcResultP sqc_finish_msg_proc_;
  ObPxInitSqcResultP sqc_init_msg_proc_;
  ObBarrierPieceMsgP barrier_piece_msg_proc_;
  ObWinbufPieceMsgP winbuf_piece_msg_proc_;
  ObPxQcInterruptedP interrupt_proc_;
  ObDynamicSamplePieceMsgP sample_piece_msg_proc_;
  ObRollupKeyPieceMsgP rollup_key_piece_msg_proc_;
  ObRDWFPieceMsgP rd_wf_piece_msg_proc_;
  ObInitChannelPieceMsgP init_channel_piece_msg_proc_;
  ObReportingWFPieceMsgP reporting_wf_piece_msg_proc_;
  ObOptStatsGatherPieceMsgP opt_stats_gather_piece_msg_proc_;
  ObRDWinFuncPXPieceMsgP rd_winfunc_px_piece_msg_proc_;
  ObSPWinFuncPXPieceMsgP sp_winfunc_px_piece_msg_proc_;
  ObJoinFilterCountRowPieceMsgP join_filter_count_row_piece_msg_proc_;
  // Store the current row of each way in merge sort
  ObArray<LastCompactRow *> store_rows_;
  LastCompactRow *last_pop_row_;
  // row_heap and receive_order are only used in scenarios for ordered data reception
  // Here the maximum memory approach is used to avoid allocating memory for each line
  ObRowHeap<ObLastCompactRowCompare, LastCompactRow> row_heap_;
  ObMsgReceiveFilter receive_order_;

  int64_t finish_ch_cnt_;
  bool all_rows_finish_;
  ObReceiveRowReader *readers_;
  int64_t reader_cnt_;
  common::ObArenaAllocator alloc_;
  ObBatchRows single_row_brs_;
  ObTempRowStore output_store_;
  ObTempRowStore::Iterator output_iter_;
};


} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_ENGINE_PX_EXCHANGE_OB_PX_FIFO_COORD_VEC_OP_H_
