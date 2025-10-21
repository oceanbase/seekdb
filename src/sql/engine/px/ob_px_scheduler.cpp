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

#include "sql/engine/px/ob_px_scheduler.h"
#include "sql/engine/px/ob_dfo_scheduler.h"
#include "sql/engine/px/datahub/components/ob_dh_winbuf.h"
#include "sql/engine/join/ob_join_filter_op.h"
#include "src/sql/engine/px/datahub/components/ob_dh_join_filter_count_row.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace omt;
using namespace share::schema;
using namespace sql;
using namespace sql::dtl;
namespace sql
{
// Only used in this cpp file, so it can be placed here
// Dedicated to processing datahub piece messages logic
template <typename PieceMsg>
class ObDhPieceMsgProc
{
public:
  ObDhPieceMsgProc() = default;
  ~ObDhPieceMsgProc() = default;
  int on_piece_msg(ObPxCoordInfo &coord_info, ObExecContext &ctx, const PieceMsg &pkt)
  {
    int ret = OB_SUCCESS;
    ObDfo *source_dfo = nullptr;
    ObDfo *target_dfo = nullptr;
    ObPieceMsgCtx *piece_ctx = nullptr;
    ObDfo *child_dfo = nullptr;
    // FIXME (TODO xiaochu): this dfo id is not necessary, a local mapping from op_id to dfo id can be maintained
    if (OB_FAIL(coord_info.dfo_mgr_.find_dfo_edge(pkt.source_dfo_id_, source_dfo))) {
      LOG_WARN("fail find dfo", K(pkt), K(ret));
    } else if (OB_FAIL(coord_info.dfo_mgr_.find_dfo_edge(pkt.target_dfo_id_, target_dfo))) {
      LOG_WARN("fail find dfo", K(pkt), K(ret));
    } else if (OB_ISNULL(source_dfo) || OB_ISNULL(target_dfo)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr or null session ptr", KP(source_dfo), KP(target_dfo), K(pkt), K(ret));
    } else if (OB_FAIL(coord_info.piece_msg_ctx_mgr_.find_piece_ctx(pkt.op_id_, pkt.type(), piece_ctx))) {
      // If not found then create a ctx
      // NOTE: Here we create a piece_ctx in a way that will not cause concurrency issues,
      // Because QC is a single-threaded message loop, processing messages from SQC one by one
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("fail get ctx", K(pkt), K(ret));
      } else if (OB_FAIL(PieceMsg::PieceMsgCtx::alloc_piece_msg_ctx(pkt, coord_info, ctx,
            source_dfo->get_total_task_count(), piece_ctx))) {
        LOG_WARN("fail to alloc piece msg", K(ret));
      } else if (nullptr != piece_ctx) {
        if (OB_FAIL(coord_info.piece_msg_ctx_mgr_.add_piece_ctx(piece_ctx, pkt.type()))) {
          LOG_WARN("fail add barrier piece ctx", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      typename PieceMsg::PieceMsgCtx *ctx = static_cast<typename PieceMsg::PieceMsgCtx *>(piece_ctx);
      ObIArray<ObPxSqcMeta> &sqcs = target_dfo->get_sqcs();
      if (OB_FAIL(PieceMsg::PieceMsgListener::on_message(*ctx, sqcs, pkt))) {
        LOG_WARN("fail process piece msg", K(pkt), K(ret));
      }
    }
    return ret;
  }
};

int ObPxMsgProc::on_process_end(ObExecContext &ctx)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  // Handle cleanup work
  return ret;
}

// entry function
// Scheduling entry function
int ObPxMsgProc::startup_msg_loop(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("TIMERECORD ",
            "reserve:=-1 name:=QC dfoid:=-1 sqcid:=-1 taskid:=-1 start:",
            ObTimeUtility::current_time());
  if (OB_FAIL(scheduler_->prepare_schedule_info(ctx))) {
    LOG_WARN("fail to prepare schedule info", K(ret));
  } else if (OB_FAIL(scheduler_->init_all_dfo_channel(ctx))) {
    LOG_WARN("fail to init all dfo channel", K(ret));
  } else if (OB_FAIL(scheduler_->try_schedule_next_dfo(ctx))) {
    LOG_WARN("fail to sched next one dfo", K(ret));
  }
  return ret;
}

void ObPxMsgProc::clean_dtl_interm_result(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scheduler_)) {
    // ignore ret
    LOG_WARN("dfo scheduler is null");
  } else {
    scheduler_->clean_dtl_interm_result(ctx);
  }
}
// 1. According to pkt information find corresponding dfo, sqc, mark current sqc thread allocation complete
// 2. Determine if all sqc under this dfo have been assigned threads to complete
//    If completed, mark dfo as thread_inited, enter step 3, otherwise end processing
// 3. Determine if the current dfo's parent is in the thread_inited state,
//    if is:
//       - Call on_dfo_pair_thread_inited to trigger the pairing and distribution of two dfo channels,
//    Otherwise:
//       - Determine if any child of the current dfo is in the thread_inited state,
//          - If so, call on_dfo_pair_thread_inited to trigger the pairing and distribution of channels for two dfos
//          - Otherwise nop
int ObPxMsgProc::on_sqc_init_msg(ObExecContext &ctx, const ObPxInitSqcResultMsg &pkt)
{
  int ret = OB_SUCCESS;

  LOG_TRACE("on_sqc_init_msg", K(pkt));

  ObDfo *edge = NULL;
  ObPxSqcMeta *sqc = NULL;
  if (OB_FAIL(coord_info_.dfo_mgr_.find_dfo_edge(pkt.dfo_id_, edge))) {
    LOG_WARN("fail find dfo", K(pkt), K(ret));
  } else if (OB_ISNULL(edge)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KP(edge), K(ret));
  } else if (OB_FAIL(edge->get_sqc(pkt.sqc_id_, sqc))) {
    LOG_WARN("fail find sqc", K(pkt), K(ret));
  } else if (OB_ISNULL(sqc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KP(sqc), K(ret));
  } else {
    if (OB_SUCCESS != pkt.rc_) {
      ret = pkt.rc_;
      ObPxErrorUtil::update_qc_error_code(coord_info_.first_error_code_,
          pkt.rc_, pkt.err_msg_, sqc->get_exec_addr());
      LOG_WARN("fail init sqc, please check remote server log for details",
               "remote_server", sqc->get_exec_addr(), K(pkt), KP(ret));
    } else if (pkt.task_count_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("task count returned by sqc invalid. expect 1 or more", K(pkt), K(ret));
    } else if (OB_FAIL(sqc->get_partitions_info().assign(pkt.tablets_info_))) {
      LOG_WARN("Failed to assign partitions info", K(ret));
    } else {
      sqc->set_task_count(pkt.task_count_);
      sqc->set_thread_inited(true);
      sqc->set_sqc_order_gi_tasks(pkt.sqc_order_gi_tasks_);
      LOG_TRACE("set sqc support_order_gi_tasks", K(sqc->get_dfo_id()), K(sqc->get_sqc_id()),
                K(pkt.sqc_order_gi_tasks_));
    }
  }

  if (OB_SUCC(ret)) {
    const ObIArray<ObPxSqcMeta> &sqcs = edge->get_sqcs();
    bool sqc_threads_inited = true;
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      const ObPxSqcMeta &sqc = sqcs.at(idx);
      if (!sqc.is_thread_inited()) {
        sqc_threads_inited = false;
        break;
      }
    }
    if (OB_SUCC(ret) && sqc_threads_inited) {
      LOG_TRACE("on_sqc_init_msg: all sqc returned task count. ready to do on_sqc_threads_inited",
                K(*edge));
      edge->set_thread_inited(true);
      ret = scheduler_->on_sqc_threads_inited(ctx, *edge);
    }
  }

  if (OB_SUCC(ret)) {
    if (edge->is_thread_inited()) {

      /* Try to notify both parent and child at the same time, consider the situation:
       *
       *  parent (thread inited)
       *    |
       *   self
       *    |
       *  child  (thread inited)
       *
       *  In this case, before the self thread is fully scheduled,
       *  the thread inited messages from child and parent cannot
       *  trigger the on_dfo_pair_thread_inited event.
       *  After self's thread inited, the on_dfo_pair_thread_inited event
       *  must be triggered for both parent and child simultaneously.
       */
      // Attempt to schedule self-parent pair
      if (edge->has_parent() && edge->parent()->is_thread_inited()) {
        if (OB_FAIL(on_dfo_pair_thread_inited(ctx, *edge, *edge->parent()))) {
          LOG_WARN("fail co-schedule parent-edge", K(ret));
        }
      }
      // Attempt to schedule self-child pair
      if (OB_SUCC(ret)) {
        int64_t cnt = edge->get_child_count();
        for (int64_t idx = 0; idx < cnt && OB_SUCC(ret); ++idx) {
          ObDfo *child= NULL;
          if (OB_FAIL(edge->get_child_dfo(idx, child))) {
            LOG_WARN("fail get child dfo", K(idx), K(cnt), K(ret));
          } else if (OB_ISNULL(child)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL unexpected", K(ret));
          } else if (child->is_thread_inited()) {
            if (OB_FAIL(on_dfo_pair_thread_inited(ctx, *child, *edge))) {
              LOG_WARN("fail co-schedule edge-child", K(ret));
            }
          }
        }
      }
    }
  }

  return ret;
}
// 1. According to pkt information find dfo, sqc, mark current sqc as completed
// 2. Determine if all sqc under this dfo have been executed
//    If completed, mark dfo as thread_finish
// 3. Determine if the current dfo is marked as thread_finish status,
//    if it is:
//      - Send release thread message to dfo
//      - schedule next dfo
//        - If all dfo have been scheduled (excluding Coord), nop
//    Otherwise:
//      - nop
int ObPxMsgProc::on_sqc_finish_msg(ObExecContext &ctx,
                                   const ObPxFinishSqcResultMsg &pkt)
{
  int ret = OB_SUCCESS;
  ObDfo *edge = NULL;
  ObPxSqcMeta *sqc = NULL;
  if (OB_FAIL(coord_info_.dfo_mgr_.find_dfo_edge(pkt.dfo_id_, edge))) {
    LOG_WARN("fail find dfo", K(pkt), K(ret));
  } else if (OB_ISNULL(edge)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(pkt), K(ret));
  } else if (OB_FAIL(edge->get_sqc(pkt.sqc_id_, sqc))) {
    LOG_WARN("fail find sqc", K(pkt), K(ret));
  } else if (OB_ISNULL(sqc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(pkt), K(ret));
  } else if (sqc->is_thread_finish()) {
    // For virtual tables, if both the mocked SQC finish message and the real SQC finish message are
    // processed by the QC, we should skip the processing of the finish message that arrives later.
  } else if (OB_FAIL(process_sqc_finish_msg_once(ctx, pkt, sqc, edge))) {
    LOG_WARN("failed to process_sqc_finish_msg_once");
  }
  return ret;
}

void ObPxMsgProc::log_warn_sqc_fail(int ret, const ObPxFinishSqcResultMsg &pkt, ObPxSqcMeta *sqc)
{
  // Do not change the follow log about px_obdiag_sqc_addr, becacue it will use in obdiag tool
  LOG_WARN("sqc fail, abort qc", K(pkt), K(ret), "px_obdiag_sqc_addr", sqc->get_exec_addr());
}

int ObPxMsgProc::process_sqc_finish_msg_once(ObExecContext &ctx, const ObPxFinishSqcResultMsg &pkt,
                                        ObPxSqcMeta *sqc, ObDfo *edge)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = NULL;
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  if (OB_ISNULL(session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr session", K(ret));
  } else if (OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("phy plan ctx NULL", K(ret));
  } else if (OB_FAIL(ctx.get_feedback_info().merge_feedback_info(pkt.fb_info_))) {
    LOG_WARN("fail to merge feedback info", K(ret));
  } else if (OB_ISNULL(session->get_tx_desc())) {
  } else if (OB_FAIL(MTL(transaction::ObTransService*)
                    ->add_tx_exec_result(*session->get_tx_desc(),
                                          pkt.get_trans_result()))) {
    LOG_WARN("fail merge result", K(ret),
             "packet_trans_result", pkt.get_trans_result(),
             "tx_desc", *session->get_tx_desc());
  } else if (pkt.get_trans_result().get_touched_ls().count() > 0
             && OB_FAIL(session->get_trans_result()
                        .add_touched_ls(pkt.get_trans_result().get_touched_ls()))) {
    LOG_WARN("fail add touched ls for tx", K(ret),
             "touched_ls", pkt.get_trans_result().get_touched_ls());
  } else {
    LOG_TRACE("on_sqc_finish_msg trans_result",
              "packet_trans_result", pkt.get_trans_result(),
              "tx_desc", *session->get_tx_desc(),
              "tx_result", session->get_trans_result());
  }
  if (OB_FAIL(ret)) {
  } else if (common::OB_INVALID_ID != pkt.temp_table_id_) {
    if (OB_FAIL(ctx.add_temp_table_interm_result_ids(pkt.temp_table_id_,
                                                     sqc->get_exec_addr(),
                                                     pkt.interm_result_ids_))) {
      LOG_WARN("failed to add temp table interm result ids.", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    sqc->set_need_report(false);
    sqc->set_thread_finish(true);
    // process for virtual table, mock eof buffer let px exit msg loop
    if (sqc->is_ignore_vtable_error() && OB_SUCCESS != pkt.rc_
        && ObVirtualTableErrorWhitelist::should_ignore_vtable_error(pkt.rc_)) {
       // If a sqc finish message is received, if the sqc involves a virtual table, all error codes need to be ignored
       // If this dfo is a child_dfo of root_dfo, to allow px to exit the message loop of the data channel
       // Need to mock an eof dtl buffer local send to px (actual not via rpc, attach only)
      const_cast<ObPxFinishSqcResultMsg &>(pkt).rc_ = OB_SUCCESS;
      OZ(root_dfo_action_.notify_peers_mock_eof(edge,
          phy_plan_ctx->get_timeout_timestamp(),
          sqc->get_exec_addr()));
    }
    NG_TRACE_EXT(sqc_finish,
                 OB_ID(dfo_id), sqc->get_dfo_id(),
                 OB_ID(sqc_id), sqc->get_sqc_id());

    LOG_TRACE("[MSG] sqc finish", K(*edge), K(*sqc));
    LOG_TRACE("on_sqc_finish_msg update feedback info",
        K(pkt.fb_info_), K(ctx.get_feedback_info()));
  }
  // mark dfo finished if all sqcs in this dfo is finished
  if (OB_SUCC(ret)) {
    ObIArray<ObPxSqcMeta> &sqcs = edge->get_sqcs();
    bool sqc_threads_finish = true;
    int64_t dfo_used_worker_count = 0;
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      ObPxSqcMeta &sqc = sqcs.at(idx);
      if (!sqc.is_thread_finish()) {
        sqc_threads_finish = false;
        break;
      } else {
        // the value 1 is accounted for sqc thread
        dfo_used_worker_count += sqc.get_task_count();
      }
    }
    if (OB_SUCC(ret) && sqc_threads_finish) {
      edge->set_thread_finish(true);
      edge->set_used_worker_count(dfo_used_worker_count);
      LOG_TRACE("[MSG] dfo finish", K(*edge));
    }
  }

  /**
   * Why check the error code here? Because we need to update the status of this failed sqc and dfo,
   * the sqc (including its worker) that sent this finish message has already ended, and we need to update it.
   * However, because an error occurred, the subsequent scheduling process does not need to continue, and the later process will handle the error.
   */
  ObPxErrorUtil::update_qc_error_code(coord_info_.first_error_code_,
      pkt.rc_, pkt.err_msg_, sqc->get_exec_addr());
  if (OB_SUCC(ret)) {
    if (OB_FAIL(pkt.rc_)) {
      DAS_CTX(ctx).get_location_router().save_cur_exec_status(pkt.rc_);
      log_warn_sqc_fail(ret, pkt, sqc);
    } else {
      // pkt rc_ == OB_SUCCESS
      // Process dml + px framework affected row
      DAS_CTX(ctx).get_location_router().save_cur_exec_status(pkt.das_retry_rc_);
      if (OB_ISNULL(ctx.get_physical_plan_ctx())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("phy plan ctx is null", K(ret));
      } else  {
        ctx.get_physical_plan_ctx()->add_affected_rows(pkt.sqc_affected_rows_);
        ctx.get_physical_plan_ctx()->add_px_dml_row_info(pkt.dml_row_info_);
        ctx.get_physical_plan_ctx()->add_total_memstore_read_row_count(
          pkt.sqc_memstore_row_read_count_);
        ctx.get_physical_plan_ctx()->add_total_ssstore_read_row_count(
          pkt.sqc_ssstore_row_read_count_);
      }
    }
  }

  // schedule_next_dfo if this dfo is finished.
  if (OB_SUCC(ret)) {
    if (edge->is_thread_finish()) {
      ret = scheduler_->try_schedule_next_dfo(ctx);
      if (OB_ITER_END == ret) {
        coord_info_.all_threads_finish_ = true;
        LOG_TRACE("TIMERECORD ",
                  "reserve:=-1 name:=QC dfoid:=-1 sqcid:=-1 taskid:=-1 end:",
                  ObTimeUtility::current_time());
        ret = OB_SUCCESS; // need to override, otherwise cannot break out of loop
      }
    }
  }

  return ret;
}

int ObPxMsgProc::on_piece_msg(
    ObExecContext &ctx,
    const ObBarrierPieceMsg &pkt)
{
  ObDhPieceMsgProc<ObBarrierPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(
    ObExecContext &ctx,
    const ObWinbufPieceMsg &pkt)
{
  ObDhPieceMsgProc<ObWinbufPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(
    ObExecContext &ctx,
    const ObDynamicSamplePieceMsg &pkt)
{
  ObDhPieceMsgProc<ObDynamicSamplePieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(
    ObExecContext &ctx,
    const ObRollupKeyPieceMsg &pkt)
{
  ObDhPieceMsgProc<ObRollupKeyPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(
    ObExecContext &ctx,
    const ObRDWFPieceMsg &pkt)
{
  ObDhPieceMsgProc<ObRDWFPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(
    ObExecContext &ctx,
    const ObInitChannelPieceMsg &pkt)
{
  ObDhPieceMsgProc<ObInitChannelPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(
    ObExecContext &ctx,
    const ObReportingWFPieceMsg &pkt)
{
  ObDhPieceMsgProc<ObReportingWFPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(
    ObExecContext &ctx,
    const ObOptStatsGatherPieceMsg &pkt)
{
  ObDhPieceMsgProc<ObOptStatsGatherPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(ObExecContext &ctx, const SPWinFuncPXPieceMsg &pkt)
{
  ObDhPieceMsgProc<SPWinFuncPXPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(ObExecContext &ctx, const RDWinFuncPXPieceMsg &pkt)
{
  ObDhPieceMsgProc<RDWinFuncPXPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_piece_msg(ObExecContext &ctx, const ObJoinFilterCountRowPieceMsg &pkt)
{
  ObDhPieceMsgProc<ObJoinFilterCountRowPieceMsg> proc;
  return proc.on_piece_msg(coord_info_, ctx, pkt);
}

int ObPxMsgProc::on_eof_row(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  // 1. mark some channel as eof
  // 2. see if all PX data channel eof, if yes, return OB_ITER_END
  return ret;
}

int ObPxMsgProc::on_dfo_pair_thread_inited(ObExecContext &ctx, ObDfo &child, ObDfo &parent)
{
  int ret = OB_SUCCESS;
  // Position allocation is done, establish DTL mapping
  //
  // NOTE: Here we temporarily simplify the implementation, if child/parent thread allocation fails
  // Then fail to exit. More detailed implementation can modify the number of thread allocations on different servers,
  // Then reattempt to allocate threads. In this scenario, the DTL Map also needs to change
  //
  if (OB_SUCC(ret)) {
    if (OB_FAIL(scheduler_->build_data_xchg_ch(ctx, child, parent))) {
      LOG_WARN("fail init dtl data channel", K(ret));
    } else {
      LOG_TRACE("build data xchange channel for dfo pair ok", K(parent), K(child));
    }
  }
  // Distribute dtl channel information to parent, child two DFOs, so that they can start sending and receiving data
  if (OB_SUCC(ret)) {
    if (OB_FAIL(scheduler_->dispatch_dtl_data_channel_info(ctx, child, parent))) {
      LOG_WARN("fail setup dtl data channel for child-parent pair", K(ret));
    } else {
      LOG_TRACE("dispatch dtl data channel for pair ok", K(parent), K(child));
    }
  }

  return ret;
}

int ObPxMsgProc::on_interrupted(ObExecContext &ctx, const ObInterruptCode &ic)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  // override ret code
  // Throw error code to main processing routine
  ret = ic.code_;
  LOG_TRACE("qc received a interrupt and throw out of msg proc", K(ic), K(ret));
  return ret;
}

// TODO
int ObPxMsgProc::on_sqc_init_fail(ObDfo &dfo, ObPxSqcMeta &sqc)
{
  int ret = OB_SUCCESS;
  UNUSED(dfo);
  UNUSED(sqc);
  return ret;
}

int ObPxTerminateMsgProc::on_sqc_init_msg(ObExecContext &ctx, const ObPxInitSqcResultMsg &pkt)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  ObDfo *edge = NULL;
  ObPxSqcMeta *sqc = NULL;
  /**
   * Mark sqc, dfo as already started.
   */
  LOG_TRACE("terminate msg proc on sqc init msg", K(pkt.rc_));
  if (pkt.task_count_ <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task count returned by sqc invalid. expect 1 or more", K(pkt), K(ret));
  } else if (OB_FAIL(coord_info_.dfo_mgr_.find_dfo_edge(pkt.dfo_id_, edge))) {
    LOG_WARN("fail find dfo", K(pkt), K(ret));
  } else if (OB_ISNULL(edge)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KP(edge), K(ret));
  } else if (OB_FAIL(edge->get_sqc(pkt.sqc_id_, sqc))) {
    LOG_WARN("fail find sqc", K(pkt), K(ret));
  } else if (OB_ISNULL(sqc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KP(sqc), K(ret));
  } else {
    sqc->set_task_count(pkt.task_count_);
    // Mark sqc has been fully started
    sqc->set_thread_inited(true);

    if (pkt.rc_ != OB_SUCCESS) {
      LOG_DEBUG("receive error code from sqc init msg", K(coord_info_.first_error_code_), K(pkt.rc_));
    }
    ObPxErrorUtil::update_qc_error_code(coord_info_.first_error_code_,
        pkt.rc_, pkt.err_msg_, sqc->get_exec_addr());
  }

  if (OB_SUCC(ret)) {
    const ObIArray<ObPxSqcMeta> &sqcs = edge->get_sqcs();
    bool sqc_threads_inited = true;
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      const ObPxSqcMeta &sqc = sqcs.at(idx);
      if (!sqc.is_thread_inited()) {
        sqc_threads_inited = false;
        break;
      }
    }
    if (OB_SUCC(ret) && sqc_threads_inited) {
      LOG_TRACE("sqc terminate msg: all sqc returned task count. ready to do on_sqc_threads_inited",
                K(*edge));
      // Mark dfo as fully started
      edge->set_thread_inited(true);
    }
  }

  return ret;
}

int ObPxTerminateMsgProc::on_sqc_finish_msg(ObExecContext &ctx, const ObPxFinishSqcResultMsg &pkt)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("terminate msg : proc on sqc finish msg", K(pkt.rc_));
  ObDfo *edge = NULL;
  ObPxSqcMeta *sqc = NULL;
  ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(session = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr session", K(ret));
  } else if (OB_FAIL(ctx.get_feedback_info().merge_feedback_info(pkt.fb_info_))) {
    LOG_WARN("fail to merge feedback info", K(ret));
  } else if (OB_ISNULL(session->get_tx_desc())) {
  } else if (OB_FAIL(MTL(transaction::ObTransService*)
                     ->add_tx_exec_result(*session->get_tx_desc(),
                                          pkt.get_trans_result()))) {
    LOG_WARN("fail report tx result", K(ret),
             "packet_trans_result", pkt.get_trans_result(),
             "tx_desc", *session->get_tx_desc());
  } else if (pkt.get_trans_result().get_touched_ls().count() > 0
             && OB_FAIL(session->get_trans_result()
                        .add_touched_ls(pkt.get_trans_result().get_touched_ls()))) {
    LOG_WARN("fail add touched ls for tx", K(ret),
             "touched_ls", pkt.get_trans_result().get_touched_ls());
  } else {
    LOG_TRACE("on_sqc_finish_msg trans_result",
              "packet_trans_result", pkt.get_trans_result(),
              "tx_desc", *session->get_tx_desc());
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(coord_info_.dfo_mgr_.find_dfo_edge(pkt.dfo_id_, edge))) {
    LOG_WARN("fail find dfo", K(pkt), K(ret));
  } else if (OB_FAIL(edge->get_sqc(pkt.sqc_id_, sqc))) {
    LOG_WARN("fail find sqc", K(pkt), K(ret));
  } else if (OB_ISNULL(edge) || OB_ISNULL(sqc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KP(edge), KP(sqc), K(ret));
  } else if (FALSE_IT(sqc->set_need_report(false))) {
  } else {
    sqc->set_thread_finish(true);
    if (pkt.rc_ != OB_SUCCESS) {
      LOG_DEBUG("receive error code from sqc finish msg", K(coord_info_.first_error_code_), K(pkt.rc_));
    }
    ObPxErrorUtil::update_qc_error_code(coord_info_.first_error_code_,
        pkt.rc_, pkt.err_msg_, sqc->get_exec_addr());

    NG_TRACE_EXT(sqc_finish,
                 OB_ID(dfo_id), sqc->get_dfo_id(),
                 OB_ID(sqc_id), sqc->get_sqc_id());

    LOG_TRACE("terminate msg : sqc finish", K(*edge), K(*sqc));
    LOG_TRACE("on_sqc_finish_msg update feedback info",
        K(pkt.fb_info_), K(ctx.get_feedback_info()));
  }

  if (OB_SUCC(ret)) {
   const ObIArray<ObPxSqcMeta> &sqcs = edge->get_sqcs();
    bool sqc_threads_finish = true;
    int64_t dfo_used_worker_count = 0;
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      const ObPxSqcMeta &sqc = sqcs.at(idx);
      if (!sqc.is_thread_finish()) {
        sqc_threads_finish = false;
        break;
      } else {
        // the value 1 is accounted for sqc thread
        dfo_used_worker_count += sqc.get_task_count();
      }
    }
    if (OB_SUCC(ret) && sqc_threads_finish) {
      edge->set_thread_finish(true);
      edge->set_used_worker_count(dfo_used_worker_count);
      LOG_TRACE("terminate msg : dfo finish", K(*edge));
    }
  }

  return ret;
}

int ObPxTerminateMsgProc::on_eof_row(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  LOG_WARN("terminate msg proc on sqc eof msg", K(ret));
  return ret;
}

int ObPxTerminateMsgProc::on_sqc_init_fail(ObDfo &dfo, ObPxSqcMeta &sqc)
{
  int ret = OB_SUCCESS;
  UNUSED(dfo);
  UNUSED(sqc);
  LOG_WARN("terminate msg proc on sqc init fail", K(ret));
  return ret;
}

int ObPxTerminateMsgProc::on_interrupted(ObExecContext &ctx, const common::ObInterruptCode &pkt)
{
  int ret = OB_SUCCESS;
  UNUSED(pkt);
  UNUSED(ctx);
  // Already in the recycling process, no longer respond to interrupts.
  LOG_WARN("terminate msg proc on sqc interrupted", K(ret));
  return ret;
}


int RuntimeFilterDependencyInfo::describe_dependency(ObDfo *root_dfo)
{
  int ret = OB_SUCCESS;
  // for each rf create op, find its pair rf use op,
  // then get the lowest common ancestor of them, mark force_bushy of the dfo which the ancestor belongs to.
  for (int64_t i = 0; i < rf_create_ops_.count() && OB_SUCC(ret); ++i) {
    const ObJoinFilterSpec *create_op = static_cast<const ObJoinFilterSpec *>(rf_create_ops_.at(i));
    for (int64_t j = 0; j < rf_use_ops_.count() && OB_SUCC(ret); ++j) {
      int64_t use_filter_id = common::OB_INVALID_ID;
      if (IS_PX_GI(rf_use_ops_.at(j)->get_type())) {
        const ObGranuleIteratorSpec *use_op =
            static_cast<const ObGranuleIteratorSpec *>(rf_use_ops_.at(j));
        use_filter_id = use_op->bf_info_.filter_id_;
      } else {
        const ObJoinFilterSpec *use_op = static_cast<const ObJoinFilterSpec *>(rf_use_ops_.at(j));
        use_filter_id = use_op->get_filter_id();
      }
      if (create_op->get_filter_id() == use_filter_id) {
        const ObOpSpec *ancestor_op = nullptr;
        ObDfo *op_dfo = nullptr;;
        if (OB_FAIL(LowestCommonAncestorFinder::find_op_common_ancestor(
            create_op, rf_use_ops_.at(j), ancestor_op))) {
          LOG_WARN("failed to find op common ancestor");
        } else if (OB_ISNULL(ancestor_op)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("op common ancestor not found");
        } else if (OB_FAIL(LowestCommonAncestorFinder::get_op_dfo(ancestor_op, root_dfo, op_dfo))) {
          LOG_WARN("failed to find op common ancestor");
        } else if (OB_ISNULL(op_dfo)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("the dfo of ancestor_op not found");
        } else {
          // Once the DFO which the ancestor belongs to has set the flag "force_bushy",
          // the DfoTreeNormalizer will not attempt to transform a right-deep DFO tree
          // into a left-deep DFO tree. Consequently, the "join filter create" operator
          // can be scheduled earlier than the "join filter use" operator.
          op_dfo->set_force_bushy(true);
        }
        break;
      }
    }
  }
  return ret;
}

int ObPxCoordInfo::init()
{
  int ret = OB_SUCCESS;
  const int64_t bucket_num = 32;
  if (OB_FAIL(p2p_dfo_map_.create(bucket_num,
      "PxDfoMapKey",
      "PxDfoMapNode"))) {
    LOG_WARN("create hash table failed", K(ret));
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
