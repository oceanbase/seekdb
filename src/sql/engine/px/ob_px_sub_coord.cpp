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

#include "ob_px_sub_coord.h"
#include "sql/engine/px/exchange/ob_px_receive_op.h"
#include "sql/engine/px/exchange/ob_px_transmit_op.h"
#include "sql/engine/join/ob_join_filter_op.h"
#include "sql/engine/join/ob_hash_join_op.h"
#include "sql/engine/join/hash_join/ob_hash_join_vec_op.h"
#include "sql/engine/basic/ob_temp_table_access_op.h"
#include "sql/engine/basic/ob_temp_table_access_vec_op.h"
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "sql/executor/ob_task_spliter.h"
#include "storage/ddl/ob_direct_insert_sstable_ctx_new.h"
#include "sql/engine/window_function/ob_window_function_vec_op.h"
#include "sql/engine/basic/ob_select_into_op.h"
#include "storage/ddl/ob_direct_load_mgr_v3.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "storage/ddl/ob_column_clustered_dag.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::sql::dtl;
using namespace oceanbase::storage;
// Note: each Task within a thread is equal, the only difference is
// They get different task ranges from the granule
int ObPxSubCoord::pre_process()
{
  int ret = OB_SUCCESS;
  // 1. Register interrupt
  // 2. Construct ObGranuleIterator input structure, by Task carry out
  // 3. Establish the SQC-QC channel
  // 4. Get the worker thread, send Task. Report the number of worker threads to QC
  // 5. Establish SQC-Task channel
  // 6. SQC get Task Channel Map and distribute to Task
  // 7. Wait for Task execution to complete
  // 8. Report results to QC
  // 9. Unregister interrupt

  LOG_TRACE("begin ObPxSubCoord process", K(ret));
  int64_t dfo_id = sqc_arg_.sqc_.get_dfo_id();
  int64_t sqc_id = sqc_arg_.sqc_.get_sqc_id();
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  LOG_TRACE("TIMERECORD ", "reserve:=0 name:=SQC dfoid:", dfo_id,"sqcid:", sqc_id,"taskid:=-1 start:", ObTimeUtility::current_time());

  NG_TRACE(tag1);
  if (OB_ISNULL(sqc_arg_.exec_ctx_)
      || OB_ISNULL(sqc_arg_.op_spec_root_)
      || OB_ISNULL(sqc_arg_.des_phy_plan_)
      || OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(*sqc_arg_.exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sqc args should not be NULL", K(ret));
  } else if (OB_FAIL(try_prealloc_data_channel(sqc_ctx_, sqc_arg_.sqc_))) {
    LOG_WARN("fail try prealloc data channel", K(ret));
  } else {
    // ObOperator *op = NULL;
    if (OB_ISNULL(sqc_arg_.op_spec_root_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: op root is null", K(ret));
    } else if (OB_FAIL(rebuild_sqc_access_table_locations())) {
      LOG_WARN("fail to rebuild locations and tsc ops", K(ret));
    } else if (OB_FAIL(construct_p2p_dh_map())) {
      LOG_WARN("fail to construct p2p dh map", K(ret));
    } else if (OB_FAIL(setup_op_input(*sqc_arg_.exec_ctx_,
                                      *sqc_arg_.op_spec_root_,
                                      sqc_ctx_,
                                      sqc_arg_.sqc_.get_access_table_locations(),
                                      sqc_arg_.sqc_.get_access_table_location_keys()))) {
      LOG_WARN("fail to setup receive/transmit op input", K(ret));
    }
  }
  if (OB_SUCC(ret) && !sqc_arg_.sqc_.get_pruning_table_locations().empty()) {
    sqc_ctx_.gi_pump_.set_need_partition_pruning(true);
    OZ(sqc_ctx_.gi_pump_.set_pruning_table_location(sqc_arg_.sqc_.get_pruning_table_locations()));
  }

  if (OB_FAIL(ret)) {
    // Notify qc interrupt event
    if (IS_INTERRUPTED()) {
      // The current process was interrupted by QC, no longer sending interrupts to QC
    } else {
      ObInterruptUtil::update_schema_error_code(sqc_arg_.exec_ctx_, ret);
      (void) ObInterruptUtil::interrupt_qc(sqc_arg_.sqc_, ret, sqc_arg_.exec_ctx_);
    }
  }

  return ret;
}
// When SQC receives a DTL message from QC, process according to the message type
// Decide whether to start executing task
// Currently, when receiving TRANSMIT CHANNEL, RECEIVE CHANNEL messages
// Will trigger TASK execution.
//
// Here is an implication: when SQC receives this kind of message, it means QC has already
// Confirm that all SQCs in this DFO have sufficient resources to execute this DFO
// Will not terminate SQC execution due to any SQC worker resource shortage
int ObPxSubCoord::try_start_tasks(int64_t &dispatch_worker_count, bool is_fast_sqc)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(create_tasks(sqc_arg_, sqc_ctx_, is_fast_sqc))) {
    LOG_WARN("fail create tasks", K(ret));
  } else if (OB_FAIL(dispatch_tasks(sqc_arg_, sqc_ctx_, dispatch_worker_count, is_fast_sqc))) {
    LOG_WARN("fail to dispatch tasks to working threads", K(ret));
  }
  return ret;
}

void ObPxSubCoord::notify_dispatched_task_exit(int64_t dispatched_worker_count)
{
  (void)thread_worker_factory_.join();
  auto &tasks = sqc_ctx_.get_tasks();
  for (int64_t idx = 0; idx < dispatched_worker_count && dispatched_worker_count <= tasks.count();
       ++idx) {
    int tick = 1;
    ObPxTask &task = tasks.at(idx);
    while (false == task.is_task_state_set(SQC_TASK_EXIT)) {
      // Send an interrupt to unfinished tasks in the current sqc once per second
      // The time of the first interrupt is 100ms. Setting this time is to
      // cover px pool scheduling task delay
      if (tick % 1000 == 100) {
        ObPxSqcMeta &sqc = sqc_arg_.sqc_;
        (void)ObInterruptUtil::interrupt_tasks(sqc, OB_GOT_SIGNAL_ABORTING);
      }
      // If it has not exited after 10s, then print a log. According to the design, this situation should not occur
      if (tick++ % 10000 == 0) {
        LOG_INFO("waiting for task exit", K(idx), K(dispatched_worker_count), K(tick));
      }
      ob_usleep(1000);
    }
    LOG_TRACE("task exit", K(idx), K(tasks.count()), K(dispatched_worker_count), "dfo_id",
              task.dfo_id_, "sqc_id", task.sqc_id_, "task_id", task.task_id_);
  }
}

int ObPxSubCoord::init_exec_env(ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(session = GET_MY_SESSION(exec_ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("deserialized exec ctx without phy plan session set. Unexpected", K(ret));
  } else if (OB_ISNULL(plan_ctx = GET_PHY_PLAN_CTX(exec_ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("deserialized exec ctx without phy plan ctx set. Unexpected", K(ret));
  } else {
    session->set_cur_phy_plan(sqc_arg_.des_phy_plan_);
    exec_ctx.reference_my_plan(sqc_arg_.des_phy_plan_);
    THIS_WORKER.set_timeout_ts(plan_ctx->get_timeout_timestamp());
  }
  return ret;
}

int ObPxSubCoord::get_tsc_or_dml_op_tablets(
    ObOpSpec &root,
    const DASTabletLocIArray &tsc_locations,
    const ObIArray<ObSqcTableLocationKey> &tsc_location_keys,
    common::ObIArray<const ObTableScanSpec*> &scan_ops,
    common::ObIArray<DASTabletLocArray> &tablets_array)
{
  int ret = OB_SUCCESS;
  ObArray<const ObTableModifySpec*> dml_ops;
  if (OB_FAIL(ObTaskSpliter::find_scan_ops(scan_ops, root))) {
    LOG_WARN("fail get scan ops", K(ret));
  } else if (OB_FAIL(ObPXServerAddrUtil::find_dml_ops(dml_ops, root))) {
    LOG_WARN("fail to find dml ops", K(ret));
  } else if (scan_ops.empty() && dml_ops.empty()) {
    /*do nothing*/
  } else if (!dml_ops.empty() && 1 != dml_ops.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected dml ops count", K(dml_ops.count()), K(ret));
  } else if (OB_FAIL(ObPxPartitionLocationUtil::get_all_tables_tablets(
        scan_ops,
        tsc_locations,
        tsc_location_keys,
        !dml_ops.empty() ? tsc_location_keys.at(0) : ObSqcTableLocationKey(),
        tablets_array))) {
    LOG_WARN("get all table scan's partition failed", K(ret));
  }
  return ret;
}

int ObPxSubCoord::setup_gi_op_input(ObExecContext &ctx,
    ObOpSpec &root,
    ObSqcCtx &sqc_ctx,
    const DASTabletLocIArray &tsc_locations,
    const ObIArray<ObSqcTableLocationKey> &tsc_location_keys)
{
  int ret = OB_SUCCESS;
  if (IS_PX_GI(root.get_type())) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    common::ObArray<DASTabletLocArray> tablets_array;
    ObSEArray<const ObTableScanSpec*, 1> scan_ops;
    ObTableModifySpec *dml_op = NULL;
    if (OB_SUCC(ret)) {
      IGNORE_RETURN try_get_dml_op(root, dml_op);
    }
    // pre query range and init scan input (for compatible)
    if (OB_SUCC(ret)) {
      if (OB_FAIL(get_tsc_or_dml_op_tablets(root,
          tsc_locations, tsc_location_keys, scan_ops,
          tablets_array))) {
        LOG_WARN("fail get scan ops", K(ret));
      } else {
        ObGranuleIteratorSpec *gi_op = reinterpret_cast<ObGranuleIteratorSpec *>(&root);
        ObOperatorKit *kit = ctx.get_operator_kit(gi_op->id_);
        if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("operator is NULL", K(ret), KP(kit));
        } else {
          ObGIOpInput *gi_input = static_cast<ObGIOpInput*>(kit->input_);
          int64_t parallelism = sqc.get_task_count();
          int64_t split_gi_task_start = ObTimeUtil::current_time();
          if (OB_FAIL(sqc_ctx.gi_pump_.init_pump_args(&ctx, sqc, gi_op, scan_ops, dml_op,
                                                      tablets_array, sqc_ctx.partitions_info_))) {
            LOG_WARN("fail to init pump args", K(ret));
          } else {
            gi_input->set_granule_pump(&sqc_ctx.gi_pump_);
            gi_input->add_table_location_keys(scan_ops);
            LOG_TRACE("setup gi op input", K(gi_input), K(&sqc_ctx.gi_pump_), K(gi_op->id_),
                      K(sqc_ctx.gi_pump_.get_task_array_map()), K(sqc.get_locations_order()));
          }
          if (OB_FAIL(ret)) {
          } else if (gi_op->enable_adaptive_task_splitting_ && parallelism > 1) {
            int64_t split_gi_task_cost = ObTimeUtil::current_time() - split_gi_task_start;
            int64_t tsc_id = scan_ops.at(0)->id_;
            const GITaskArrayMap &array_map = sqc_ctx.gi_pump_.get_task_array_map();
            int64_t initial_task_count = 0;
            for (int64_t i = 0; i < array_map.count() && OB_SUCC(ret); ++i) {
              if (array_map.at(i).tsc_op_id_ == tsc_id) {
                initial_task_count = array_map.at(i).taskset_array_.at(0).task_count_;
              }
            }
            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(gi_input->init_task_rebalancer(&ctx, parallelism, split_gi_task_cost,
                                                              initial_task_count,
                                                              gi_op->get_id()))) {
              LOG_WARN("failed to init task rebalancer");
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObPxSubCoord::pre_setup_op_input(ObExecContext &ctx,
    ObOpSpec &root,
    ObSqcCtx &sqc_ctx,
    const DASTabletLocIArray &tsc_locations,
    const ObIArray<ObSqcTableLocationKey> &tsc_location_keys)
{
  int ret = OB_SUCCESS;
  if (IS_PX_GI(root.get_type())) {
    // if it's not single tsc leaf dfo,
    // setup_gi_op_input will be called later by subcoord preprocess func
    if (is_single_tsc_leaf_dfo_ &&
        OB_FAIL(setup_gi_op_input(ctx, root, sqc_ctx,
        tsc_locations, tsc_location_keys))) {
      LOG_WARN("fail to setup gi op input", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (IS_PX_RECEIVE(root.get_type())) {
      // Encountering the receive operator, terminate downward iteration
    } else {
      for (int32_t i = 0; i < root.get_child_num() && OB_SUCC(ret); ++i) {
        ObOpSpec *child = root.get_child(i);
        if (OB_FAIL(SMART_CALL((pre_setup_op_input(ctx, *child,
            sqc_ctx, tsc_locations, tsc_location_keys))))) {
          LOG_WARN("pre_setup_op_input failed", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObPxSubCoord::setup_op_input(ObExecContext &ctx,
                                 ObOpSpec &root,
                                 ObSqcCtx &sqc_ctx,
                                 const DASTabletLocIArray &tsc_locations,
                                 const ObIArray<ObSqcTableLocationKey> &tsc_location_keys)
{
  int ret = OB_SUCCESS;
  ASH_ITEM_ATTACH_GUARD(plan_line_id, root.id_);
  if (IS_PX_RECEIVE(root.get_type())) {
    ObPxReceiveSpec *receive_op = reinterpret_cast<ObPxReceiveSpec *>(&root);
    ObOperatorKit *kit = ctx.get_operator_kit(receive_op->id_);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), KP(kit));
    } else {
      ObPxReceiveOpInput *receive_input = static_cast<ObPxReceiveOpInput*>(kit->input_);
      receive_input->set_ignore_vtable_error(sqc_arg_.sqc_.is_ignore_vtable_error());
      receive_input->set_sqc_proxy(sqc_ctx.sqc_proxy_);
      receive_input->set_ignore_vtable_error(sqc_arg_.sqc_.is_ignore_vtable_error());
      LOG_TRACE("setup op input",
               "op_id", root.get_id(),
               KP(receive_input));
    }
  } else if (IS_PX_TRANSMIT(root.get_type())) {
    ObPxTransmitSpec *transmit_op = reinterpret_cast<ObPxTransmitSpec *>(&root);
    ObOperatorKit *kit = ctx.get_operator_kit(transmit_op->id_);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), KP(kit));
    } else {
      ObPxTransmitOpInput *transmit_input = static_cast<ObPxTransmitOpInput*>(kit->input_);
      transmit_input->set_sqc_proxy(sqc_ctx.sqc_proxy_);
      LOG_TRACE("setup op input",
               "op_id", root.get_id(),
               KP(transmit_input));
    }
  } else if (IS_DML(root.get_type())) {
    bool need_start_ddl = false;

    if (OB_FAIL(check_need_start_ddl(need_start_ddl))) {
      LOG_WARN("check need start ddl failed", K(ret));
    } else if (need_start_ddl) {
      if (OB_FAIL(start_ddl())) {
        LOG_WARN("start ddl failed", K(ret));
      }
#ifdef ERRSIM
      if (OB_SUCC(ret)) {
        ret = OB_E(EventTable::EN_DDL_START_FAIL) OB_SUCCESS;
      }
#endif
    }
  } else if (IS_PX_GI(root.get_type())) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObGranuleIteratorSpec *gi_op = reinterpret_cast<ObGranuleIteratorSpec *>(&root);
    ObOperatorKit *kit = ctx.get_operator_kit(gi_op->id_);
    ObGIOpInput *gi_input = static_cast<ObGIOpInput*>(kit->input_);
    if (!is_single_tsc_leaf_dfo_ &&
        OB_FAIL(setup_gi_op_input(ctx, root, sqc_ctx,
        tsc_locations, tsc_location_keys))) {
      LOG_WARN("fail to setup gi op input", K(ret));
    } else {
      // dop may changed after dop auto scaling, we should modify the dop in gi
      gi_input->set_parallelism(sqc.get_task_count());
      sqc_ctx.gi_pump_.set_parallelism(sqc.get_task_count());
      if (gi_op->enable_adaptive_task_splitting_ && gi_input->task_balancer_ != nullptr) {
        gi_input->task_balancer_->scale_total_worker_count(sqc.get_task_count());
      }

    }
  } else if (IS_PX_JOIN_FILTER(root.get_type())) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObJoinFilterSpec *filter_spec = reinterpret_cast<ObJoinFilterSpec *>(&root);
    ObJoinFilterOpInput *filter_input = NULL;
    int64_t tenant_id = ctx.get_my_session()->get_effective_tenant_id();
    ObOperatorKit *kit = ctx.get_operator_kit(root.id_);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), KP(kit));
    } else if (FALSE_IT(filter_input = static_cast<ObJoinFilterOpInput*>(kit->input_))) {
    } else if (FALSE_IT(filter_input->set_px_sequence_id(
          sqc.get_interrupt_id().px_interrupt_id_.first_))) {
    } else if (OB_FAIL(filter_input->load_runtime_config(*filter_spec, ctx))) {
      LOG_WARN("fail to load runtime config", K(ret));
    } else if (filter_spec->is_create_mode()) {
      int64_t filter_len = filter_spec->get_filter_length();
      filter_input->set_sqc_proxy(sqc_ctx.sqc_proxy_);
      if (!filter_spec->is_shared_join_filter()) {
        /*do nothing*/
      } else if (OB_FAIL(filter_input->init_share_info(*filter_spec,
          ctx, sqc.get_task_count(),
          filter_spec->is_shuffle_? sqc.get_sqc_count() : 1))) {
        LOG_WARN("fail to init share info", K(ret));
      } else {
        if (OB_FAIL(all_shared_rf_msgs_.push_back(filter_input->share_info_.shared_msgs_))) {
          LOG_WARN("fail to push back rf msgs", K(ret));
        }
        if (OB_FAIL(ret) && filter_input->share_info_.shared_msgs_ != 0) {
          ObArray<ObP2PDatahubMsgBase *> *array_ptr =
          reinterpret_cast<ObArray<ObP2PDatahubMsgBase *> *>(filter_input->share_info_.shared_msgs_);
          for (int j = 0; j < array_ptr->count(); ++j) {
            if (OB_NOT_NULL(array_ptr->at(j))) {
              array_ptr->at(j)->destroy();
            }
          }
          if (!array_ptr->empty()) {
            array_ptr->reset();
          }
        }
      }
    }
  } else if (root.get_type() == PHY_TEMP_TABLE_ACCESS) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObTempTableAccessOpInput *access_input = NULL;
    uint64_t *access_count_ptr = NULL;
    ObOperatorKit *kit = ctx.get_operator_kit(root.id_);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), KP(kit));
    } else if (OB_ISNULL(access_count_ptr = (uint64_t *)ctx.get_allocator().alloc(sizeof(uint64_t)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc count_ptr", K(ret));
    } else {
      access_input = static_cast<ObTempTableAccessOpInput*>(kit->input_);
      ObTempTableAccessOpSpec &access_op = static_cast<ObTempTableAccessOpSpec&>(root);
      bool find = false;
      for (int64_t i = 0; OB_SUCC(ret) && !find && i < sqc.get_temp_table_ctx().count(); ++i) {
        ObSqlTempTableCtx &temp_table_ctx = sqc.get_temp_table_ctx().at(i);
        if (access_op.temp_table_id_ == temp_table_ctx.temp_table_id_) {
          for (int64_t j = 0; OB_SUCC(ret) && !find && j < temp_table_ctx.interm_result_infos_.count(); ++j) {
            if (sqc.get_exec_addr() == temp_table_ctx.interm_result_infos_.at(j).addr_) {
              ObTempTableResultInfo &info = temp_table_ctx.interm_result_infos_.at(j);
              std::random_shuffle(info.interm_result_ids_.begin(), info.interm_result_ids_.end());
              if (OB_FAIL(access_input->interm_result_ids_.assign(info.interm_result_ids_))) {
                LOG_WARN("failed to assign result ids", K(ret));
              } else {
                find = true;
              }
            }
          }
        }
      }
      if (OB_FAIL(ret)) {
        //do nothing
      } else if (!find) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("temp table not found", K(access_op.temp_table_id_), K(ret));
      } else {
        *access_count_ptr = access_input->interm_result_ids_.count();
        access_input->unfinished_count_ptr_ = reinterpret_cast<uint64_t>(access_count_ptr);
      }
    }
  }  else if (root.get_type() == PHY_VEC_TEMP_TABLE_ACCESS) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObTempTableAccessVecOpInput *access_input = NULL;
    uint64_t *access_count_ptr = NULL;
    ObOperatorKit *kit = ctx.get_operator_kit(root.id_);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), KP(kit));
    } else if (OB_ISNULL(access_count_ptr = (uint64_t *)ctx.get_allocator().alloc(sizeof(uint64_t)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc count_ptr", K(ret));
    } else {
      access_input = static_cast<ObTempTableAccessVecOpInput*>(kit->input_);
      ObTempTableAccessVecOpSpec &access_op = static_cast<ObTempTableAccessVecOpSpec&>(root);
      bool find = false;
      for (int64_t i = 0; OB_SUCC(ret) && !find && i < sqc.get_temp_table_ctx().count(); ++i) {
        ObSqlTempTableCtx &temp_table_ctx = sqc.get_temp_table_ctx().at(i);
        if (access_op.temp_table_id_ == temp_table_ctx.temp_table_id_) {
          for (int64_t j = 0; OB_SUCC(ret) && !find && j < temp_table_ctx.interm_result_infos_.count(); ++j) {
            if (sqc.get_exec_addr() == temp_table_ctx.interm_result_infos_.at(j).addr_) {
              ObTempTableResultInfo &info = temp_table_ctx.interm_result_infos_.at(j);
              std::random_shuffle(info.interm_result_ids_.begin(), info.interm_result_ids_.end());
              if (OB_FAIL(access_input->interm_result_ids_.assign(info.interm_result_ids_))) {
                LOG_WARN("failed to assign result ids", K(ret));
              } else {
                find = true;
              }
            }
          }
        }
      }
      if (OB_FAIL(ret)) {
        //do nothing
      } else if (!find) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("temp table not found", K(access_op.temp_table_id_), K(ret));
      } else {
        *access_count_ptr = access_input->interm_result_ids_.count();
        access_input->unfinished_count_ptr_ = reinterpret_cast<uint64_t>(access_count_ptr);
      }
    }
  } else if (root.get_type() == PHY_HASH_JOIN) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObHashJoinInput *hj_input = NULL;
    ObOperatorKit *kit = ctx.get_operator_kit(root.id_);
    ObHashJoinSpec *hj_spec = reinterpret_cast<ObHashJoinSpec *>(&root);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), KP(kit));
    } else if (FALSE_IT(hj_input = static_cast<ObHashJoinInput*>(kit->input_))) {
    } else if (hj_spec->is_shared_ht_ && OB_FAIL(hj_input->init_shared_hj_info(ctx.get_allocator(), sqc.get_task_count()))) {
      LOG_WARN("failed to init shared hash join info", K(ret));
    } else {
      LOG_TRACE("debug hj input", K(hj_spec->is_shared_ht_));
    }
  } else if (root.get_type() == PHY_VEC_HASH_JOIN) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObHashJoinVecInput *hj_input = NULL;
    ObOperatorKit *kit = ctx.get_operator_kit(root.id_);
    ObHashJoinVecSpec *hj_spec = reinterpret_cast<ObHashJoinVecSpec *>(&root);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), KP(kit));
    } else if (FALSE_IT(hj_input = static_cast<ObHashJoinVecInput*>(kit->input_))) {
    } else if (hj_spec->is_shared_ht_
               && OB_FAIL(hj_input->init_shared_hj_info(ctx.get_allocator(),
                                                        sqc.get_task_count()))) {
      LOG_WARN("failed to init shared hash join info", K(ret));
    } else {
      LOG_TRACE("debug hj input", K(hj_spec->is_shared_ht_));
    }

  } else if (root.get_type() == PHY_WINDOW_FUNCTION) {
    // set task_count to ObWindowFunctionOpInput for wf pushdown
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObWindowFunctionOpInput *wf_input = NULL;
    ObOperatorKit *kit = ctx.get_operator_kit(root.id_);
    ObWindowFunctionSpec *wf_spec = reinterpret_cast<ObWindowFunctionSpec *>(&root);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is NULL", K(ret), KP(kit));
    } else if (FALSE_IT(wf_input = static_cast<ObWindowFunctionOpInput*>(kit->input_))) {
    } else if (wf_spec->is_participator()) {
      wf_input->set_local_task_count(sqc.get_task_count());
      wf_input->set_total_task_count(sqc.get_total_task_count());
      if (OB_FAIL(wf_input->init_wf_participator_shared_info(
          ctx.get_allocator(), sqc.get_task_count()))) {
        LOG_WARN("failed to init_wf_participator_shared_info", K(ret), K(sqc.get_task_count()));
      }
      LOG_DEBUG("debug wf input", K(wf_spec->role_type_), K(sqc.get_task_count()),
                K(sqc.get_total_task_count()));
    }
  } else if (root.get_type() == PHY_VEC_SORT) {
    // TODO XUNSI: if shared topn filter, init the shared topn msg here
  } else if (root.get_type() == PHY_VEC_WINDOW_FUNCTION) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObWindowFunctionVecOpInput *wf_input = NULL;
    ObOperatorKit *kit = ctx.get_operator_kit(root.id_);
    ObWindowFunctionVecSpec *wf_spec = reinterpret_cast<ObWindowFunctionVecSpec *>(&root);
    if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("operator is null", K(ret), K(kit));
    } else if (FALSE_IT(wf_input = static_cast<ObWindowFunctionVecOpInput *>(kit->input_))) {
    } else if (wf_spec->is_participator()) {
      wf_input->set_local_task_count(sqc.get_task_count());
      wf_input->set_total_task_count(sqc.get_total_task_count());
      if (OB_FAIL(wf_input->init_wf_participator_shared_info(ctx.get_allocator(),
                                                             sqc.get_task_count()))) {
        LOG_WARN("init wf participator shared info failed", K(ret));
      }
      LOG_DEBUG("debug wf input", K(wf_spec->role_type_), K(sqc.get_task_count()),
                K(sqc.get_total_task_count()));
    }
  } else if (root.get_type() == PHY_SELECT_INTO) {
    ObPxSqcMeta &sqc = sqc_arg_.sqc_;
    ObSelectIntoSpec *select_into_spec = reinterpret_cast<ObSelectIntoSpec *>(&root);
    sql::ObExternalFileFormat external_properties;
    ObString &properties = select_into_spec->external_properties_.str_;
    if (properties.empty()) {
      // do nothing
    } else if (OB_FAIL(external_properties.load_from_string(properties, ctx.get_allocator()))) {
      LOG_WARN("failed to init external_odps_format", K(ret));
    } else if (sql::ObExternalFileFormat::ODPS_FORMAT != external_properties.format_type_) {
      // do nothing
    } else { 
      if (!GCONF._use_odps_jni_connector) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not support odps cpp connector", K(ret));
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not support odps jni connector", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(root.register_to_datahub(ctx))) {
      LOG_WARN("fail register op to datahub", K(ret));
    } else if (OB_FAIL(root.register_init_channel_msg(ctx))) {
      LOG_WARN("failed to register init channel msg", K(ret));
    }
  }
  if (IS_PX_RECEIVE(root.get_type())) {
    // Encounter receive operator, terminate downward iteration
  } else if (OB_SUCC(ret)) {
    for (int32_t i = 0; i < root.get_child_cnt() && OB_SUCC(ret); ++i) {
      ObOpSpec *child = root.get_child(i);
      if (OB_ISNULL(child)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL child op unexpected", K(ret));
      } else {
        ret = SMART_CALL(setup_op_input(ctx, *child, sqc_ctx, tsc_locations,
            tsc_location_keys));
      }
    }
  }
  return ret;
}
// Implementation method: Through the RPC method to assign threads to the task, if the assignment fails (thread not obtained successfully within 50ms)
//           then reduce the number of tasks
int ObPxSubCoord::create_tasks(ObPxRpcInitSqcArgs &sqc_arg, ObSqcCtx &sqc_ctx, bool is_fast_sqc)
{
  int ret = OB_SUCCESS;
  ObPxSqcMeta &sqc = sqc_arg.sqc_;
  ObSQLSessionInfo *session = NULL;
  if (OB_ISNULL(sqc_arg.exec_ctx_)
      || OB_ISNULL(sqc_arg.op_spec_root_)
      || OB_ISNULL(sqc_arg.des_phy_plan_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sqc args should not be NULL", K(ret));
  } else if (OB_FAIL(sqc_ctx.reserve_task_mem(sqc.get_task_count()))) {
    // To ensure that the task can be recorded after creation, allocate record memory first
    LOG_WARN("fail pre alloc memory", K(sqc), K(ret));
  } else if (OB_UNLIKELY(NULL == (session = sqc_arg.exec_ctx_->get_my_session()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr session", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < sqc.get_task_count(); ++i) {
    ObPxTask task;
    const ObAddr &sqc_exec_addr = sqc.get_exec_addr();
    const ObAddr &task_exec_addr = sqc.get_exec_addr();
    const ObAddr &qc_exec_addr = sqc.get_qc_addr();
    task.set_task_id(i);
    if (sqc.get_branch_id_base()) {
      task.set_branch_id(sqc.get_branch_id_base() + i);
    }
    task.set_sqc_addr(sqc_exec_addr);
    task.set_exec_addr(task_exec_addr);
    task.set_qc_addr(qc_exec_addr);
    task.set_sqc_id(sqc.get_sqc_id());
    task.set_dfo_id(sqc.get_dfo_id());
    task.set_execution_id(sqc.get_execution_id());
    task.set_qc_id(sqc.get_qc_id());
    task.set_interrupt_id(sqc.get_interrupt_id());
    task.set_fulltree(sqc.is_fulltree());
    task.set_use_local_thread(is_fast_sqc);
    if (OB_SUCC(ret)) {
      ObPxTask *task_ptr = nullptr;
      if (OB_FAIL(sqc_ctx.add_task(task, task_ptr))) {
        LOG_ERROR("fail add task. should always SUCC as mem reserved", K(ret));
      } else if (OB_ISNULL(task_ptr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("task ptr should not be null", KP(task_ptr), K(ret));
      }
    }
  }
  return ret;
}

int ObPxSubCoord::dispatch_tasks(ObPxRpcInitSqcArgs &sqc_arg, ObSqcCtx &sqc_ctx, int64_t &dispatch_worker_count, bool is_fast_sqc)
{
  int ret = OB_SUCCESS;
  dispatch_worker_count = 0;
  ObPxSqcMeta &sqc = sqc_arg.sqc_;
  if (OB_ISNULL(sqc_arg.exec_ctx_)
      || OB_ISNULL(sqc_arg.sqc_handler_)
      || OB_ISNULL(sqc_arg.op_spec_root_)
      || OB_ISNULL(sqc_arg.des_phy_plan_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Invalid sqc args", K(ret), K(sqc));
  } else if (is_fast_sqc) {
    dispatch_worker_count = 0;
    ret = dispatch_task_to_local_thread(sqc_arg, sqc_ctx, sqc);
  } else {
    // execute all task in thread pool, non-block call
    for (int64_t i = 0; OB_SUCC(ret) && i < sqc.get_task_count(); ++i) {
      sqc_arg.sqc_handler_->inc_ref_count();
      ret = dispatch_task_to_thread_pool(sqc_arg, sqc_ctx, sqc, i);
      if (OB_SUCC(ret)) {
        ++dispatch_worker_count;
      } else {
        sqc_arg.sqc_handler_->dec_ref_count();
      }
    }
  }
  return ret;
}


int ObPxSubCoord::dispatch_task_to_local_thread(ObPxRpcInitSqcArgs &sqc_arg,
                                                ObSqcCtx &sqc_ctx,
                                                ObPxSqcMeta &sqc)
{
  int ret = OB_SUCCESS;
  ObPxRpcInitTaskArgs args;
  ObPxTask *task_ptr = nullptr;
  int64_t task_idx = 0; // only 1 task
  if (OB_FAIL(sqc_ctx.get_task(task_idx, task_ptr))) {
    LOG_ERROR("fail add task. should always SUCC as mem reserved", K(ret));
  } else if (OB_ISNULL(task_ptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task ptr should not be null", KP(task_ptr), K(ret));
  } else {
    args.exec_ctx_ = sqc_arg.exec_ctx_;
    args.op_spec_root_ = sqc_arg.op_spec_root_;
    args.static_engine_root_ = sqc_arg.static_engine_root_;
    args.des_phy_plan_ = sqc_arg.des_phy_plan_;
    args.task_ = *task_ptr;
    args.sqc_task_ptr_ = task_ptr; // pass memory address to task execution thread for directly updating task state
    // Record the start time of scheduling the task
    args.sqc_handler_ = sqc_arg.sqc_handler_;
  }

  ObPxWorkerRunnable *worker = nullptr;
  if (OB_FAIL(ret)) {
    // fail
  } else if (OB_ISNULL(worker = local_worker_factory_.create_worker())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail create new worker", K(sqc), K(ret));
  } else if (OB_FAIL(worker->run(args))) {
    LOG_WARN("fail run task", K(sqc), K(ret));
  } else {
    LOG_TRACE("success execute local root task", K(task_idx), "task", args.task_);
  }
  return ret;
}

int ObPxSubCoord::dispatch_task_to_thread_pool(ObPxRpcInitSqcArgs &sqc_arg,
                                              ObSqcCtx &sqc_ctx,
                                              ObPxSqcMeta &sqc,
                                              int64_t task_idx)

{
  int ret = OB_SUCCESS;
  ObPxRpcInitTaskArgs args;
  ObPxTask *task_ptr = nullptr;
  ObPxWorkerRunnable *worker = nullptr;
  if (OB_FAIL(sqc_ctx.get_task(task_idx, task_ptr))) {
    LOG_ERROR("fail add task. should always SUCC as mem reserved", K(ret));
  } else if (OB_ISNULL(task_ptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task ptr should not be null", KP(task_ptr), K(ret));
  } else {
    if (nullptr != sqc_arg.op_spec_root_) {
      args.set_serialize_param(*sqc_arg.exec_ctx_,
                             *sqc_arg.op_spec_root_,
                             *sqc_arg.des_phy_plan_);
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: op root is null", K(ret));
    }
    args.task_ = *task_ptr;
    args.sqc_task_ptr_ = task_ptr; // pass memory address to task execution thread for directly updating task state
    // Record the start time of scheduling the task
    args.sqc_handler_ = sqc_arg.sqc_handler_;
  }

  if (OB_FAIL(ret)) {
    // fail
  } else if (OB_ISNULL(worker =
      static_cast<ObPxWorkerRunnable *>(thread_worker_factory_.create_worker()))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail create new worker", K(ret));
  } else if (OB_FAIL(worker->run(args))) {
    // DOP could not be met, no longer continue allocation
    task_ptr->set_result(ret);
    LOG_ERROR("can't alloc task thread."
              "reservation logic behavior unexpected. reservation logic behavior unexpected",
              "request_dop", sqc.get_task_count(),
              K(task_idx),
              K(sqc),
              K(ret));
  } else {
    LOG_TRACE("success issue one task", K(task_idx), K(sqc));
  }
  return ret;
}

int ObPxSubCoord::try_prealloc_data_channel(ObSqcCtx &sqc_ctx, ObPxSqcMeta &sqc)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(try_prealloc_transmit_channel(sqc_ctx, sqc))) {
    LOG_WARN("fail preallocate transmit channel", K(ret));
  } else if (OB_FAIL(try_prealloc_receive_channel(sqc_ctx, sqc))) {
    LOG_WARN("fail preallocate receive channel", K(ret));
  }
  return ret;
}
// If QC has already assigned a transmit channel to the worker under SQC, set it up in advance
int ObPxSubCoord::try_prealloc_transmit_channel(ObSqcCtx &sqc_ctx, ObPxSqcMeta &sqc)
{
  int ret = OB_SUCCESS;
  if (sqc.is_prealloc_transmit_channel()) {
    if (OB_FAIL(sqc_ctx.transmit_data_ch_provider_.add_msg(sqc.get_transmit_channel_msg()))) {
      LOG_WARN("fail set transmit data ch msg", K(ret));
    }
  }
  return ret;
}
// If QC has already assigned a transmit channel to the worker under SQC, set it up in advance
int ObPxSubCoord::try_prealloc_receive_channel(ObSqcCtx &sqc_ctx, ObPxSqcMeta &sqc)
{
  int ret = OB_SUCCESS;
  if (sqc.is_prealloc_receive_channel()) {
    if (sqc.recieve_use_interm_result()) {
      auto &msgs = sqc.get_serial_receive_channels();
      ARRAY_FOREACH_X(msgs, idx, cnt, OB_SUCC(ret)) {
        if (OB_FAIL(sqc_ctx.receive_data_ch_provider_.add_msg(msgs.at(idx)))) {
          LOG_WARN("fail set receive data ch msg", K(ret));
        }
      }
    } else {
      if (OB_FAIL(sqc_ctx.receive_data_ch_provider_.add_msg(sqc.get_receive_channel_msg()))) {
        LOG_WARN("fail set receive data ch msg", K(ret));
      }
    }
  }
  return ret;
}

void ObPxSubCoord::destroy_shared_rf_msgs()
{
  for (int i = 0; i < all_shared_rf_msgs_.count(); ++i) {
    ObArray<ObP2PDatahubMsgBase *> *array_ptr =
        reinterpret_cast<ObArray<ObP2PDatahubMsgBase *> *>(all_shared_rf_msgs_.at(i));
    for (int j = 0; OB_NOT_NULL(array_ptr) && j < array_ptr->count(); ++j) {
      array_ptr->at(j)->destroy();
    }
    if (OB_NOT_NULL(array_ptr) && !array_ptr->empty()) {
      array_ptr->reset();
    }
  }
}

// the last worker will invoke this function
int ObPxSubCoord::end_process()
{
  LOG_TRACE("start sqc end process");
  int ret = OB_SUCCESS;
  int64_t dfo_id = sqc_arg_.sqc_.get_dfo_id();
  int64_t sqc_id = sqc_arg_.sqc_.get_sqc_id();
  ObPhysicalPlanCtx *phy_plan_ctx = nullptr;

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(sqc_arg_.exec_ctx_)
        || OB_ISNULL(sqc_arg_.op_spec_root_)
        || OB_ISNULL(sqc_arg_.des_phy_plan_)
        || OB_ISNULL(phy_plan_ctx = GET_PHY_PLAN_CTX(*sqc_arg_.exec_ctx_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sqc args should not be NULL", K(ret));
    } else if (OB_FAIL(sqc_ctx_.sqc_proxy_.check_task_finish_status(phy_plan_ctx->get_timeout_timestamp()))) {
      LOG_WARN("fail check task finish status", K(ret));
    }
  }
  void destroy_shared_rf_msgs();

  NG_TRACE(tag3);
  LOG_TRACE("exit ObPxSubCoord process", K(ret));
  LOG_TRACE("TIMERECORD ", "reserve:=0 name:=SQC dfoid:", dfo_id,"sqcid:", sqc_id,"taskid:=-1 end:", ObTimeUtility::current_time());
  return ret;
}

int ObPxSubCoord::check_need_start_ddl(bool &need_start_ddl)
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = sqc_arg_.exec_ctx_;
  ObSQLSessionInfo *my_session = nullptr;
  need_start_ddl = false;
  if (OB_ISNULL(exec_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, exec ctx must not be nullptr", K(ret));
  } else if (OB_ISNULL(my_session = GET_MY_SESSION(*exec_ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, session must not be nullptr", K(ret));
  } else if (my_session->get_ddl_info().is_ddl()) {
    need_start_ddl = true;
  }
  return ret;
}

int ObPxSubCoord::start_ddl()
{
  int ret = OB_SUCCESS;
  ObExecContext *exec_ctx = sqc_arg_.exec_ctx_;
  ObPhysicalPlanCtx *plan_ctx = nullptr;
  const ObPhysicalPlan *phy_plan = nullptr;
  ObIArray<ObSqcTableLocationKey> &location_keys = sqc_arg_.sqc_.get_access_table_location_keys();
  if (OB_UNLIKELY(nullptr != ddl_dag_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ddl dag has already been inited", K(ret), KPC(ddl_dag_));
  } else if (OB_ISNULL(exec_ctx)
      || OB_ISNULL(plan_ctx = GET_PHY_PLAN_CTX(*exec_ctx))
      || OB_ISNULL(phy_plan = plan_ctx->get_phy_plan())
      || location_keys.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(ret), KP(exec_ctx), KP(plan_ctx), KP(phy_plan), K(location_keys.count()));
  } else {
    const int64_t ddl_task_id = phy_plan->get_ddl_task_id();
    const int64_t ddl_execution_id = phy_plan->get_ddl_execution_id();
    const int64_t ddl_table_id = phy_plan->get_ddl_table_id();

    uint64_t tenant_data_version = 0;
    int64_t snapshot_version = 0;
    int64_t schema_version = 0;
    bool is_no_logging = false;
    share::ObDDLTaskStatus unused_task_status = share::ObDDLTaskStatus::PREPARE;
    uint64_t unused_taget_object_id = 0;
    bool is_offline_index_rebuild = false;

    if (OB_FAIL(ObDDLUtil::get_data_information(MTL_ID(), ddl_task_id,
            tenant_data_version, snapshot_version, unused_task_status, unused_taget_object_id, schema_version, is_no_logging, is_offline_index_rebuild))) {
      LOG_WARN("get ddl task info failed", K(ret), K(ddl_task_id));
    } else if (tenant_data_version < DDL_IDEM_DATA_FORMAT_VERSION ) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported, reject request util finish update", K(ret), K(tenant_data_version));
    }

    if (OB_FAIL(ret))  {
    } else {
      const int64_t px_thread_count = sqc_arg_.sqc_.get_task_count();
      ObColumnClusteredDagInitParam ddl_dag_param;
      ddl_dag_param.direct_load_type_ = ObDirectLoadMgrUtil::ddl_get_direct_load_type(GCTX.is_shared_storage_mode(), tenant_data_version);
      ddl_dag_param.ddl_thread_count_ = px_thread_count;
      ddl_dag_param.px_thread_count_ = px_thread_count;
      ddl_dag_param.ddl_task_param_.ddl_task_id_ = ddl_task_id;
      ddl_dag_param.ddl_task_param_.execution_id_ = ddl_execution_id;
      ddl_dag_param.ddl_task_param_.tenant_data_version_ = tenant_data_version;
      ddl_dag_param.ddl_task_param_.snapshot_version_ = snapshot_version;
      ddl_dag_param.ddl_task_param_.target_table_id_ = ddl_table_id;
      ddl_dag_param.ddl_task_param_.schema_version_ = schema_version; // for idempotence, the schema version must be fixed, so get it from task record
      ddl_dag_param.ddl_task_param_.is_no_logging_ = is_no_logging;
      ddl_dag_param.ddl_task_param_.is_offline_index_rebuild_ = is_offline_index_rebuild;
      if (OB_FAIL(get_participants(sqc_arg_.sqc_, ddl_table_id, ddl_dag_param.ls_tablet_ids_))) {
        LOG_WARN("fail to get tablet ids", K(ret), K(ddl_task_id), K(ddl_table_id));
      } else if (OB_FAIL(ObTenantDagScheduler::alloc_dag(exec_ctx->get_allocator(), false/*is_ha_dag*/, ddl_dag_))) {
        LOG_WARN("alloc ddl dag failed", K(ret), K(ddl_task_id), KP(ddl_dag_));
      } else if (OB_FAIL(ddl_dag_->init(&ddl_dag_param, nullptr, true/*trace id*/))) {
        LOG_WARN("init ddl dag failed", K(ret), K(ddl_dag_param));
      } else if (OB_FAIL(ddl_dag_threads_.init(px_thread_count, ddl_dag_, GET_MY_SESSION(*exec_ctx)))) {
        LOG_WARN("init ddl dag thread pool failed", K(ret));
      } else if (OB_FAIL(ddl_dag_threads_.start())) {
        LOG_WARN("start ddl dag threads failed", K(ret));
      } else {
        ddl_dag_->set_start_time();
      }
    }
    FLOG_INFO("start ddl with dag", K(ret), K(ddl_task_id), K(ddl_execution_id), K(ddl_table_id), KPC(ddl_dag_));
  }
  ddl_rewrite_ret_code(ret);
  return ret;
}

int ObPxSubCoord::end_ddl(const bool need_commit)
{
  int ret = OB_SUCCESS;
  UNUSEDx(need_commit);
  if (OB_NOT_NULL(ddl_dag_)) {
    DEBUG_SYNC(END_DDL_IN_PX_SUBCOORD);
    FLOG_INFO("end ddl with dag", K(ret), KPC(ddl_dag_));
    ddl_dag_->simply_set_stop();
    ddl_dag_threads_.stop();
    ddl_dag_threads_.wait();
    ret = ddl_dag_->get_dag_ret();
    ddl_dag_->~ObColumnClusteredDag();
    if (OB_ISNULL(sqc_arg_.exec_ctx_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sqc exec context is null", K(ret), KPC(ddl_dag_));
    } else {
      sqc_arg_.exec_ctx_->get_allocator().free(ddl_dag_);
      ddl_dag_ = nullptr;
    }
  }
  ddl_rewrite_ret_code(ret);
  return ret;
}

void ObPxSubCoord::ddl_rewrite_ret_code(int &ret_code)
{
  if (OB_EAGAIN == ret_code) {
    ret_code = OB_STATE_NOT_MATCH; // avoid px hang
  }
}

int ObPxSubCoord::get_participants(ObPxSqcMeta &sqc,
                                   const int64_t table_id,
                                   ObIArray<std::pair<ObLSID, ObTabletID>> &ls_tablet_ids) const
{
  int ret = OB_SUCCESS;
  const DASTabletLocIArray &locations = sqc.get_access_table_locations();
  for (int64_t i = 0; OB_SUCC(ret) && i < locations.count(); ++i) {
    ObDASTabletLoc *tablet_loc = ObDASUtils::get_related_tablet_loc(*locations.at(i), table_id);
    if (OB_FAIL(add_var_to_array_no_dup(ls_tablet_ids, std::make_pair(tablet_loc->ls_id_, tablet_loc->tablet_id_)))) {
      LOG_WARN("add var to array no dup failed", K(ret));
    }
  }
  return ret;
}

int ObPxSubCoord::rebuild_sqc_access_table_locations()
{
  int ret = OB_SUCCESS;
  DASTabletLocIArray &access_locations = sqc_arg_.sqc_.get_access_table_locations_for_update();
  ObIArray<ObSqcTableLocationKey> &location_keys = sqc_arg_.sqc_.get_access_table_location_keys();
  ObDASCtx &das_ctx = DAS_CTX(*sqc_arg_.exec_ctx_);
  // FIXME @yishen Performance?
  ObDASTableLoc *table_loc = NULL;
  if (!access_locations.empty()) {
    // do nothing
    // it means that it's rebuilded by pre_setup_op_input
    if (is_single_tsc_leaf_dfo_) {
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected access locations", K(access_locations));
    }
  } else {
    for (int i = 0; i < location_keys.count() && OB_SUCC(ret); ++i) {
      // dml location always at first
      if (OB_ISNULL(table_loc) && location_keys.at(i).is_loc_uncertain_) {
        ObDASLocationRouter &loc_router = DAS_CTX(*sqc_arg_.exec_ctx_).get_location_router();
        OZ(ObTableLocation::get_full_leader_table_loc(loc_router,
           sqc_arg_.exec_ctx_->get_allocator(),
           sqc_arg_.exec_ctx_->get_my_session()->get_effective_tenant_id(),
           location_keys.at(i).table_location_key_,
           location_keys.at(i).ref_table_id_,
           table_loc));
      }
      if (OB_SUCC(ret)) {
        if (OB_ISNULL(table_loc) ||
            location_keys.at(i).table_location_key_ != table_loc->get_table_location_key() ||
            location_keys.at(i).ref_table_id_ != table_loc->get_ref_table_id()) {
          table_loc = das_ctx.get_table_loc_by_id(
              location_keys.at(i).table_location_key_,
              location_keys.at(i).ref_table_id_);
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(table_loc)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected table loc", K(ret));
      } else {
        for (DASTabletLocListIter tmp_node = table_loc->tablet_locs_begin();
             tmp_node != table_loc->tablet_locs_end(); ++tmp_node) {
          ObDASTabletLoc *tablet_loc = *tmp_node;
          if (tablet_loc->tablet_id_ == location_keys.at(i).tablet_id_) {
            if (OB_FAIL(access_locations.push_back(tablet_loc))) {
              LOG_WARN("fail to push back access locations", K(ret));
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && location_keys.count() != access_locations.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid location key count", K(ret),
        K(location_keys.count()),
        K(access_locations.count()));
  }
  return ret;
}

void ObPxSubCoord::try_get_dml_op(ObOpSpec &root, ObTableModifySpec *&dml_op)
{
  if (1 == root.get_child_num()) {
      // Enable PX, GI allocation is above the insert/replace operator, generating the following plan:
      // ....
      //   GI
      //     INSERT/REPLACE
      //      ....
      // In this case, the Table corresponding to the INSERT/REPLACE operator also needs to participate in the GI task division
      // Also exists GI operator below MONITOR operator, currently only these two cases exist.
    if (IS_DML(root.get_child(0)->get_type())) {
      dml_op = static_cast<ObTableModifySpec*>(root.get_child(0));
    } else if (PHY_MONITORING_DUMP == root.get_child(0)->get_type() ||
               PHY_MATERIAL == root.get_child(0)->get_type()) {
      try_get_dml_op(*root.get_child(0), dml_op);
    }
  }
}
//////////// END /////////
