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
#include "sql/dtl/ob_dtl_channel_group.h"
#include "share/rc/ob_server_runtime.h"
#include "sql/engine/px/ob_dfo_scheduler.h"
#include "sql/engine/px/ob_px_local_sqc_launcher.h"
#include "ob_px_coord_op.h"
#include "sql/engine/px/ob_px_util.h"
#include "sql/dtl/ob_dtl_interm_result_manager.h"
#include "share/ob_ex_rpc.h"
#include "sql/monitor/show_trace/ob_show_trace.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;

ObDfoSchedulerBasic::ObDfoSchedulerBasic(ObPxCoordInfo &coord_info,
                      ObPxRootDfoAction &root_dfo_action,
                      ObIPxCoordEventListener &listener)
  : coord_info_(coord_info),
    root_dfo_action_(root_dfo_action),
    listener_(listener)
{
}

int ObDfoSchedulerBasic::dispatch_root_dfo_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const
{
  int ret = OB_SUCCESS;
  ObPxTaskChSets parent_ch_sets;
  int64_t child_dfo_id = child.get_dfo_id();
  if (parent.is_root_dfo()) {
    ObDtlChTotalInfo *ch_info = nullptr;
    if (OB_FAIL(child.get_dfo_ch_info(0, ch_info))) {
      LOG_WARN("failed to get task receive chs", K(ret));
    } else if (!parent.check_root_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr", K(parent), K(ret));
    } else if (OB_FAIL(root_dfo_action_.receive_channel_root_dfo(ctx, parent, *ch_info))) {
      LOG_WARN("failed to receive channel for root dfo", K(ret));
    }
  }
  return ret;
}

int ObDfoSchedulerBasic::init_all_dfo_channel(ObExecContext &ctx) const
{
  int ret = OB_SUCCESS;
  /*do nothings*/
  UNUSED(ctx);
  return ret;
}

int ObDfoSchedulerBasic::on_sqc_threads_inited(ObExecContext &ctx, ObDfo &dfo) const
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  ObShowTraceSessionBuffer *trace_buf = OB_ISNULL(session) ? NULL : session->get_show_trace_buffer();
  if (!dfo.is_root_dfo() && OB_NOT_NULL(trace_buf) && trace_buf->is_recording()) {
    ObIArray<ObPxSqcMeta> &sqcs = dfo.get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, true) {
      const ObPxSqcMeta &sqc = sqcs.at(idx);
      ObTraceSpanGuard sqc_guard(session, TRACE_PX_SQC);
      sqc_guard.set_tag(TRACE_TAG_DFO_ID, dfo.get_dfo_id());
      sqc_guard.set_tag(TRACE_TAG_SQC_ID, sqc.get_sqc_id());
      sqc_guard.set_tag(TRACE_TAG_QC_ID, dfo.get_qc_id());
      sqc_guard.set_tag(TRACE_TAG_TASK_COUNT, sqc.get_task_count());
      sqc_guard.set_tag(TRACE_TAG_RET_CODE, OB_SUCCESS);
    }
  }
  if (OB_FAIL(dfo.prepare_channel_info())) {
    LOG_WARN("failed to prepare channel info", K(ret));
  }
  LOG_TRACE("on_sqc_threads_inited: dfo data xchg ch allocated", K(ret));
  return ret;
}
// Build the m * n in-process worker shuffle.
int ObDfoSchedulerBasic::build_data_mn_xchg_ch(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const
{
  int ret = OB_SUCCESS;
  bool is_slave_mapping = (parent.is_in_slave_mapping() && child.is_out_slave_mapping());
  ObPQDistributeMethod::Type child_dist_method = child.get_dist_method();
  if (is_slave_mapping) {
    // build channel for slave mapping scenes
    if (OB_FAIL(ObSlaveMapUtil::build_slave_mapping_mn_ch_map(ctx, child, parent))) {
      LOG_WARN("failed to build slave mapping mn channel map");
    }
  } else if (IS_PKEY_DIST_METHOD(child_dist_method)) {
    // build channel for pkey related scenes, e.g. pdml && pkey
    if (OB_FAIL(ObSlaveMapUtil::build_pkey_mn_ch_map(ctx, child, parent))) {
      LOG_WARN("failed to build partition mn channel map");
    }
  } else {
    // build channel for other normal scenes
    int64_t child_dfo_idx = -1;
    ObPxChTotalInfos *transmit_mn_ch_info = &child.get_dfo_ch_total_infos();
    if (OB_FAIL(ObDfo::check_dfo_pair(parent, child, child_dfo_idx))) {
      LOG_WARN("failed to check dfo pair", K(ret));
    } else if (OB_FAIL(ObSlaveMapUtil::build_mn_channel(transmit_mn_ch_info, child, parent))) {
      LOG_WARN("failed to build mn channel");
    }
  }
  return ret;
}

int ObDfoSchedulerBasic::build_data_xchg_ch(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const
{
  int ret = OB_SUCCESS;
  ret = build_data_mn_xchg_ch(ctx, child, parent);
  return ret;
}

int ObDfoSchedulerBasic::dispatch_receive_channel_info_via_sqc(ObExecContext &ctx,
                                                                       ObDfo &child,
                                                                       ObDfo &parent,
                                                                       bool is_parallel_scheduler) const
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  ObPxTaskChSets parent_ch_sets;
  int64_t child_dfo_id = child.get_dfo_id();
  if (parent.is_root_dfo()) {
    if (OB_FAIL(dispatch_root_dfo_channel_info(ctx, child, parent))) {
      LOG_WARN("fail dispatch root dfo receive channel info", K(ret), K(parent), K(child));
    }
  } else {
    // Split receive channels sets by sqc dimension and send to each SQC
    ObIArray<ObPxSqcMeta> &sqcs = parent.get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      int64_t sqc_id = sqcs.at(idx).get_sqc_id();
      ObPxReceiveDataChannelMsg &receive_data_channel_msg = sqcs.at(idx).get_receive_channel_msg();
      if (OB_INVALID_INDEX == sqc_id) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected param", K(sqc_id), K(ret));
      } else {
        ObDtlChTotalInfo *ch_info = nullptr;
        if (OB_FAIL(child.get_dfo_ch_info(idx, ch_info))) {
          LOG_WARN("failed to get task receive chs", K(ret));
        } else if (OB_FAIL(receive_data_channel_msg.set_payload(child_dfo_id, *ch_info))) {
          LOG_WARN("fail init msg", K(ret));
        } else if (!receive_data_channel_msg.is_valid()) {
          LOG_WARN("receive data channel msg is not valid", K(ret));
        } else if (!is_parallel_scheduler &&
            OB_FAIL(sqcs.at(idx).add_serial_recieve_channel(receive_data_channel_msg))) {
          LOG_WARN("fail to add recieve channel", K(ret), K(receive_data_channel_msg));
        } else {
          LOG_TRACE("ObPxCoord::MsgProc::dispatch_receive_channel_info_via_sqc done.",
                    K(idx), K(cnt), K(sqc_id), K(child_dfo_id), K(parent_ch_sets));
        }
      }
    }
  }
  return ret;
}

int ObDfoSchedulerBasic::set_temp_table_ctx_for_sqc(ObExecContext &ctx,
                                                    ObDfo &child) const
{
  int ret = OB_SUCCESS;
  ObIArray<ObPxSqcMeta> &sqcs = child.get_sqcs();
  for (int64_t i = 0; OB_SUCC(ret) && i < sqcs.count(); i++) {
    ObPxSqcMeta &sqc = sqcs.at(i);
    if (OB_FAIL(sqc.get_temp_table_ctx().assign(ctx.get_temp_table_ctx()))) {
      LOG_WARN("failed to assign temp table ctx", K(ret));
    }
  }
  return ret;
}


int ObDfoSchedulerBasic::dispatch_transmit_channel_info_via_sqc(ObExecContext &ctx,
                                                                        ObDfo &child,
                                                                        ObDfo &parent) const
{
  UNUSED(ctx);
  UNUSED(parent);
  int ret = OB_SUCCESS;
  ObPxTaskChSets child_ch_sets;
  ObPxPartChMapArray &map = child.get_part_ch_map();
  if (child.is_root_dfo()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("a child dfo should not be root dfo", K(child), K(ret));
  } else {
    ObIArray<ObPxSqcMeta> &sqcs = child.get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      int64_t sqc_id = sqcs.at(idx).get_sqc_id();
      ObPxTransmitDataChannelMsg &transmit_data_channel_msg = sqcs.at(idx).get_transmit_channel_msg();
      ObDtlChTotalInfo *ch_info = nullptr;
      if (OB_FAIL(child.get_dfo_ch_info(sqc_id, ch_info))) {
        LOG_WARN("fail get child tasks", K(ret));
      } else if (OB_FAIL(transmit_data_channel_msg.set_payload(*ch_info, map))) {
        LOG_WARN("fail init msg", K(ret));
      }

      LOG_TRACE("ObPxCoord::MsgProc::dispatch_transmit_channel_info_via_sqc done."
                "sent transmit_data_channel_msg to child task",
                K(transmit_data_channel_msg), K(child), K(idx), K(cnt), K(ret));
    }
  }
  return ret;
}
// -------------division line-----------
int ObSerialDfoScheduler::init_all_dfo_channel(ObExecContext &ctx) const
{
  int ret = OB_SUCCESS;
  ObIArray<ObDfo *> &dfos = coord_info_.dfo_mgr_.get_all_dfos_for_update();
  for (int i = 0; OB_SUCC(ret) && i < dfos.count(); ++i) {
    ObDfo *child = dfos.at(i);
    ObDfo *parent = child->parent();
    if (OB_ISNULL(child) || OB_ISNULL(parent)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dfo is null", K(ret));
    } else if (!child->has_child_dfo() && !child->is_thread_inited()) {
      if (child->has_temp_table_scan()) {
        if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_temp_child_distribution(ctx,
                                                                         *child))) {
          LOG_WARN("fail to allocate local sqc by temp child distribution", K(child), K(ret));
        } else { /*do nothing.*/ }
      } else if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_data_distribution(
          coord_info_.pruning_table_location_,
          ctx, *child))) {
        LOG_WARN("fail to alloc data distribution", K(ret));
      } else { /*do nothing.*/ }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(set_temp_table_ctx_for_sqc(ctx, *child))) {
          LOG_WARN("failed to set temp table ctx", K(ret));
        }
      }
    } else {
      /*do nothing*/
    }
    if (OB_SUCC(ret)) {
      const bool has_reference_child = IS_HASH_SLAVE_MAPPING(parent->get_in_slave_mapping_type());
      if (parent->is_thread_inited()) {
      } else if (has_reference_child && OB_FAIL(ObPxSqcDistributionUtil::alloc_distribution_of_reference_child(
            coord_info_.pruning_table_location_, ctx, *parent))) {
        LOG_WARN("alloc distribution of reference child failed", K(ret));
      } else if (parent->has_temp_table_scan()) {
        if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_temp_child_distribution(ctx, *parent))) {
          LOG_WARN("fail to allocate local sqc by data distribution", K(parent), K(ret));
        }
      } else if (parent->is_root_dfo()) {
        if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_local_distribution(ctx, *parent))) {
          LOG_WARN("fail to alloc local distribution", K(ret));
        }
      } else if (parent->has_scan_op() || parent->has_dml_op()) {
        if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_data_distribution(
                coord_info_.pruning_table_location_, ctx, *parent))) {
          LOG_WARN("fail to allocate local sqc by data distribution", K(parent), K(ret));
        }
        LOG_TRACE("alloc_by_data_distribution", K(*parent));
      } else if (has_reference_child) {
        if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_reference_child_distribution(*parent))) {
          LOG_WARN("fail to allocate local sqc by data distribution", K(parent), K(child), K(ret));
        }
        LOG_TRACE("alloc_by_reference_child_distribution", K(*parent));
      } else if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_data_distribution(
                     coord_info_.pruning_table_location_, ctx, *parent))) {
        LOG_WARN("fail to alloc data distribution", K(ret));
      }
      if (OB_SUCC(ret) && !parent->is_scheduled()) {
        if (OB_FAIL(set_temp_table_ctx_for_sqc(ctx, *parent))) {
          LOG_WARN("failed to set temp table ctx", K(ret));
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (parent->need_access_store() && parent->is_in_slave_mapping()
               && ObPQDistributeMethod::HASH == child->get_dist_method()
               && child->is_out_slave_mapping()) {
      if (OB_FAIL(ObPxSqcDistributionUtil::check_slave_mapping_location_constraint(*child, *parent))) {
        LOG_WARN("slave mapping location constraint not satisfy", K(parent->get_dfo_id()),
                 K(child->get_dfo_id()));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(init_dfo_channel(ctx, child, parent))) {
        LOG_WARN("fail to init dfo channel", K(ret));
      }
    }
  }

  return ret;
}

int ObSerialDfoScheduler::init_data_xchg_ch(ObExecContext &ctx, ObDfo *dfo) const
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  if (OB_ISNULL(dfo)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dfo is null", K(ret));
  } else {
    ObIArray<ObPxSqcMeta> &sqcs = dfo->get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      ObPxSqcMeta &sqc = sqcs.at(idx);
      sqc.set_task_count(1);
      sqc.set_thread_inited(true);
    }
    if (OB_SUCC(ret)) {
      dfo->set_thread_inited(true);
      if (OB_FAIL(on_sqc_threads_inited(ctx, *dfo))) {
        LOG_WARN("failed to sqc thread init", K(ret));
      }
    }
  }
  return ret;
}
int ObSerialDfoScheduler::init_dfo_channel(ObExecContext &ctx, ObDfo *child, ObDfo *parent) const
{
  int ret = OB_SUCCESS;
  if (!child->is_thread_inited() && OB_FAIL(init_data_xchg_ch(ctx, child))) {
    LOG_WARN("fail to build data xchg ch", K(ret));
  } else if (!parent->is_thread_inited() && OB_FAIL(init_data_xchg_ch(ctx, parent))) {
    LOG_WARN("fail to build parent xchg ch", K(ret));
  } else if (OB_FAIL(build_data_xchg_ch(ctx, *child, *parent))) {
    LOG_WARN("fail to build data xchg ch", K(ret));
  } else if (OB_FAIL(dispatch_dtl_data_channel_info(ctx, *child, *parent))) {
    LOG_WARN("fail to dispatch dtl data channel", K(ret));
  }
  return ret;
}

int ObSerialDfoScheduler::dispatch_dtl_data_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dispatch_receive_channel_info_via_sqc(ctx, child,
      parent, /*is_parallel_scheduler*/false))) {
    LOG_WARN("fail to dispatch receive channel", K(ret));
  } else if (OB_FAIL(dispatch_transmit_channel_info_via_sqc(ctx, child, parent))) {
    LOG_WARN("fail to dispatch transmit channel", K(ret));
  }
  return ret;
}

int ObSerialDfoScheduler::try_schedule_next_dfo(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObTraceSpanGuard px_schedule_span(ctx.get_my_session(), TRACE_PX_SCHEDULE);
  ObDfo *dfo = NULL;
  if (OB_FAIL(coord_info_.dfo_mgr_.get_ready_dfo(dfo))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("fail get ready dfos", K(ret));
    } else {
      LOG_TRACE("No more dfos to schedule", K(ret));
    }
  } else if (OB_ISNULL(dfo)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dfo is null, unexpected schedule", K(ret));
  } else if (OB_FAIL(do_schedule_dfo(ctx, *dfo))) {
    LOG_WARN("fail to do schedule dfo", K(ret));
  }
  return ret;
}

int ObSerialDfoScheduler::dispatch_sqcs(ObExecContext &exec_ctx,
                                        ObDfo &dfo,
                                        ObIArray<ObPxSqcMeta> &sqcs) const
{
  int ret = OB_SUCCESS;
  const ObPhysicalPlan *phy_plan = NULL;
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  ObSQLSessionInfo *session = NULL;
  ObCurTraceId::TraceId *cur_thread_id = NULL;
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(NULL == (phy_plan = dfo.get_phy_plan()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL plan ptr unexpected", K(ret));
    } else if (OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(exec_ctx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("phy plan ctx NULL", K(ret));
    } else if (OB_ISNULL(session = exec_ctx.get_my_session())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is NULL", K(ret));
    } else if (OB_ISNULL(cur_thread_id = ObCurTraceId::get_trace_id())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("no memory", K(ret));
    }
  }
  const bool ignore_vtable_error = coord_info_.should_ignore_vtable_error();
  ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
    ObPxSqcMeta &sqc = sqcs.at(idx);
    if (OB_FAIL(ret)) {
    } else {
      SMART_VAR(ObPxInitSqcArgs, args) {
        int64_t timeout_us = phy_plan_ctx->get_timeout_timestamp() - ObTimeUtility::current_time();
        args.set_serialize_param(exec_ctx, const_cast<ObOpSpec &>(*dfo.get_root_op_spec()), *phy_plan);
        if ((NULL != dfo.parent() && !dfo.parent()->is_root_dfo()) ||
          coord_info_.enable_px_batch_rescan()) {
          sqc.set_transmit_use_interm_result(true);
        }
        if (NULL != dfo.parent() && dfo.parent()->is_root_dfo()) {
          sqc.set_adjoining_root_dfo(true);
        }
        if (dfo.has_child_dfo()) {
          sqc.set_recieve_use_interm_result(true);
        }
        if (ignore_vtable_error) {
          sqc.set_ignore_vtable_error(true);
        }
        if (coord_info_.enable_px_batch_rescan()) {
          OZ(sqc.set_rescan_batch_params(coord_info_.batch_rescan_ctl_->params_));
        }
        if (timeout_us <= 0) {
          ret = OB_TIMEOUT;
          LOG_WARN("dispatch sqc timeout", K(ret));
        } else if (OB_FAIL(args.sqc_.assign(sqc))) {
          LOG_WARN("fail assign sqc", K(ret));
        } else if (FALSE_IT(sqc.set_need_report(true))) {
          // The SQC reports completion after local asynchronous launch.
        } else if (OB_FAIL(OB_E(EventTable::EN_PX_SQC_INIT_FAILED) OB_SUCCESS)) {
          sqc.set_need_report(false);
          LOG_WARN("[SIM] fail to init local sqc", K(ret));
          if (ignore_vtable_error && ObVirtualTableErrorWhitelist::should_ignore_vtable_error(ret)) {
            ObLocalSqcFailureReporter call(
                &sqc, ret, phy_plan_ctx->get_timeout_timestamp());
            call.mock_sqc_finish_msg();
            ret = OB_SUCCESS;
          }
        } else {
          // Deep-clone the SQC arguments before dispatching them to a runtime worker.
          // worker. Asynchronous launch lets the coordinator drain the local
          // DTL channel while the SQC task is producing rows.
          int64_t ser_len = args.get_serialize_size();
          std::shared_ptr<char[]> ser_buf(new (std::nothrow) char[ser_len]);
          int64_t ser_pos = 0;
          if (OB_ISNULL(ser_buf.get())) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to alloc serialize buffer", K(ret), K(ser_len));
          } else if (OB_FAIL(args.serialize(ser_buf.get(), ser_len, ser_pos))) {
            LOG_WARN("fail to serialize sqc init args", K(ret));
          } else {
            (void)ex_rpc::async_call([ser_buf, ser_pos]() {
              return launch_sqc_fast_local(ser_buf.get(), ser_pos); });
          }
        }
      }
    }
  }
  return ret;
}

int ObSerialDfoScheduler::do_schedule_dfo(ObExecContext &ctx, ObDfo &dfo) const
{
  int ret = OB_SUCCESS;
  ObIArray<ObPxSqcMeta> &sqcs = dfo.get_sqcs();
  LOG_TRACE("Dfo's sqcs count", K(dfo), "sqc_count", sqcs.count());

  ObSQLSessionInfo *session = NULL;
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(NULL == (session = ctx.get_my_session()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr session", K(ret));
    }
  }
  // 0. Allocate QC-SQC channel information
  ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
    ObPxSqcMeta &sqc = sqcs.at(idx);
    ObDtlChannelInfo &qc_ci = sqc.get_qc_channel_info();
    ObDtlChannelInfo &sqc_ci = sqc.get_sqc_channel_info();
    if (OB_FAIL(ObDtlChannelGroup::make_channel(sqc_ci /* producer */,
                                                qc_ci /* consumer */))) {
      LOG_WARN("fail make channel for QC-SQC", K(ret));
    } else {
      LOG_TRACE("make a new local channel for qc and sqc",
                K(idx), K(cnt), K(sqc_ci), K(qc_ci));
    }
  }

  int64_t thread_id = GETTID();
  // 1. Link QC-SQC channel
  ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
    ObPxSqcMeta &sqc = sqcs.at(idx);
    ObDtlChannelInfo &ci = sqc.get_qc_channel_info();
    ObDtlChannel *ch = NULL;
    // ObDtlChannelGroup::make_channel has already filled the attributes of ci
    // So link_channel knows how to establish the channel
    if (OB_FAIL(ObDtlChannelGroup::link_channel(ci, ch))) {
      LOG_WARN("fail link channel", K(ci), K(ret));
    } else if (OB_ISNULL(ch)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail add qc channel", K(ret));
    } else {
      ch->set_qc_owner();
      ch->set_thread_id(thread_id);
      (void)coord_info_.msg_loop_.register_channel(*ch);
      sqc.set_qc_channel(ch);
      LOG_TRACE("link qc-sqc channel and registered to qc msg loop. ready to receive sqc ctrl msg",
                K(idx), K(cnt), K(*ch), K(dfo), K(sqc));
    }
  }

  // 2. allocate branch_id for DML: replace, insert update, select for update
  if (OB_SUCC(ret) && dfo.has_need_branch_id_op()) {
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      int16_t branch_id = 0;
      const int64_t max_task_count = sqcs.at(idx).get_max_task_count();
      if (OB_FAIL(ObSqlTransControl::alloc_branch_id(ctx, max_task_count, branch_id))) {
        LOG_WARN("alloc branch id fail", KR(ret), K(max_task_count));
      } else {
        sqcs.at(idx).set_branch_id_base(branch_id);
        LOG_TRACE("alloc branch id", K(max_task_count), K(branch_id), K(sqcs.at(idx)));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(dispatch_sqcs(ctx, dfo, sqcs))) {
      LOG_WARN("fail to dispatch sqc", K(ret));
    }
  }

  dfo.set_scheduled();
  return ret;
}

int ObSerialDfoScheduler::erase_dtl_interm_results(ObPxDtlIntermResBatch &batch)
{
  int ret = OB_SUCCESS;
  dtl::ObDTLIntermResultKey key;
#ifdef ERRSIM
  int ecode = EventTable::EN_PX_SINGLE_DFO_NOT_ERASE_DTL_INTERM_RESULT;
  if (OB_SUCCESS != ecode && OB_SUCC(ret)) {
    LOG_WARN("not erase_dtl_interm_result by design", K(ret));
    return OB_SUCCESS;
  }
#endif
  int64_t batch_size = 0 == batch.batch_size_ ? 1 : batch.batch_size_;
  for (int64_t i = 0; i < batch.info_.count(); i++) {
    ObPxDtlIntermResInfo &info = batch.info_.at(i);
    for (int64_t task_id = 0; task_id < info.task_count_; task_id++) {
      ObPxTaskChSet ch_set;
      if (OB_FAIL(ObDtlChannelUtil::get_receive_dtl_channel_set(info.sqc_id_, task_id,
            info.ch_total_info_, ch_set))) {
        LOG_WARN("get receive dtl channel set failed", K(ret));
      } else {
        LOG_TRACE("erase dtl interm result", K(i), K(batch.batch_size_), K(info), K(task_id), K(ch_set));
        for (int64_t ch_idx = 0; ch_idx < ch_set.count(); ch_idx++) {
          key.channel_id_ = ch_set.get_ch_info_set().at(ch_idx).chid_;
          for (int64_t batch_id = 0; batch_id < batch_size && OB_SUCC(ret); batch_id++) {
            key.batch_id_= batch_id;
            if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::dtl::ObDTLIntermResultManager>()->erase_interm_result_info(key))) {
              if (OB_HASH_NOT_EXIST == ret) {
                ret = OB_SUCCESS;
                break;
              } else {
                LOG_WARN("fail to release receive internal result", K(ret));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

void ObSerialDfoScheduler::clean_dtl_interm_result(ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObDfo *> &all_dfos = coord_info_.dfo_mgr_.get_all_dfos();
  ObDfo *last_dfo = nullptr;
  int clean_ret = OB_E(EventTable::EN_ENABLE_CLEAN_INTERM_RES) OB_SUCCESS;
  if (clean_ret != OB_SUCCESS) {
    // Fault injection: Do not clean up interm results.
  } else if (all_dfos.empty()) {
    // do nothing
  } else if (FALSE_IT(last_dfo = all_dfos.at(all_dfos.count() - 1))) {
  } else if (OB_NOT_NULL(last_dfo) && last_dfo->is_scheduled() && OB_NOT_NULL(last_dfo->parent())
      && last_dfo->parent()->is_root_dfo()) {
    // all dfo scheduled, do nothing.
    LOG_TRACE("all dfo scheduled.");
  } else {
    ObPxDtlIntermResBatch batch;
    batch.batch_size_ = coord_info_.get_rescan_param_count();
    for (int64_t i = 0; i < all_dfos.count(); i++) {
      ObDfo *dfo = all_dfos.at(i);
      ObDfo *parent = NULL;
      if (OB_NOT_NULL(dfo) && dfo->is_scheduled() && NULL != (parent = dfo->parent())
          && !parent->is_root_dfo() && !parent->is_scheduled()) {
        // if current dfo is scheduled but parent dfo is not scheduled.
        for (int64_t j = 0; j < parent->get_sqcs_count(); j++) {
          ObPxSqcMeta &sqc = parent->get_sqcs().at(j);
          int64_t msg_idx = 0;
          for (; msg_idx < sqc.get_serial_receive_channels().count(); msg_idx++) {
            if (sqc.get_serial_receive_channels().at(msg_idx).get_child_dfo_id() == dfo->get_dfo_id()) {
              break;
            }
          }
          if (OB_LIKELY(msg_idx < sqc.get_serial_receive_channels().count())) {
            ObPxReceiveDataChannelMsg &msg = sqc.get_serial_receive_channels().at(msg_idx);
            if (OB_FAIL(batch.info_.push_back(ObPxDtlIntermResInfo(msg.get_ch_total_info(),
                        sqc.get_sqc_id(), sqc.get_task_count())))) {
              LOG_WARN("push back failed", K(ret));
            }
          }
        }
      }
    }
    if (OB_SUCC(ret) && !batch.info_.empty()
        && OB_FAIL(erase_dtl_interm_results(batch))) {
      LOG_WARN("erase dtl interm results failed", KR(ret));
    }
  }
  UNUSED(exec_ctx);
}
// -------------division line-----------
// Start DFO's SQC thread
int ObParallelDfoScheduler::do_schedule_dfo(ObExecContext &exec_ctx, ObDfo &dfo) const
{
  int ret = OB_SUCCESS;
  ObIArray<ObPxSqcMeta> &sqcs = dfo.get_sqcs();
  LOG_TRACE("Dfo's sqcs count", K(dfo), "sqc_count", sqcs.count());

  ObSQLSessionInfo *session = NULL;
  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(NULL == (session = exec_ctx.get_my_session()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr session", K(ret));
    }
  }
  // 0. Allocate QC-SQC channel information
  ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
    ObPxSqcMeta &sqc = sqcs.at(idx);
    ObDtlChannelInfo &qc_ci = sqc.get_qc_channel_info();
    ObDtlChannelInfo &sqc_ci = sqc.get_sqc_channel_info();
    if (OB_FAIL(ObDtlChannelGroup::make_channel(sqc_ci /* producer */,
                                                qc_ci /* consumer */))) {
      LOG_WARN("fail make channel for QC-SQC", K(ret));
    } else {
      LOG_TRACE("make a new local channel for qc and sqc",
                K(idx), K(cnt), K(sqc_ci), K(qc_ci));
    }
  }

  int64_t thread_id = GETTID();
  // 1. Link QC-SQC channel
  ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
    ObPxSqcMeta &sqc = sqcs.at(idx);
    ObDtlChannelInfo &ci = sqc.get_qc_channel_info();
    ObDtlChannel *ch = NULL;
    // ObDtlChannelGroup::make_channel has already filled the attributes of ci
    // So link_channel knows how to establish the channel
    if (OB_FAIL(ObDtlChannelGroup::link_channel(ci, ch))) {
      LOG_WARN("fail link channel", K(ci), K(ret));
    } else if (OB_ISNULL(ch)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail add qc channel", K(ret));
    } else {
      ch->set_qc_owner();
      ch->set_thread_id(thread_id);
      (void)coord_info_.msg_loop_.register_channel(*ch);
      sqc.set_qc_channel(ch);
      sqc.set_sqc_count(sqcs.count());
      LOG_TRACE("link qc-sqc channel and registered to qc msg loop. ready to receive sqc ctrl msg",
                K(idx), K(cnt), K(*ch), K(dfo), K(sqc));
    }
  }

  // 2. allocate branch_id for DML: replace, insert update, select for update
  if (OB_SUCC(ret) && dfo.has_need_branch_id_op()) {
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      int16_t branch_id = 0;
      const int64_t max_task_count = sqcs.at(idx).get_max_task_count();
      if (OB_FAIL(ObSqlTransControl::alloc_branch_id(exec_ctx, max_task_count, branch_id))) {
        LOG_WARN("alloc branch id fail", KR(ret), K(max_task_count));
      } else {
        sqcs.at(idx).set_branch_id_base(branch_id);
        LOG_TRACE("alloc branch id", K(max_task_count), K(branch_id), K(sqcs.at(idx)));
      }
    }
  }

  if (OB_SUCC(ret)) {
    // The following logic handles the timeout situation during the handshake phase
    //  - Purpose: To prevent deadlock
    //  - Method: Once a timeout occurs, terminate all sqc, wait for a period of time, and then retry the entire dfo
    //  - Issue: init sqc is asynchronous, where part of the sqc has already reported the information of obtaining the task
    //           suddenly terminated, QC side status needs to be re-maintained. However, there are the following issues:
    //           Scenario example:
    //            1. sqc1 success, sqc2 timeout
    //            2. dfo abort, clean sqc state
    //            3. sqc1 reports that the task has already been allocated (old news)
    //            4. sqc1, sqc2 receive interrupt information
    //            5. sqc1 reschedule
    //            6. sqc2 reports that tasks have been allocated (latest news)
    //            7. qc believes that all dfo have been successfully scheduled (in fact, they have not)
    //            8. sqc1 reports the allocated task (too late msg)
    //
    ret = dispatch_sqc(exec_ctx, dfo, sqcs);
  }
  return ret;
}

int ObParallelDfoScheduler::dispatch_dtl_data_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const
{
  int ret = OB_SUCCESS;
  /* Note the order of settings: set receive channel first, then set transmit channel.
   * This ensures that when transmit sends data, the receive end already has someone listening,
   * otherwise, data sent by transmit may go unreceived for a period of time, causing DTL to retry excessively and affecting system performance
   */

  if (OB_SUCC(ret)) {
    if (parent.is_prealloc_receive_channel() && !parent.is_scheduled()) {
      // Because parent can contain multiple receive operators, only for the scheduling scenario
      // can be carried by sqc, subsequent receive operator channel information must go through dtl
      if (OB_FAIL(dispatch_receive_channel_info_via_sqc(ctx, child, parent))) {
        LOG_WARN("fail dispatch receive channel info", K(child), K(parent), K(ret));
      }
    } else {
      if (OB_FAIL(dispatch_receive_channel_info(ctx, child, parent))) {
        LOG_WARN("fail dispatch receive channel info", K(child), K(parent), K(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (child.is_prealloc_transmit_channel() && !child.is_scheduled()) {
      if (OB_FAIL(dispatch_transmit_channel_info_via_sqc(ctx, child, parent))) {
        LOG_WARN("fail dispatch transmit channel info", K(child), K(ret));
      }
    } else {
      if (OB_FAIL(dispatch_transmit_channel_info(ctx, child, parent))) {
        LOG_WARN("fail dispatch transmit channel info", K(child), K(ret));
      }
    }
  }

  return ret;
}


int ObParallelDfoScheduler::dispatch_transmit_channel_info(ObExecContext &ctx, ObDfo &child, ObDfo &parent) const
{
  UNUSED(ctx);
  UNUSED(parent);
  int ret = OB_SUCCESS;
  ObPxTaskChSets child_ch_sets;
  ObPxPartChMapArray &map = child.get_part_ch_map();
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  // TODO: abort here to test transmit wait for channel info when inner_open.
  if (OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("phy plan ctx NULL", K(ret));
  } else if (child.is_root_dfo()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("a child dfo should not be root dfo", K(child), K(ret));
  } else {
    ObIArray<ObPxSqcMeta> &sqcs = child.get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      ObDtlChannel *ch = sqcs.at(idx).get_qc_channel();
      int64_t sqc_id = sqcs.at(idx).get_sqc_id();
      ObPxTransmitDataChannelMsg transmit_data_channel_msg;
      if (OB_ISNULL(ch)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("qc channel should not be null", K(ret));
      } else {
        ObDtlChTotalInfo *ch_info = nullptr;
        if (OB_FAIL(child.get_dfo_ch_info(idx, ch_info))) {
          LOG_WARN("fail get child tasks", K(ret));
        } else if (OB_FAIL(transmit_data_channel_msg.set_payload(*ch_info, map))) {
          LOG_WARN("fail init msg", K(ret));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ch->send(transmit_data_channel_msg,
            phy_plan_ctx->get_timeout_timestamp()))) { // Do our best, if push fails it will be handled by other mechanisms
        LOG_WARN("fail push data to channel", K(ret));
      } else if (OB_FAIL(ch->flush(true, false))) {
        LOG_WARN("fail flush dtl data", K(ret));
      }
      LOG_TRACE("ObPxCoord::MsgProc::dispatch_transmit_channel_info done."
                "sent transmit_data_channel_msg to child task",
                K(transmit_data_channel_msg), K(child), K(idx), K(cnt), K(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(ObPxChannelUtil::sqcs_channles_asyn_wait(sqcs))) {
      LOG_WARN("failed to wait for sqcs", K(ret));
    }
  }
  return ret;
}

int ObParallelDfoScheduler::dispatch_receive_channel_info(ObExecContext &ctx,
                                                                  ObDfo &child,
                                                                  ObDfo &parent) const
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  ObPxTaskChSets parent_ch_sets;
  int64_t child_dfo_id = child.get_dfo_id();
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  if (OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("phy plan ctx NULL", K(ret));
  } else if (parent.is_root_dfo()) {
    if (OB_FAIL(dispatch_root_dfo_channel_info(ctx, child, parent))) {
      LOG_WARN("fail dispatch root dfo receive channel info", K(ret), K(parent), K(child));
    }
  } else {
    // Split receive channels sets by sqc dimension and send to each SQC
    ObIArray<ObPxSqcMeta> &sqcs = parent.get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      ObDtlChannel *ch = sqcs.at(idx).get_qc_channel();
      int64_t sqc_id = sqcs.at(idx).get_sqc_id();
      ObPxReceiveDataChannelMsg receive_data_channel_msg;
      if (OB_ISNULL(ch) || OB_INVALID_INDEX == sqc_id) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected param", KP(ch), K(parent), K(sqc_id), K(ret));
      } else {
        ObDtlChTotalInfo *ch_info = nullptr;
        if (OB_FAIL(child.get_dfo_ch_info(idx, ch_info))) {
          LOG_WARN("failed to get task receive chs", K(ret));
        }

        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(receive_data_channel_msg.set_payload(child_dfo_id, *ch_info))) {
          LOG_WARN("fail init msg", K(ret));
        } else if (!receive_data_channel_msg.is_valid()) {
          LOG_WARN("receive data channel msg is not valid", K(ret), K(receive_data_channel_msg));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(ch->send(receive_data_channel_msg,
            phy_plan_ctx->get_timeout_timestamp()))) { // Do our best, if push fails it will be handled by other mechanisms
          LOG_WARN("fail push data to channel", K(ret));
        } else if (OB_FAIL(ch->flush(true, false))) {
          LOG_WARN("fail flush dtl data", K(ret));
        } else {
          LOG_TRACE("dispatched receive ch",
                    K(idx), K(cnt), K(*ch), K(sqc_id), K(child_dfo_id), K(parent_ch_sets));
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ObPxChannelUtil::sqcs_channles_asyn_wait(sqcs))) {
      LOG_WARN("failed to wait for sqcs", K(ret));
    }
  }
  return ret;
}
// Optimization point: can prealloc can be precalculated very early
int ObParallelDfoScheduler::check_if_can_prealloc_xchg_ch(ObDfo &child,
                                                                  ObDfo &parent,
                                                                  bool &bret) const
{
  int ret = OB_SUCCESS;
  bret = true;
  if (child.is_scheduled() || parent.is_scheduled()) {
    bret = false;
  } else {
    const ObIArray<ObPxSqcMeta> &sqcs = child.get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, true == bret) {
      const ObPxSqcMeta &sqc = sqcs.at(idx);
      if (1 < sqc.get_max_task_count() ||
          1 < sqc.get_min_task_count()) {
        bret = false;
      }
    }
  }
  if (bret && OB_SUCC(ret)) {
    const ObIArray<ObPxSqcMeta> &sqcs = parent.get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, true == bret) {
      const ObPxSqcMeta &sqc = sqcs.at(idx);
      if (1 < sqc.get_max_task_count() ||
          1 < sqc.get_min_task_count()) {
        bret = false;
      }
    }
  }
  return ret;
}

int ObParallelDfoScheduler::do_fast_schedule(ObExecContext &exec_ctx,
                                                     ObDfo &child,
                                                     ObDfo &parent) const
{
  int ret = OB_SUCCESS;
  // Enable data channel pre-allocation mode, hinting subsequent logic not to allocate data channels
  // The following three function calls must be in the correct order, because:
  //  1. After calling root dfo, root dfo will be marked as a scheduling success status
  //  2. Then pretend that child dfo also scheduled successfully,
  //  After the above two steps, it can be considered that the number of threads for child and parent are determined,
  //  can allocate the parent-child channel information
  //  3. Finally schedule child to carry channel information to the sqc end
  if (OB_SUCC(ret) && !parent.is_scheduled()) {
    parent.set_prealloc_receive_channel(true);
    if (parent.has_parent() && parent.parent()->is_thread_inited()) {
      parent.set_prealloc_transmit_channel(true);
    }
    if (OB_FAIL(mock_on_sqc_init_msg(exec_ctx, parent))) {
      LOG_WARN("fail mock init parent dfo", K(parent), K(child), K(ret));
    }
  }
  if (OB_SUCC(ret) && !child.is_scheduled()) {
    child.set_prealloc_transmit_channel(true);
    if (OB_FAIL(mock_on_sqc_init_msg(exec_ctx, child))) {
      LOG_WARN("fail mock init child dfo", K(parent), K(child), K(ret));
    }
  }
  if (OB_SUCC(ret) && !parent.is_scheduled()) {
    if (OB_FAIL(schedule_dfo(exec_ctx, parent))) {
      LOG_WARN("fail schedule root dfo", K(parent), K(ret));
    }
  }
  if (OB_SUCC(ret) && !child.is_scheduled()) {
    if (OB_FAIL(schedule_dfo(exec_ctx, child))) {
      LOG_WARN("fail schedule child dfo", K(child), K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    LOG_TRACE("fast schedule ok", K(parent), K(child));
  }
  return ret;
}

int ObParallelDfoScheduler::mock_on_sqc_init_msg(ObExecContext &ctx, ObDfo &dfo) const
{
  int ret = OB_SUCCESS;
  if (dfo.is_root_dfo()) {
    // root dfo no need to mock this message
  } else {
    ObIArray<ObPxSqcMeta> &sqcs = dfo.get_sqcs();
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      ObPxSqcMeta &sqc = sqcs.at(idx);
      if (1 != sqc.get_max_task_count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("only if all sqc task cnt is one", K(sqc), K(ret));
      } else {
        ObPxInitSqcResultMsg pkt;
        pkt.dfo_id_ = sqc.get_dfo_id();
        pkt.sqc_id_ = sqc.get_sqc_id();
        pkt.rc_ = OB_SUCCESS;
        pkt.task_count_ = sqc.get_max_task_count();
        if (OB_FAIL(proc_.on_sqc_init_msg(ctx, pkt))) {
          LOG_WARN("fail mock sqc init msg", K(pkt), K(sqc), K(ret));
        }
      }
    }
  }
  return ret;
}


int ObParallelDfoScheduler::schedule_dfo(ObExecContext &exec_ctx,
    ObDfo &dfo) const
{
  int ret = OB_SUCCESS;
  int retry_times = 0;
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  /* Exception handling:
   * 1. Timeout Msg: Timeout, uncertain whether successful, at this time a link may be established, how to handle?
   * 2. No Msg: DFO link to DTL failed, unable to return message
   * 3. DFO Msg: Dispatch failed due to insufficient threads
   *
   * Unified handling method:
   * 1. QC reads DTL until timeout
   */
  NG_TRACE_EXT(dfo_start, OB_ID(dfo_id), dfo.get_dfo_id());
  if (dfo.is_root_dfo()) {
    if (OB_FAIL(on_root_dfo_scheduled(exec_ctx, dfo))) {
      LOG_WARN("fail setup root dfo", K(ret));
    }
  } else if (OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(exec_ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("phy plan ctx NULL", K(ret));
  } else if (OB_FAIL(do_schedule_dfo(exec_ctx, dfo))) {
    LOG_WARN("fail dispatch dfo", K(ret));
  }
  // Regardless of success or failure, mark as scheduled.
  // When schedule fails, the entire query fails.
  dfo.set_scheduled();
  LOG_TRACE("schedule dfo ok", K(dfo), K(retry_times), K(ret));
  return ret;
}


int ObParallelDfoScheduler::on_root_dfo_scheduled(ObExecContext &ctx, ObDfo &root_dfo) const
{
  int ret = OB_SUCCESS;
  ObPxSqcMeta *sqc = NULL;

  LOG_TRACE("on_root_dfo_scheduled", K(root_dfo));

  if (OB_FAIL(root_dfo.get_sqc(0, sqc))) {
    LOG_WARN("fail find sqc", K(root_dfo), K(ret));
  } else if (OB_ISNULL(sqc)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(root_dfo), KP(sqc), K(ret));
  } else {
    sqc->set_task_count(1);
    sqc->set_thread_inited(true);
    root_dfo.set_thread_inited(true);
    root_dfo.set_used_worker_count(0);
    ret = on_sqc_threads_inited(ctx, root_dfo);
  }

  if (OB_SUCC(ret)) {
    if (root_dfo.is_thread_inited()) {
      // Attempt to schedule self-child pair
      if (OB_SUCC(ret)) {
        int64_t cnt = root_dfo.get_child_count();
        ObDfo *child= NULL;
        if (1 != cnt) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("root dfo should has only 1 child dfo", K(cnt), K(ret));
        } else if (OB_FAIL(root_dfo.get_child_dfo(0, child))) {
          LOG_WARN("fail get child dfo", K(cnt), K(ret));
        } else if (OB_ISNULL(child)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("NULL unexpected", K(ret));
        } else if (child->is_thread_inited()) {
          // Because the timing of who schedules successfully first in the root-child pair is uncertain
          // Any dfo that successfully schedules after has the obligation to advance the on_dfo_pair_thread_inited message
          // For example, in the scheduling of root-A-B, A and B have already been scheduled successfully, and thread init msg has been received
          // At this point, scheduling root, requires root to advance on_dfo_pair_thread_inited
          ret = proc_.on_dfo_pair_thread_inited(ctx, *child, root_dfo);
        }
      }
    }
  }
  return ret;
}
// Batch distribute DFO to each server, build SQC
int ObParallelDfoScheduler::dispatch_sqc(ObExecContext &exec_ctx,
                                         ObDfo &dfo,
                                         ObIArray<ObPxSqcMeta> &sqcs) const
{
  int ret = OB_SUCCESS;
  bool fast_sqc = dfo.is_fast_dfo();

  const ObPhysicalPlan *phy_plan = NULL;
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  ObSQLSessionInfo *session = NULL;

  if (OB_SUCC(ret)) {
    if (OB_UNLIKELY(NULL == (phy_plan = dfo.get_phy_plan()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL plan ptr unexpected", K(ret));
    } else if (OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(exec_ctx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("phy plan ctx NULL", K(ret));
    } else if (OB_ISNULL(session = exec_ctx.get_my_session())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is NULL", K(ret));
    }
  }
  if (OB_SUCC(ret) && nullptr != dfo.parent() && dfo.parent()->is_root_dfo()) {
    ARRAY_FOREACH(sqcs, idx) {
      ObPxSqcMeta &sqc = sqcs.at(idx);
      sqc.set_adjoining_root_dfo(true);
    }
  }
  // Clone each local SQC execution context, launch it on a runtime worker, and
  // feed the admission response into the coordinator message path.
  ObSEArray<ObPxInitSqcResponse, 8> responses;
  ObSEArray<int, 8> resp_rets;
  ObSEArray<ex_rpc::HandleRef<ObPxInitSqcResponse>, 8> handles;
  if (OB_SUCC(ret) && sqcs.count() > 0) {
    SMART_VAR(ObPxInitSqcArgs, args) {
      if (sqcs.count() > 1) {
        args.enable_serialize_cache();
      }
      args.set_serialize_param(exec_ctx, const_cast<ObOpSpec &>(*dfo.get_root_op_spec()), *phy_plan);
      ARRAY_FOREACH_X(sqcs, idx, count, OB_SUCC(ret)) {
        ObPxSqcMeta &sqc = sqcs.at(idx);
        int64_t timeout_us = phy_plan_ctx->get_timeout_timestamp() - ObTimeUtility::current_time();
        if (timeout_us < 0) {
          ret = OB_TIMEOUT;
        } else if (OB_FAIL(args.sqc_.assign(sqc))) {
          LOG_WARN("fail assign sqc", K(ret));
        } else {
          int64_t ser_len = args.get_serialize_size();
          char *ser_buf = static_cast<char *>(exec_ctx.get_allocator().alloc(ser_len));
          int64_t ser_pos = 0;
          if (OB_ISNULL(ser_buf)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to alloc serialize buffer", K(ret), K(ser_len));
          } else if (OB_FAIL(args.serialize(ser_buf, ser_len, ser_pos))) {
            LOG_WARN("fail to serialize sqc init args", K(ret));
          } else {
            ex_rpc::HandleRef<ObPxInitSqcResponse> handle =
                ex_rpc::async_call<ObPxInitSqcResponse>(
                    [ser_buf, ser_pos](ObPxInitSqcResponse &resp) -> int {
                      return launch_sqc_async_local(ser_buf, ser_pos, resp); });
            if (!handle) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("fail to dispatch in-proc sqc", K(ret), K(sqc));
            } else if (OB_FAIL(handles.push_back(handle))) {
              LOG_WARN("fail to push sqc handle", K(ret));
            }
          }
        }
        if (OB_FAIL(ret)) {
          break;
        }
      }
    }
  }

  for (int64_t hidx = 0; hidx < handles.count(); ++hidx) {
    int launch_ret = handles.at(hidx)->wait();
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = responses.push_back(handles.at(hidx)->result()))) {
      ret = OB_SUCCESS == ret ? tmp_ret : ret;
    } else if (OB_SUCCESS != (tmp_ret = resp_rets.push_back(launch_ret))) {
      ret = OB_SUCCESS == ret ? tmp_ret : ret;
    }
    if (OB_SUCCESS != launch_ret && OB_SUCC(ret)) {
      ret = launch_ret;
      LOG_WARN("fail to init in-proc sqc", K(ret), K(hidx));
    }
  }

  if (OB_FAIL(ret)) {
    for (int64_t i = 0; i < sqcs.count(); ++i) {
      ObPxSqcMeta &sqc = sqcs.at(i);
      if (i < responses.count() && i < resp_rets.count()
          && OB_SUCCESS == resp_rets.at(i)
          && OB_SUCCESS == responses.at(i).rc_) {
        sqc.set_need_report(true);
      } else {
        sqc.set_need_report(false);
        sqc.set_thread_finish(true);
      }
    }
  } else {
    ARRAY_FOREACH(responses, idx) {
      ObPxInitSqcResponse &resp = responses.at(idx);
      ObPxSqcMeta &sqc = sqcs.at(idx);
      sqc.set_need_report(true);
      if (!fast_sqc) {
        ObPxInitSqcResultMsg pkt;
        pkt.dfo_id_ = sqc.get_dfo_id();
        pkt.sqc_id_ = sqc.get_sqc_id();
        pkt.rc_ = resp.rc_;
        pkt.task_count_ = resp.reserved_thread_count_;
        pkt.sqc_order_gi_tasks_ = resp.sqc_order_gi_tasks_;
        if (resp.reserved_thread_count_ < sqc.get_max_task_count()) {
          LOG_TRACE("SQC don`t have enough thread or thread auto scaling, Downgraded thread allocation",
              K(resp), K(sqc));
        }
        if (OB_FAIL(pkt.tablets_info_.assign(resp.partitions_info_))) {
          LOG_WARN("Failed to assign partition info", K(ret));
        } else if (OB_FAIL(proc_.on_sqc_init_msg(exec_ctx, pkt))) {
          LOG_WARN("fail to do sqc init callback", K(resp), K(pkt), K(ret));
        }
      }
    }
  }
  return ret;
}

int ObParallelDfoScheduler::try_schedule_next_dfo(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObTraceSpanGuard px_schedule_span(ctx.get_my_session(), TRACE_PX_SCHEDULE);
  ObSEArray<ObDfo *, 3> dfos;
  while (OB_SUCC(ret)) {
    // Each iteration outputs only one pair of DFO, parent & child
    if (OB_FAIL(coord_info_.dfo_mgr_.get_ready_dfos(dfos))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("fail get ready dfos", K(ret));
      } else {
        LOG_TRACE("No more dfos to schedule", K(ret));
      }
    } else if (0 == dfos.count()) {
      LOG_TRACE("No dfos to schedule for now. wait");
      break;
    } else if (2 != dfos.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get_ready_dfo should output a pair of dfo",
               "actual", dfos.count(), "expect", 2, K(ret));
    } else if (OB_ISNULL(dfos.at(0)) || OB_ISNULL(dfos.at(1))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL unexpetected", K(ret));
    } else {
      /*
       * Agree with get_ready_dfo() that 0 is child, 1 is parent:
       *
       *   parent  <-- 1
       *   /
       * child  <-- 0
       */
      ObDfo &child = *dfos.at(0);
      ObDfo &parent = *dfos.at(1);
      LOG_TRACE("to schedule", K(parent), K(child));
      if (OB_FAIL(schedule_pair(ctx, child, parent))) {
        LOG_WARN("fail schedule parent and child", K(ret));
      }
    }
  }
  return ret;
}

int ObParallelDfoScheduler::schedule_pair(ObExecContext &exec_ctx,
                                                  ObDfo &child,
                                                  ObDfo &parent)
{
  int ret = OB_SUCCESS;
  //
  // for scan dfo:  dop + ranges -> dop + svr -> (svr1, th_cnt1), (svr2, th_cnt2), ...
  // for other dfo: dop + child svr -> (svr1, th_cnt1), (svr2, th_cnt2), ...
  //
  if (OB_SUCC(ret)) {
    // Scheduling must be done in pairs, whenever a child is scheduled, its parent must have already been scheduled successfully
    if (!child.is_scheduled() && child.has_child_dfo()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("a interm node with child running should not be in state of unscheduled",
               K(child), K(ret));
    } else if (!child.is_scheduled()) {
      if (child.has_temp_table_scan()) {
        if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_temp_child_distribution(exec_ctx,
                                                                         child))) {
          LOG_WARN("fail to allocate local sqc by temp table distribution", K(child), K(ret));
        } else { /*do nohting.*/ }
      } else if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_data_distribution(
            coord_info_.pruning_table_location_,
            exec_ctx,
            child))) {
        LOG_WARN("fail to allocate local sqc by data distribution", K(child), K(ret));
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(set_temp_table_ctx_for_sqc(exec_ctx, child))) {
          LOG_WARN("failed to set temp table ctx", K(ret));
        }
      }
      LOG_TRACE("alloc_by_data_distribution", K(child));
    } else {
      // already in schedule, pass
    }
  }
  if (OB_SUCC(ret)) {
    if (!parent.is_scheduled()) {
      const bool has_reference_child = IS_HASH_SLAVE_MAPPING(parent.get_in_slave_mapping_type());
      if (has_reference_child && OB_FAIL(ObPxSqcDistributionUtil::alloc_distribution_of_reference_child(
            coord_info_.pruning_table_location_, exec_ctx, parent))) {
        LOG_WARN("alloc distribution of reference child failed", K(ret));
      } else if (parent.has_temp_table_scan()) {
        if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_temp_child_distribution(exec_ctx,
                                                                         parent))) {
          LOG_WARN("fail to allocate local sqc by data distribution", K(parent), K(ret));
        } else { /*do nohting.*/ }
      } else if (parent.is_root_dfo()) {
        // QC/local dfo, directly execute on the local machine and thread, no need to calculate execution location
        if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_local_distribution(exec_ctx,
                                                                    parent))) {
          LOG_WARN("alloc SQC on local failed", K(parent), K(ret));
        }
      } else {
        // DONE (xiaochu): If parent dfo already includes a scan, then there is a case where dfo processes according to scan
        // The location of the data is allocated, and the data of child dfo needs to be actively shuffled to the machine where the parent resides.
        // Generally, there are three cases:
        // (1) parent approaches child, suitable for scenarios where parent is a pure compute node
        // (2) parent independent, child data shuffle to parent, suitable for scenarios where parent also needs to read from disk;
        //     Where pdml global index maintain fits this situation.
        // (3) parent、child completely independent, each allocates position according to its own situation, suitable for scenarios requiring expansion of computational capacity
        // (4) parent, child are special slave mapping relationships, need to distribute parent according to the distribution of the reference table
        // sqc。
        //
        // Below only the first and second cases are implemented, the third requirement is unclear, listed as TODO
        if (parent.has_scan_op() || parent.has_dml_op()) { // Refer to Partial Partition Wise Join
          // When TSC exists in DFO or global index maintain op in pdml:
          // 1. When TSC exists, the location information of sqcs uses the location information from the tsc table
          // 2. When it is pdml, dml+px situation, sqcs's locations information uses the locations of the DML corresponding table
          if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_data_distribution(
            coord_info_.pruning_table_location_, exec_ctx, parent))) {
            LOG_WARN("fail to allocate local sqc by data distribution", K(parent), K(ret));
          }
          LOG_TRACE("alloc_by_data_distribution", K(parent));
        } else if (parent.is_single()) {
          // Common in PDML scenarios, if parent does not have tsc, the intermediate parent DFO needs to pull data from child DFO to QC locally first, then shuffle it to the upper DFO
          // For example, parent might be a scalar group by, which is marked as is_local, at this time
          // Walk alloc_by_data_distribution, internally it will allocate a QC local thread to execute
          // or nested PX scenario
          if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_data_distribution(
            coord_info_.pruning_table_location_, exec_ctx, parent))) {
            LOG_WARN("fail to allocate local sqc by data distribution", K(parent), K(ret));
          }
          LOG_TRACE("alloc_by_local_distribution", K(parent));
        } else if (has_reference_child) {
          if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_reference_child_distribution(parent))) {
            LOG_WARN("fail to allocate local sqc by data distribution", K(parent), K(child), K(ret));
          }
        } else if (OB_FAIL(ObPxSqcDistributionUtil::alloc_by_local_distribution(exec_ctx, parent))) {
          LOG_WARN("fail to allocate local sqc", K(parent), K(ret));
        }
        LOG_TRACE("alloc_by_child_distribution", K(child), K(parent));
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(set_temp_table_ctx_for_sqc(exec_ctx, parent))) {
          LOG_WARN("failed to set temp table ctx", K(ret));
        }
      }
    } else {
      // already in schedule, pass
    }
  }

  if (OB_FAIL(ret)) {
  } else if (parent.need_access_store() && parent.is_in_slave_mapping()
             && ObPQDistributeMethod::HASH == child.get_dist_method()
             && child.is_out_slave_mapping()) {
    if (OB_FAIL(ObPxSqcDistributionUtil::check_slave_mapping_location_constraint(child, parent))) {
      LOG_WARN("slave mapping location constraint not satisfy", K(parent.get_dfo_id()),
               K(child.get_dfo_id()));
    }
  }
  // Optimization branch: QC and its child dfo data channel allocation as early as possible when certain conditions are met
  bool can_prealloc = false;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(check_if_can_prealloc_xchg_ch(child, parent, can_prealloc))) {
      LOG_WARN("fail check can prealloc xchange, ingore", K(ret));
    } else if (can_prealloc) {
      if (OB_FAIL(do_fast_schedule(exec_ctx, child, parent))) {
        LOG_WARN("fail do fast schedule", K(parent), K(child), K(ret));
      }
    }
  }
  // Note: Do not worry about duplicate scheduling, the reason is as follows:
  // If the above do_fast_schedule successfully scheduled parent / child
  // Then its is_schedule status will be updated to true, below the schedule_dfo
  // Obviously would not be scheduled.
  //
    // schedule child first
    // because child can do some useful (e.g. scan) work while parent is scheduling
  if (OB_SUCC(ret)) {
    if (!child.is_scheduled()) {
      if (OB_FAIL(schedule_dfo(exec_ctx, child))) { // send DFO to each server
        LOG_WARN("fail schedule dfo", K(child), K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (!parent.is_scheduled()) {
      if (OB_FAIL(schedule_dfo(exec_ctx, parent))) { // send DFO to each server
        LOG_WARN("fail schedule dfo", K(parent), K(ret));
      }
    }
  }

  return ret;
}
