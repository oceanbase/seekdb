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

#include "ob_px_util.h"
#include "sql/dtl/ob_dtl_channel_group.h"
#include "sql/engine/px/ob_px_scheduler.h"
#include "sql/executor/ob_task_spliter.h"
#include "sql/engine/px/ob_px_sqc_handler.h"
#include "share/schema/ob_part_mgr_util.h"
#include "sql/engine/px/ob_dfo_scheduler.h"


using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;
using namespace oceanbase::share;

#define CASE_IGNORE_ERR_HELPER(ERR_CODE)                        \
case ERR_CODE: {                                                \
  should_ignore = true;                                         \
  LOG_USER_WARN(OB_IGNORE_ERR_ACCESS_VIRTUAL_TABLE, ERR_CODE);  \
  break;                                                        \
}                                                               \

OB_SERIALIZE_MEMBER(ObExprExtraSerializeInfo, *current_time_, *last_trace_id_);

ObBaseOrderMap::~ObBaseOrderMap()
{
  int ret = OB_SUCCESS;
  ClearMapFunc clear_func;
  if (OB_FAIL(map_.foreach_refactored(clear_func))) {
    LOG_WARN("failed to clear");
  }
  map_.destroy();
  allocator_.reset();
}

int ObBaseOrderMap::init(int64_t count)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(map_.create(count, ObModIds::OB_SQL_PX))) {
    SQL_LOG(WARN, "Failed to create hash table", K(count));
  }
  return ret;
}

int ObBaseOrderMap::add_base_partition_order(int64_t pwj_group_id,
                                             const TabletIdArray &tablet_id_array,
                                             const DASTabletLocIArray &dst_locations, bool asc)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  ObTMArray<int64_t> *base_order = nullptr;
  if (OB_ISNULL(buf = reinterpret_cast<ObTMArray<int64_t> *>(
                    allocator_.alloc(sizeof(ObTMArray<int64_t>))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory");
  } else if (FALSE_IT(base_order = new(buf) ObTMArray<int64_t>())) {
  } else if (OB_FAIL(base_order->reserve(dst_locations.count()))) {
    LOG_WARN("fail reserve base order", K(ret), K(dst_locations.count()));
  } else if (OB_FAIL(map_.set_refactored(pwj_group_id, std::make_pair(base_order, asc)))) {
    base_order->destroy();
    LOG_WARN("failed to set", K(pwj_group_id));
  } else {
    for (int i = 0; i < dst_locations.count() && OB_SUCC(ret); ++i) {
      for (int j = 0; j < tablet_id_array.count() && OB_SUCC(ret); ++j) {
        if (dst_locations.at(i)->tablet_id_.id() == tablet_id_array.at(j)) {
          if (OB_FAIL(base_order->push_back(j))) {
            LOG_WARN("fail to push idx into base order", K(ret));
          }
          break;
        }
      }
    }
  }
  return ret;
}

int ObBaseOrderMap::reorder_partition_as_base_order(int64_t pwj_group_id,
                                                    const TabletIdArray &tablet_id_array,
                                                    DASTabletLocIArray &dst_locations)
{
  int ret = OB_SUCCESS;
  std::pair<ObIArray<int64_t> *, bool> base_order;
  ObIArray<int64_t> *base_order_arr = nullptr;
  if (OB_FAIL(map_.get_refactored(pwj_group_id, base_order))) {
    LOG_WARN("hash not found", K(pwj_group_id));
  } else if (FALSE_IT(base_order_arr = base_order.first)) {
  } else {
    int index = 0;
    for (int i = 0; i < base_order_arr->count() && OB_SUCC(ret); ++i) {
      for (int j = 0; j < dst_locations.count() && OB_SUCC(ret); ++j) {
        if (dst_locations.at(j)->tablet_id_.id() == tablet_id_array.at(base_order_arr->at(i))) {
          std::swap(dst_locations.at(j), dst_locations.at(index++));
          break;
        }
      }
    }
  }
  return ret;
}
// Physical distribution strategy: For leaf nodes, dfo distribution is generally directly according to data distribution
// Note: If there are two or more scans in dfo, only consider the first one. And, the remaining scans
//       The copy distribution is exactly the same as the first scan, otherwise an error is reported.
int ObPxSqcDistributionUtil::alloc_by_data_distribution(const ObIArray<ObTableLocation> *table_locations,
                                                   ObExecContext &ctx,
                                                   ObDfo &dfo)
{
  int ret = OB_SUCCESS;
  if (nullptr != dfo.get_root_op_spec()) {
    if (OB_FAIL(alloc_by_data_distribution_inner(table_locations, ctx, dfo))) {
      LOG_WARN("failed to alloc data distribution", K(ret));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  }
  return ret;
}


int ObPxSqcDistributionUtil::build_dynamic_partition_table_location(common::ObIArray<const ObTableScanSpec *> &scan_ops,
      const ObIArray<ObTableLocation> *table_locations, ObDfo &dfo)
{
  int ret = OB_SUCCESS;
  uint64_t table_location_key = OB_INVALID_INDEX;
  for (int i = 0; i < scan_ops.count() && OB_SUCC(ret); ++i) {
    if (OB_ISNULL(scan_ops.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("scan ops is null", K(ret));
    } else {
      table_location_key = scan_ops.at(i)->get_table_loc_id();
      for (int j = 0; j < table_locations->count() && OB_SUCC(ret); ++j) {
        if (table_location_key == table_locations->at(j).get_table_id()) {
          ObIArray<ObPxSqcMeta> &sqcs = dfo.get_sqcs();
          for (int k = 0; k < sqcs.count() && OB_SUCC(ret); ++k) {
            if (OB_FAIL(sqcs.at(k).get_pruning_table_locations().push_back(table_locations->at(j)))) {
              LOG_WARN("fail to push back pruning table locations", K(ret));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObPxSqcDistributionUtil::alloc_by_data_distribution_inner(
    const ObIArray<ObTableLocation> *table_locations,
    ObExecContext &ctx,
    ObDfo &dfo)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObTableScanSpec *, 2> scan_ops;
  ObSEArray<const ObTableModifySpec *, 1> dml_ops;
  // INSERT, REPLACE operator
  const ObTableModifySpec *dml_op = nullptr;
  const ObTableScanSpec *scan_op = nullptr;
  const ObOpSpec *root_op = NULL;
  dfo.get_root(root_op);
  // The logic of PDML will be removed
  if (OB_ISNULL(root_op)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr or sqc is not empty", K(ret), K(dfo));
  } else if (0 != dfo.get_sqcs_count()) {
    /**
     * this dfo has been built. do nothing.
     */
    LOG_TRACE("this dfo has been built", K(dfo.get_dfo_id()));
  } else if (OB_FAIL(ObTaskSpliter::find_scan_ops(scan_ops, *root_op))) {
    LOG_WARN("fail find scan ops in dfo", K(dfo), K(ret));
  } else if (OB_FAIL(ObPxSqcDistributionUtil::find_dml_ops(dml_ops, *root_op))) {
    LOG_WARN("failed find insert op in dfo", K(ret), K(dfo));
  } else if (1 < dml_ops.count()) { // Currently, there is at most one DML operator in one DFO
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the count of insert ops is not right", K(ret), K(dml_ops.count()));
  } else if (0 == scan_ops.count() && 0 == dml_ops.count()) {
    /**
     * some dfo may not contain tsc and dml. for example, select 8 from union all select t1.c1 from t1.
     */
    if (OB_FAIL(alloc_by_local_distribution(ctx, dfo))) {
      LOG_WARN("alloc SQC on local failed", K(ret));
    }
  } else {
    ObDASTableLoc *table_loc = NULL;
    ObDASTableLoc *dml_full_loc = NULL;
    uint64_t table_location_key = OB_INVALID_INDEX;
    uint64_t ref_table_id = OB_INVALID_ID;
    if (scan_ops.count() > 0) {
      scan_op = scan_ops.at(0);
      table_location_key = scan_op->get_table_loc_id();
      ref_table_id = scan_op->get_loc_ref_table_id();
    }
    if (OB_SUCC(ret)) {
      if (dml_ops.count() > 0) {
        dml_op = dml_ops.at(0);
        if (OB_FAIL(dml_op->get_single_table_loc_id(table_location_key, ref_table_id))) {
          LOG_WARN("get single table loc id failed", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (dml_op && dml_op->is_table_location_uncertain()) {
      // Need to get FULL TABLE LOCATION. FIX ME YISHEN.
      CK(OB_NOT_NULL(ctx.get_my_session()));
      OZ(ObTableLocation::get_full_local_table_loc(DAS_CTX(ctx).get_location_router(),
                                                    ctx.get_allocator(),
                                                    table_location_key,
                                                    ref_table_id,
                                                    table_loc));
      if (OB_SUCC(ret)) {
        dml_full_loc = table_loc;
      }
    } else {
      // Obtain the location information of the partition corresponding to the current DFO through TSC or DML
      // Subsequently use location information to construct the corresponding SQC meta
      if (OB_ISNULL(table_loc = DAS_CTX(ctx).get_table_loc_by_id(table_location_key, ref_table_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get table loc", K(ret), K(table_location_key), K(ref_table_id), K(DAS_CTX(ctx).get_table_loc_list()));
      }
    }

    if (OB_FAIL(ret)) {
      // bypass
    } else if (OB_ISNULL(table_loc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get phy table location", K(ret));
    } else {
      const DASTabletLocList &locations = table_loc->get_tablet_locs();
      if (locations.size() <= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the location array is empty", K(locations.size()), K(ret));
      } else if (OB_FAIL(build_dfo_sqc(ctx, locations, dfo))) {
        LOG_WARN("fail fill dfo with sqc infos", K(dfo), K(ret));
      } else if (OB_FAIL(set_dfo_accessed_location(ctx, table_location_key, dfo, scan_ops, dml_op, dml_full_loc))) {
        LOG_WARN("fail to set all table partition for tsc", K(ret), K(scan_ops.count()), K(dml_op),
                 K(table_location_key), K(ref_table_id), K(locations));
      } else if (OB_NOT_NULL(table_locations) && !table_locations->empty() &&
            OB_FAIL(build_dynamic_partition_table_location(scan_ops, table_locations, dfo))) {
        LOG_WARN("fail to build dynamic partition pruning table", K(ret));
      }
      LOG_TRACE("allocate sqc by data distribution", K(dfo), K(locations));
    }
  }
  return ret;
}

int ObPxSqcDistributionUtil::find_dml_ops(common::ObIArray<const ObTableModifySpec *> &insert_ops,
                             const ObOpSpec &op)
{
  return find_dml_ops_inner(insert_ops, op);
}

bool ObPxSqcDistributionUtil::check_build_dfo_with_dml(const ObOpSpec &op)
{
  bool b_ret = false;
  if (static_cast<const ObTableModifySpec &>(op).use_dist_das()
      && PHY_INSERT_ON_DUP != op.get_type()) {
    // px no need schedule das except merge
  } else if (PHY_LOCK == op.get_type()) {
    // no need lock op
  } else {
    b_ret = true;
  }
  return b_ret;
}

int ObPxSqcDistributionUtil::find_dml_ops_inner(common::ObIArray<const ObTableModifySpec *> &insert_ops,
                             const ObOpSpec &op)
{
  int ret = OB_SUCCESS;
  if (IS_DML(op.get_type())) {
    if (!check_build_dfo_with_dml(op)) {
    } else if (OB_FAIL(insert_ops.push_back(static_cast<const ObTableModifySpec *>(&op)))) {
      LOG_WARN("fail to push back table insert op", K(ret));
    }
  }
  if (OB_SUCC(ret) && !IS_RECEIVE(op.get_type())) {
    for (int32_t i = 0; OB_SUCC(ret) && i < op.get_child_num(); ++i) {
      const ObOpSpec *child_op = op.get_child(i);
      if (OB_ISNULL(child_op)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(find_dml_ops(insert_ops, *child_op))) {
        LOG_WARN("fail to find child insert ops",
                 K(ret), K(i), "op_id", op.get_id(), "child_id", child_op->get_id());
      }
    }
  }
  return ret;
}


int ObPxSqcDistributionUtil::build_dfo_sqc(ObExecContext &ctx,
                                      const DASTabletLocList &locations,
                                      ObDfo &dfo)
{
  int ret = OB_SUCCESS;
  const ObPhysicalPlanCtx *phy_plan_ctx = GET_PHY_PLAN_CTX(ctx);
  const ObPhysicalPlan *phy_plan = nullptr;
  int64_t parallel = std::max<int64_t>(dfo.get_assigned_worker_count(), 1);
  if (OB_ISNULL(phy_plan_ctx)
      || OB_ISNULL(phy_plan = phy_plan_ctx->get_phy_plan())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL phy plan ptr", K(ret));
  } else if (!locations.empty()) {
    SMART_VAR(ObPxSqcMeta, sqc) {
      sqc.set_dfo_id(dfo.get_dfo_id());
      sqc.set_sqc_id(0);
      sqc.set_execution_id(dfo.get_execution_id());
      sqc.set_px_sequence_id(dfo.get_px_sequence_id());
      sqc.set_qc_id(dfo.get_qc_id());
      sqc.set_interrupt_id(dfo.get_interrupt_id());
      sqc.set_fulltree(dfo.is_fulltree());
      sqc.set_parent_dfo_id(dfo.get_parent_dfo_id());
      sqc.set_single_tsc_leaf_dfo(dfo.is_single_tsc_leaf_dfo());
      sqc.set_min_task_count(1);
      sqc.set_max_task_count(parallel);
      sqc.set_total_task_count(parallel);
      sqc.set_total_part_count(locations.size());
      sqc.get_monitoring_info().init(dfo);
      sqc.set_partition_random_affinitize(dfo.partition_random_affinitize());
      if (OB_FAIL(dfo.add_sqc(sqc))) {
        LOG_WARN("failed to add local sqc", K(ret), K(sqc));
      }
    }
  }
  return ret;
}


int ObPxSqcDistributionUtil::alloc_by_temp_child_distribution(ObExecContext &exec_ctx,
                                                         ObDfo &dfo)
{
  int ret = OB_SUCCESS;
  if (nullptr != dfo.get_root_op_spec()) {
    if (OB_FAIL(alloc_by_temp_child_distribution_inner(exec_ctx, dfo))) {
      LOG_WARN("failed to alloc temp child distribution", K(ret));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  }
  return ret;
}

int ObPxSqcDistributionUtil::alloc_by_temp_child_distribution_inner(ObExecContext &exec_ctx,
                                                               ObDfo &child)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObTableScanSpec *, 2> scan_ops;
  const ObTableScanSpec *scan_op = nullptr;
  const ObOpSpec *root_op = NULL;
  child.get_root(root_op);
  ObIArray<ObSqlTempTableCtx>& temp_ctx = exec_ctx.get_temp_table_ctx();
  ObSqlTempTableCtx *ctx = NULL;
  int64_t parallel = child.get_assigned_worker_count();
  if (0 >= parallel) {
    parallel = 1;
    LOG_TRACE("parallel not set in query hint. set default to 1");
  }
  for (int64_t i = 0; NULL == ctx && i < temp_ctx.count(); i++) {
    if (child.get_temp_table_id() == temp_ctx.at(i).temp_table_id_) {
      ctx = &temp_ctx.at(i);
    }
  }
  if (OB_NOT_NULL(ctx) && !ctx->interm_result_infos_.empty()) {
    ObIArray<ObTempTableResultInfo> &interm_result_infos = ctx->interm_result_infos_;
    int64_t result_count = 0;
    for (int64_t i = 0; i < interm_result_infos.count(); ++i) {
      result_count += interm_result_infos.at(i).interm_result_ids_.count();
    }
    SMART_VAR(ObPxSqcMeta, sqc) {
      sqc.set_dfo_id(child.get_dfo_id());
      sqc.set_sqc_id(0);
      sqc.set_execution_id(child.get_execution_id());
      sqc.set_px_sequence_id(child.get_px_sequence_id());
      sqc.set_qc_id(child.get_qc_id());
      sqc.set_interrupt_id(child.get_interrupt_id());
      sqc.set_fulltree(child.is_fulltree());
      sqc.set_parent_dfo_id(child.get_parent_dfo_id());
      sqc.set_min_task_count(1);
      sqc.set_max_task_count(parallel);
      sqc.set_total_task_count(parallel);
      sqc.set_total_part_count(std::max<int64_t>(result_count, 1));
      sqc.get_monitoring_info().init(child);
      sqc.set_partition_random_affinitize(child.partition_random_affinitize());
      if (OB_FAIL(child.add_sqc(sqc))) {
        LOG_WARN("failed to add local temp-table sqc", K(ret), K(sqc));
      }
    }
  } else if (OB_ISNULL(ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expect temp table in dfo", K(child.get_temp_table_id()), K(temp_ctx));
  }
  if (OB_SUCC(ret)) {
    int64_t base_table_location_key = OB_INVALID_ID;
    if (OB_ISNULL(root_op)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL ptr or sqc is not empty", K(ret), K(child));
    } else if (OB_FAIL(ObTaskSpliter::find_scan_ops(scan_ops, *root_op))) {
      LOG_WARN("fail find scan ops in dfo", K(child), K(ret));
    } else if (scan_ops.empty()) {
    } else if (FALSE_IT(base_table_location_key = scan_ops.at(0)->get_table_loc_id())) {
    } else if (OB_FAIL(set_dfo_accessed_location(exec_ctx,
          base_table_location_key, child, scan_ops, NULL, NULL))) {
      LOG_WARN("fail to set all table partition for tsc", K(ret));
    }
  }
  return ret;
}

int ObPxSqcDistributionUtil::alloc_by_child_distribution(const ObDfo &child, ObDfo &parent)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObPxSqcMeta> &sqcs = child.get_sqcs();
  if (!sqcs.empty()) {
    const ObPxSqcMeta &child_sqc = sqcs.at(0);
    SMART_VAR(ObPxSqcMeta, sqc) {
        sqc.set_dfo_id(parent.get_dfo_id());
        sqc.set_min_task_count(child_sqc.get_min_task_count());
        sqc.set_max_task_count(child_sqc.get_max_task_count());
        sqc.set_total_task_count(child_sqc.get_total_task_count());
        sqc.set_total_part_count(child_sqc.get_total_part_count());
        sqc.set_sqc_id(0);
        sqc.set_execution_id(parent.get_execution_id());
        sqc.set_px_sequence_id(parent.get_px_sequence_id());
        sqc.set_qc_id(parent.get_qc_id());
        sqc.set_interrupt_id(parent.get_interrupt_id());
        sqc.set_fulltree(parent.is_fulltree());
        sqc.set_parent_dfo_id(parent.get_parent_dfo_id());
        sqc.get_monitoring_info().assign(child_sqc.get_monitoring_info());
        sqc.set_partition_random_affinitize(child.partition_random_affinitize());
        if (OB_FAIL(parent.add_sqc(sqc))) {
          LOG_WARN("fail add sqc", K(sqc), K(ret));
        }
    }
    LOG_TRACE("allocate by child distribution", K(sqcs));
  }
  return ret;
}

int ObPxSqcDistributionUtil::alloc_by_local_distribution(ObExecContext &exec_ctx,
                                                    ObDfo &dfo)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(exec_ctx);
  if (OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL phy plan ctx", K(ret));
  } else {
    SMART_VAR(ObPxSqcMeta, sqc) {
      sqc.set_dfo_id(dfo.get_dfo_id());
      sqc.set_min_task_count(1);
      const int64_t parallel = std::max<int64_t>(dfo.get_assigned_worker_count(), 1);
      sqc.set_max_task_count(parallel);
      sqc.set_total_task_count(parallel);
      sqc.set_total_part_count(1);
      sqc.set_sqc_id(0);
      sqc.set_execution_id(dfo.get_execution_id());
      sqc.set_px_sequence_id(dfo.get_px_sequence_id());
      sqc.set_qc_id(dfo.get_qc_id());
      sqc.set_interrupt_id(dfo.get_interrupt_id());
      sqc.set_fulltree(dfo.is_fulltree());
      sqc.set_parent_dfo_id(dfo.get_parent_dfo_id());
      sqc.get_monitoring_info().init(dfo);
      sqc.set_partition_random_affinitize(dfo.partition_random_affinitize());
      OZ(dfo.add_sqc(sqc));
    }
  }
  return ret;
}

/**
 *  hash-local's ppwj slave mapping plan has two types, the first one is:
 *                  |
 *               hash join (dfo3)
 *                  |
 * ----------------------------------
 * |                                |
 * |                                |
 * |                                |
 * TSC(dfo1:partition-hash)         TSC(dfo2:hash-local)
 * When encountering this type of scheduling tree, we schedule dfo1 and dfo3 before dfo2 has been scheduled.
 * In the pkey plan, dfo2 is the reference table, and in the construction of slave mapping, the parent (which is dfo3) construction
 * depends on the reference side, so when allocating the parent, we pre-allocate dfo2 as well, and then the parent is built
 * according to dfo2's sqc.
 *
 */
int ObPxSqcDistributionUtil::alloc_by_reference_child_distribution(
    ObDfo &parent)
{
  int ret = OB_SUCCESS;
  ObDfo *reference_child = nullptr;
  if (OB_FAIL(find_reference_child(parent, reference_child))) {
    LOG_WARN("find reference child failed", K(ret));
  } else if (OB_ISNULL(reference_child)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null child", K(ret));
  } else if (OB_FAIL(alloc_by_child_distribution(*reference_child, parent))) {
    LOG_WARN("failed to alloc by child distribution", K(ret));
  }
  return ret;
}

int ObPxSqcDistributionUtil::alloc_distribution_of_reference_child(
                                          const ObIArray<ObTableLocation> *table_locations,
                                          ObExecContext &exec_ctx,
                                          ObDfo &parent)
{
  int ret = OB_SUCCESS;
  ObDfo *reference_child = nullptr;
  if (OB_FAIL(find_reference_child(parent, reference_child))) {
    LOG_WARN("find reference child failed", K(ret));
  } else if (OB_ISNULL(reference_child)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null child", K(ret));
  } else if (OB_FAIL(alloc_by_data_distribution(table_locations, exec_ctx, *reference_child))) {
    LOG_WARN("failed to alloc by data distribution", K(ret));
  }
  return ret;
}

int ObPxSqcDistributionUtil::find_reference_child(ObDfo &parent, ObDfo *&reference_child)
{
  int ret = OB_SUCCESS;
  reference_child = nullptr;
  for (int64_t i = 0;
       OB_SUCC(ret) && nullptr == reference_child && i < parent.get_child_count();
       ++i) {
    ObDfo *candi_child = nullptr;
    if (OB_FAIL(parent.get_child_dfo(i, candi_child))) {
      LOG_WARN("failed to get dfo", K(ret));
    } else if (OB_ISNULL(candi_child)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null child", K(ret));
    } else if (ObPQDistributeMethod::HASH == candi_child->get_dist_method() &&
               candi_child->is_out_slave_mapping()) {
      reference_child = candi_child;
    }
  }
  return ret;
}


int ObPxSqcDistributionUtil::get_access_partition_order(
  ObDfo &dfo,
  const ObOpSpec *phy_op,
  bool &asc_order)
{
  int ret = OB_SUCCESS;
  const ObOpSpec *root = NULL;
  dfo.get_root(root);
  if (OB_ISNULL(root)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get root phy op", K(ret));
  } else if (OB_FAIL(get_access_partition_order_recursively(root, phy_op, asc_order))) {
    LOG_WARN("fail to get table scan partition", K(ret));
  }
  return ret;
}

int ObPxSqcDistributionUtil::get_access_partition_order_recursively (
  const ObOpSpec *root,
  const ObOpSpec *phy_op,
  bool &asc_order)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(root) || OB_ISNULL(phy_op)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the root or phy op is null", K(ret), K(root), K(phy_op));
  } else if (root == phy_op) { // No GI case, default ASC access
    asc_order = true;
    LOG_DEBUG("No GI in this dfo");
  } else if (PHY_GRANULE_ITERATOR == phy_op->get_type()) {
    const ObGranuleIteratorSpec *gi = static_cast<const ObGranuleIteratorSpec*>(phy_op);
    asc_order = !ObGranuleUtil::desc_order(gi->gi_attri_flag_);
  } else if (OB_FAIL(get_access_partition_order_recursively(root, phy_op->get_parent(), asc_order))) {
    LOG_WARN("fail to access partition order", K(ret));
  }
  return ret;
}

int ObPxSqcDistributionUtil::set_dfo_accessed_location(ObExecContext &ctx,
                                                  int64_t base_table_location_key,
                                                  ObDfo &dfo,
                                                  ObIArray<const ObTableScanSpec *> &scan_ops,
                                                  const ObTableModifySpec *dml_op,
                                                  ObDASTableLoc *dml_loc)
{
  int ret = OB_SUCCESS;
  ObDASTableLoc *dml_table_loc = nullptr;
  ObTableID dml_table_location_key = OB_INVALID_ID;
  ObTableID dml_ref_table_id = OB_INVALID_ID;
  ObBaseOrderMap base_order_map;
  ObSEArray<std::pair<int64_t, bool>, 18> locations_order;
  if (OB_FAIL(base_order_map.init(max(1, scan_ops.count())))) {
    LOG_WARN("Failed to init base_order_map");
  }
  // process insert op corresponding partition location information
  if (OB_FAIL(ret) || OB_ISNULL(dml_op)) {
    // pass
  } else {
    ObDASTableLoc *table_loc = nullptr;
    ObTableID table_location_key = OB_INVALID_ID;
    ObTableID ref_table_id = OB_INVALID_ID;
    if (OB_FAIL(dml_op->get_single_table_loc_id(table_location_key, ref_table_id))) {
      LOG_WARN("get single table location id failed", K(ret));
    } else {
      if (dml_op->is_table_location_uncertain()) {
        if (OB_ISNULL(dml_loc)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected dml loc", K(ret));
        } else {
          table_loc = dml_loc;
        }
      } else {
        // Obtain the location information of the partition corresponding to the current DFO through TSC or DML
        // Subsequently use location information to construct the corresponding SQC meta
        if (OB_ISNULL(table_loc = DAS_CTX(ctx).get_table_loc_by_id(table_location_key, ref_table_id))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail to get table loc id", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
      // bypass
    } else if (OB_ISNULL(table_loc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table loc is null", K(ret));
    } else if (OB_FAIL(set_sqcs_accessed_location(ctx, base_table_location_key,
        dfo, base_order_map, table_loc, dml_op, locations_order))) {
      LOG_WARN("failed to set sqc accessed location", K(ret));
    }
    dml_table_loc = table_loc;
    dml_table_location_key = table_location_key;
    dml_ref_table_id = ref_table_id;
  }
  // Process tsc corresponding partition location information
  for (int64_t i = 0; OB_SUCC(ret) && i < scan_ops.count(); ++i) {
    ObDASTableLoc *table_loc = nullptr;
    const ObTableScanSpec *scan_op = nullptr;
    uint64_t table_location_key = common::OB_INVALID_ID;
    uint64_t ref_table_id = common::OB_INVALID_ID;
    // Physical table or virtual table?
    if (OB_ISNULL(scan_op = scan_ops.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("scan op can't be null", K(ret));
    } else if (FALSE_IT(table_location_key = scan_op->get_table_loc_id())) {
    } else if (FALSE_IT(ref_table_id = scan_op->get_loc_ref_table_id())) {
    } else if (OB_ISNULL(table_loc = DAS_CTX(ctx).get_table_loc_by_id(table_location_key, ref_table_id))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get phy table location", K(ret));
    } else if (OB_FAIL(set_sqcs_accessed_location(ctx,
          // dml op has already set sqc.get_location information,
          // table scan does not need to be set again
          OB_ISNULL(dml_op) ? base_table_location_key : OB_INVALID_ID,
          dfo, base_order_map, table_loc, scan_op, locations_order))) {
      LOG_WARN("failed to set sqc accessed location", K(ret), K(table_location_key),
               K(ref_table_id), KPC(table_loc));
    }
  } // end for
  if (OB_FAIL(ret)) {
    if (NULL == dml_op) {
      LOG_WARN("set dfo accessed location failed, dml op is null", K(base_table_location_key), K(dfo));
    } else {
      LOG_WARN("set dfo accessed location failed, dml op is not null", K(base_table_location_key),
               K(dml_op), K(dml_op->is_table_location_uncertain()),  K(dml_table_location_key),
               K(dml_ref_table_id), KPC(dml_table_loc));
    }
  } else {
    ARRAY_FOREACH_X(dfo.get_sqcs(), sqc_idx, sqc_cnt, OB_SUCC(ret)) {
      ObPxSqcMeta &sqc_meta = dfo.get_sqcs().at(sqc_idx);
      if (OB_FAIL(sqc_meta.get_locations_order().assign(locations_order))) {
        LOG_WARN("assign failed", K(ret));
      }
    }
  }
  return ret;
}

int ObPxSqcDistributionUtil::set_sqcs_accessed_location(
    ObExecContext &ctx, int64_t base_table_location_key, ObDfo &dfo,
    ObBaseOrderMap &base_order_map,
    const ObDASTableLoc *table_loc, const ObOpSpec *phy_op,
    ObIArray<std::pair<int64_t, bool>> &locations_order)
{
  int ret = OB_SUCCESS;
  ObIArray<ObPxSqcMeta> &sqcs = dfo.get_sqcs();
  int n_locations = 0;
  const DASTabletLocList &locations = table_loc->get_tablet_locs();
  DASTabletLocSEArray temp_locations;
  if (OB_ISNULL(table_loc) || OB_ISNULL(phy_op)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null table_loc or phy_op", K(phy_op), K(table_loc));
  } else {
    int64_t table_location_key = table_loc->get_table_location_key();
    bool asc_order = true;
    if (locations.size() <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the locations can not be zero", K(ret), K(locations.size()));
    } else if (OB_FAIL(get_access_partition_order(dfo, phy_op, asc_order))) {
      LOG_WARN("fail to get table scan partition order", K(ret));
    } else if (OB_FAIL(ObPxSqcDistributionUtil::reorder_all_partitions(table_location_key,
        table_loc->get_ref_table_id(), locations,
        temp_locations, asc_order, ctx, base_order_map, phy_op->get_id(), locations_order))) {
      // Sort the partitions involved in the current SQC according to the access order required by GI
      // If it is a partition wise join scenario, need to sort in asc/desc order according to partition_wise_join requirements combined with GI requirements
      LOG_WARN("fail to reorder all partitions", K(ret));
    } else {
      LOG_TRACE("sqc partition order is", K(asc_order), K(locations), K(temp_locations), KPC(table_loc->loc_meta_));
    }
  }
  if (OB_SUCC(ret) && 1 != sqcs.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expected exactly one local sqc", K(ret), K(sqcs.count()));
  } else if (OB_SUCC(ret)) {
    ObPxSqcMeta &sqc_meta = sqcs.at(0);
    DASTabletLocIArray &sqc_locations = sqc_meta.get_access_table_locations_for_update();
    ObIArray<ObSqcTableLocationKey> &sqc_location_keys = sqc_meta.get_access_table_location_keys();
    ObIArray<ObSqcTableLocationIndex> &sqc_location_indexes = sqc_meta.get_access_table_location_indexes();
    int64_t location_start_pos = sqc_locations.count();
    int64_t location_end_pos = sqc_locations.count();
    ARRAY_FOREACH_X(temp_locations, idx, cnt, OB_SUCC(ret)) {
      if (OB_FAIL(sqc_locations.push_back(temp_locations.at(idx)))) {
        LOG_WARN("sqc push back table location failed", K(ret));
      } else if (OB_FAIL(sqc_location_keys.push_back(ObSqcTableLocationKey(
            table_loc->get_table_location_key(),
            table_loc->get_ref_table_id(),
            temp_locations.at(idx)->tablet_id_,
            IS_DML(phy_op->get_type()),
            IS_DML(phy_op->get_type()) ?
              static_cast<const ObTableModifySpec *>(phy_op)->is_table_location_uncertain() :
              false)))) {
      } else {
        ++n_locations;
        ++location_end_pos;
      }
    }
    if (OB_SUCC(ret) && location_start_pos < location_end_pos) {
      if (OB_FAIL(sqc_location_indexes.push_back(ObSqcTableLocationIndex(
          table_loc->get_table_location_key(),
          location_start_pos,
          location_end_pos - 1)))) {
        LOG_WARN("fail to push back table location index", K(ret));
      }
    }
  }
  if (OB_SUCC(ret) && n_locations != locations.size()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("some tablet locations were not assigned to the local sqc", K(ret), K(n_locations),
             K(locations.size()), K(sqcs), K(locations));
  }
  return ret;
}


// used to fast lookup from phy partition id to partition order(index)
// for a range partition, the greater the range, the greater the partition_index
// for a hash partition, the index means nothing
int ObPxSqcDistributionUtil::build_tablet_idx_map(ObTaskExecutorCtx &task_exec_ctx,
                                              uint64_t ref_table_id,
                                              ObTabletIdxMap &idx_map)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = NULL;
  if (OB_ISNULL(task_exec_ctx.schema_service_)) {
  } else if (OB_FAIL(task_exec_ctx.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("fail get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( ref_table_id, table_schema))) {
    LOG_WARN("fail get table schema", K(ref_table_id), K(ret));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("fail get schema", K(ref_table_id), K(ret));
  } else if (OB_FAIL(build_tablet_idx_map(table_schema, idx_map))) {
    LOG_WARN("fail create index map", K(ret), "cnt", table_schema->get_all_part_num());
  }
  return ret;
}


class ObPXTabletOrderIndexCmp
{
public:
  ObPXTabletOrderIndexCmp(bool asc, ObTabletIdxMap *map)
      : asc_(asc), map_(map)
  {}
  bool operator() (const ObDASTabletLoc *left, const ObDASTabletLoc *right)
  {
    int ret = OB_SUCCESS;
    bool bret = false;
    int64_t lv, rv;
    if (OB_FAIL(map_->get_refactored(left->tablet_id_.id(), lv))) {
      LOG_WARN("fail get partition index", K(asc_), K(left), K(right), K(ret));
      throw OB_EXCEPTION<OB_HASH_NOT_EXIST>();
    } else if (OB_FAIL(map_->get_refactored(right->tablet_id_.id(), rv))) {
      LOG_WARN("fail get partition index", K(asc_), K(left), K(right), K(ret));
      throw OB_EXCEPTION<OB_HASH_NOT_EXIST>();
    } else {
      bret = asc_ ? (lv < rv) : (lv > rv);
    }
    return bret;
  }
private:
  bool asc_;
  ObTabletIdxMap *map_;
};

int ObPxSqcDistributionUtil::reorder_all_partitions(
    int64_t table_location_key, int64_t ref_table_id, const DASTabletLocList &src_locations,
    DASTabletLocIArray &dst_locations, bool asc, ObExecContext &exec_ctx,
    ObBaseOrderMap &base_order_map, int64_t op_id,
    ObIArray<std::pair<int64_t, bool>> &locations_order)
{
  int ret = OB_SUCCESS;
  dst_locations.reset();
  if (src_locations.size() > 1) {
    ObTabletIdxMap tablet_order_map;
    if (OB_FAIL(dst_locations.reserve(src_locations.size()))) {
      LOG_WARN("fail reserve locations", K(ret), K(src_locations.size()));
    // virtual table is list partition now,
    // no actual partition define, can't traverse
    // table schema for partition info
    } else if (!is_virtual_table(ref_table_id) &&
        OB_FAIL(build_tablet_idx_map(exec_ctx.get_task_exec_ctx(),
                                     ref_table_id, tablet_order_map))) {
      LOG_WARN("fail build index lookup map", K(ret));
    }
    for (auto iter = src_locations.begin(); iter != src_locations.end() && OB_SUCC(ret); ++iter) {
        if (OB_FAIL(dst_locations.push_back(*iter))) {
        LOG_WARN("fail to push dst locations", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      try {
        if (!is_virtual_table(ref_table_id)) {
          lib::ob_sort(&dst_locations.at(0),
                    &dst_locations.at(0) + dst_locations.count(),
                    ObPXTabletOrderIndexCmp(asc, &tablet_order_map));
        }
      } catch (OB_BASE_EXCEPTION &except) {
        if (OB_HASH_NOT_EXIST == (ret = except.get_errno())) {
          // schema changed during execution, notify to retry
          ret = OB_SCHEMA_ERROR;
        }
      }
      GroupPWJTabletIdMap *group_pwj_map = nullptr;
      if (OB_FAIL(ret)) {
        LOG_WARN("fail to sort  locations", K(ret));
      } else if (OB_NOT_NULL(group_pwj_map = exec_ctx.get_group_pwj_map())) {
        GroupPWJTabletIdInfo group_pwj_tablet_id_info;
        TabletIdArray &tablet_id_array = group_pwj_tablet_id_info.tablet_id_array_;
        if (OB_FAIL(group_pwj_map->get_refactored(table_location_key, group_pwj_tablet_id_info))) {
          if (OB_HASH_NOT_EXIST == ret) {
            // means this is not a partition wise join table, do not need to reorder partition
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("failed to get_refactored", K(table_location_key));
          }
        } else {
          // set base order or reorder partition as base order
          uint64_t pwj_group_id = group_pwj_tablet_id_info.group_id_;
          std::pair<ObIArray<int64_t> *, bool> base_order;
          if (OB_FAIL(base_order_map.get_map().get_refactored(pwj_group_id, base_order))) {
            if (ret == OB_HASH_NOT_EXIST) {
              ret = base_order_map.add_base_partition_order(pwj_group_id, tablet_id_array,
                                                            dst_locations, asc);
              if (ret != OB_SUCCESS) {
                LOG_WARN("failed to add_base_partition_order");
              } else {
                LOG_TRACE("succ to add_base_partition_order", K(pwj_group_id), K(table_location_key));
              }
            } else {
              LOG_WARN("failed to get_refactored");
            }
          } else if (OB_FAIL(base_order_map.reorder_partition_as_base_order(
                  pwj_group_id, tablet_id_array, dst_locations))) {
            LOG_WARN("failed to reorder_partition_as_base_order");
          } else {
            asc = base_order.second;
            LOG_TRACE("succ to reorder_partition_as_base_order", K(pwj_group_id), K(table_location_key));
          }
        }
      }
    }
  } else if (1 == src_locations.size() &&
             OB_FAIL(dst_locations.push_back(*src_locations.begin()))) {
    LOG_WARN("fail to push dst locations", K(ret));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(locations_order.push_back(std::make_pair(op_id, asc)))) {
      LOG_WARN("push back failed", K(ret));
    }
  }
  return ret;
}

/**
 * Algorithm documentation:
 * General idea:
 * n is the total number of threads, p is the total number of partitions involved, ni is the number of threads allocated to the i-th sqc, pi is the number of partitions for the i-th sqc.
 * a. An adjust function, recursively adjusts the number of threads for sqc. Calculate ni = n*pi/p to ensure each is greater than or equal to 1.
 * b. Calculate the execution time of sqc and sort them accordingly.
 * c. The remaining threads are added to the sqc from longest to shortest execution time.
 */
int ObPxSqcDistributionUtil::split_parallel_into_task(const int64_t parallel,
                                                 const common::ObIArray<int64_t> &sqc_part_count,
                                                 common::ObIArray<int64_t> &results) {
  int ret = OB_SUCCESS;
  common::ObArray<ObPxSqcTaskCountMeta> sqc_task_metas;
  int64_t total_part_count = 0;
  int64_t total_thread_count = 0;
  int64_t thread_remain = 0;
  results.reset();
  if (parallel <= 0 || sqc_part_count.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid input argument", K(ret), K(parallel), K(sqc_part_count.count()));
  } else if (OB_FAIL(results.prepare_allocate(sqc_part_count.count()))) {
    LOG_WARN("Failed to prepare allocate array", K(ret));
  }
  // prepare
  ARRAY_FOREACH(sqc_part_count, idx) {
    ObPxSqcTaskCountMeta meta;
    meta.idx_ = idx;
    meta.partition_count_ = sqc_part_count.at(idx);
    meta.thread_count_ = 0;
    meta.time_ = 0;
    if (OB_FAIL(sqc_task_metas.push_back(meta))) {
      LOG_WARN("Failed to push back sqc partition count", K(ret));
    } else if (sqc_part_count.at(idx) < 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Invalid partition count", K(ret));
    } else {
      total_part_count += sqc_part_count.at(idx);
    }
  }
  if (OB_SUCC(ret)) {
    // Why adjustment is needed, because in extreme cases some sqc might only get less than one thread; the algorithm must ensure that each sqc has at least
    // There is a thread.
    if (OB_FAIL(adjust_sqc_task_count(sqc_task_metas, parallel, total_part_count))) {
      LOG_WARN("Failed to adjust sqc task count", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    // Calculate the execution time for each sqc
    for (int64_t i = 0; i < sqc_task_metas.count(); ++i) {
      ObPxSqcTaskCountMeta &meta = sqc_task_metas.at(i);
      total_thread_count += meta.thread_count_;
      meta.time_ = static_cast<double>(meta.partition_count_) / static_cast<double>(meta.thread_count_);
    }
    // Sort, longer execution time tasks are placed first
    auto compare_fun_long_time_first = [](ObPxSqcTaskCountMeta a, ObPxSqcTaskCountMeta b) -> bool { return a.time_ > b.time_; };
    lib::ob_sort(sqc_task_metas.begin(),
              sqc_task_metas.end(),
              compare_fun_long_time_first);
    /// Assign the remaining threads
    thread_remain = parallel - total_thread_count;
    if (thread_remain <= 0) {
      // This situation is normal, it will occur when parallel < sqc count.
    } else if (thread_remain > sqc_task_metas.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Thread remain is invalid", K(ret), K(thread_remain), K(sqc_task_metas.count()));
    } else {
      for (int64_t i = 0; i < thread_remain; ++i) {
        ObPxSqcTaskCountMeta &meta = sqc_task_metas.at(i);
        meta.thread_count_ += 1;
        total_thread_count += 1;
        meta.time_ = static_cast<double>(meta.partition_count_) / static_cast<double>(meta.thread_count_);
      }
    }
  }
  // Record the result
  if (OB_SUCC(ret)) {
    int64_t idx = 0;
    for (int64_t i = 0; i < sqc_task_metas.count(); ++i) {
      ObPxSqcTaskCountMeta meta = sqc_task_metas.at(i);
      idx = meta.idx_;
      results.at(idx) = meta.thread_count_;
    }
  }
  // Check, the specified parallelism is greater than the number of machines, theoretically no more than parallel threads are allocated.
  if (OB_SUCC(ret) && parallel > sqc_task_metas.count() && total_thread_count > parallel) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Failed to allocate expected parallel", K(ret));
  }
  LOG_TRACE("Sqc max task count", K(ret), K(results), K(sqc_task_metas));
  return ret;
}

int ObPxSqcDistributionUtil::adjust_sqc_task_count(common::ObIArray<ObPxSqcTaskCountMeta> &sqc_tasks,
                                              int64_t parallel,
                                              int64_t partition)
{
  int ret = OB_SUCCESS;
  int64_t thread_used = 0;
  int64_t partition_remain = partition;
  // There exists a case where the total number of partitions is 0, for example, in gi task partitioning, where none of the partitions have macroblocks.
  int64_t real_partition = NON_ZERO_VALUE(partition);
  ARRAY_FOREACH(sqc_tasks, idx) {
    ObPxSqcTaskCountMeta &meta = sqc_tasks.at(idx);
    if (!meta.finish_) {
      meta.thread_count_ = meta.partition_count_ * parallel / real_partition;
      if (0 >= meta.thread_count_) {
        // Occur a fractional number of threads or a negative number of threads, adjust this thread to 1, mark it as finish, no further adjustments will be made to it.
        thread_used++;
        partition_remain -= meta.partition_count_;
        meta.finish_ = true;
        meta.thread_count_ = 1;
      }
    }
  }
  if (thread_used != 0) {
    if (OB_FAIL(adjust_sqc_task_count(sqc_tasks, parallel - thread_used, partition_remain))) {
      LOG_WARN("Failed to adjust sqc task count", K(ret), K(sqc_tasks));
    }
  }
  return ret;
}

int ObPxOperatorVisitor::visit(ObExecContext &ctx, const ObOpSpec &root, ApplyFunc &func)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(func.apply(ctx, root))) {
    LOG_WARN("fail apply func to input", "op_id", root.id_, K(ret));
  } else if (!IS_PX_RECEIVE(root.type_)) {
    for (int32_t i = 0; OB_SUCC(ret) && i < root.get_child_cnt(); ++i) {
      const ObOpSpec *child_op = root.get_child(i);
      if (OB_ISNULL(child_op)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null child operator", K(i), K(root.type_));
      } else if (OB_FAIL(visit(ctx, *child_op, func))) {
        LOG_WARN("fail to apply func", K(ret));
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(func.reset(root))) {
    LOG_WARN("Failed to reset", K(ret));
  }
  return ret;
}

int ObPxSqcDistributionUtil::build_tablet_idx_map(
      const share::schema::ObTableSchema *table_schema,
      ObTabletIdxMap &idx_map)
{
  int ret = OB_SUCCESS;
  int64_t tablet_idx = 0;
  if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table schema is null", K(ret));
  } else if (OB_FAIL(idx_map.create(table_schema->get_all_part_num(), "TabletOrderIdx"))) {
    LOG_WARN("fail create index map", K(ret), "cnt", table_schema->get_all_part_num());
  } else if (is_virtual_table(table_schema->get_table_id())) {
    // Distributed virtual tables expose a single schema partition while workers may use
    // distinct logical partition ids. Map each logical id directly to its array position.
    for (int i = 0; OB_SUCC(ret) && i < table_schema->get_all_part_num(); ++i) {
      if (OB_FAIL(idx_map.set_refactored(i + 1, tablet_idx++))) {
        LOG_WARN("fail set value to hashmap", K(ret));
      }
    }
  } else {
    ObPartitionSchemaIter iter(*table_schema, CHECK_PARTITION_MODE_NORMAL);
    ObPartitionSchemaIter::Info info;
    do {
      if (OB_FAIL(iter.next_partition_info(info))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("fail get next partition item from iterator", K(ret));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_FAIL(idx_map.set_refactored(info.tablet_id_.id(), tablet_idx++))) {
        LOG_WARN("fail set value to hashmap", K(ret));
      }
      LOG_DEBUG("table item info", K(info));
    } while (OB_SUCC(ret));
  }
  return ret;
}

int ObPxPartitionLocationUtil::get_all_tables_tablets(
    const common::ObIArray<const ObTableScanSpec*> &scan_ops,
    const DASTabletLocIArray &all_locations,
    const common::ObIArray<ObSqcTableLocationKey> &table_location_keys,
    ObSqcTableLocationKey dml_location_key,
    common::ObIArray<DASTabletLocArray> &all_tablets)
{
  int ret = common::OB_SUCCESS;
  DASTabletLocArray tablets;
  int64_t idx = 0;
  CK(all_locations.count() == table_location_keys.count());
  if (OB_SUCC(ret) && dml_location_key.table_location_key_ != OB_INVALID_ID) {
    for ( ; idx < all_locations.count() && OB_SUCC(ret); ++idx) {
      if (dml_location_key.table_location_key_ == table_location_keys.at(idx).table_location_key_
          && table_location_keys.at(idx).is_dml_) {
        if (OB_FAIL(tablets.push_back(all_locations.at(idx)))) {
          LOG_WARN("fail to push back pkey", K(ret));
        }
      }
    }
    if (OB_SUCC(ret) && !tablets.empty()) {
      if (OB_FAIL(all_tablets.push_back(tablets))) {
        LOG_WARN("push back tsc partitions failed", K(ret));
      }
    }
  }
  for (int i = 0; i < scan_ops.count() && OB_SUCC(ret); ++i) {
    tablets.reset();
    for (int j = 0; j < all_locations.count() && OB_SUCC(ret); ++j) {
      if (scan_ops.at(i)->get_table_loc_id() == table_location_keys.at(j).table_location_key_
          && !table_location_keys.at(j).is_dml_) {
        if (OB_FAIL(tablets.push_back(all_locations.at(j)))) {
          LOG_WARN("fail to push back pkey", K(ret));
        }
      }
    }
    if (OB_SUCC(ret) && !tablets.empty()) {
      if (OB_FAIL(all_tablets.push_back(tablets))) {
        LOG_WARN("push back tsc partitions failed", K(ret));
      }
    }
  }
  LOG_TRACE("add partition in table by tscs", K(ret), K(all_locations), K(all_tablets));
  return ret;
}

/* Previously, QC set sqc.is_fulltree_ and serialize it.
 * QC find all dfos which contain QC and mark sqcs in this and all descedant dfos as fulltree.
 * The problem is that all child dfos of the dfo containing QC will be marked as full tree, including
 * dfos that are not below this nested QC.
 * Now we ignore sqc.is_fulltree_ and set is_fulltree = true when meet a QC.
*/
int ObPxTreeSerializer::serialize_tree(char *buf,
                                       int64_t buf_len,
                                       int64_t &pos,
                                       const ObOpSpec &root,
                                       bool is_fulltree,
                                       ObPhyOpSeriCtx *seri_ctx)
{
  int ret = OB_SUCCESS;
  is_fulltree = is_fulltree || IS_PX_COORD(root.type_);
  int32_t child_cnt = (!is_fulltree && IS_RECEIVE(root.type_)) ? 0 : root.get_child_cnt();
  if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, root.type_))) {
    LOG_WARN("fail to encode op type", K(ret));
  } else if (OB_FAIL(serialization::encode(buf, buf_len, pos, child_cnt))) {
    LOG_WARN("fail to encode op type", K(ret));
  } else if (OB_FAIL((seri_ctx == NULL ? root.serialize(buf, buf_len, pos) :
      root.serialize(buf, buf_len, pos, *seri_ctx)))) {
    ObCStringHelper helper;
    LOG_WARN("fail to serialize root", K(ret), "type", root.type_, "root", helper.convert(root));
  } else if ((PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == root.type_)
             && OB_FAIL(serialize_sub_plan(buf, buf_len, pos, root))) {
    LOG_WARN("fail to serialize sub plan", K(ret));
  }
  // Terminate serialization when meet ObReceive, as this op indicates
  for (int32_t i = 0; OB_SUCC(ret) && i < child_cnt; ++i) {
    const ObOpSpec *child_op = root.get_child(i);
    if (OB_ISNULL(child_op)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null child operator", K(i), K(root.type_));
    } else if (OB_FAIL(serialize_tree(buf, buf_len, pos, *child_op, is_fulltree, seri_ctx))) {
      LOG_WARN("fail to serialize tree", K(ret));
    }
  }
  LOG_DEBUG("end trace serialize tree", K(pos), K(buf_len));
  return ret;
}

int ObPxTreeSerializer::deserialize_tree(const char *buf,
                                         int64_t data_len,
                                         int64_t &pos,
                                         ObPhysicalPlan &phy_plan,
                                         ObOpSpec *&root,
                                         ObIArray<const ObTableScanSpec *> &tsc_ops)
{
  int ret = OB_SUCCESS;
  int32_t phy_operator_type = 0;
  uint32_t child_cnt = 0;
  ObOpSpec *op = NULL;
  if (OB_FAIL(serialization::decode_vi32(buf, data_len, pos, &phy_operator_type))) {
    LOG_WARN("fail to decode phy operator type", K(ret));
  } else if (OB_FAIL(serialization::decode(buf, data_len, pos, child_cnt))) {
    LOG_WARN("fail to encode op type", K(ret));
  } else {
    LOG_DEBUG("deserialize phy_operator", K(phy_operator_type),
              "type_str", ob_phy_operator_type_str(static_cast<ObPhyOperatorType>(phy_operator_type)), K(pos));
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(phy_plan.alloc_op_spec(
        static_cast<ObPhyOperatorType>(phy_operator_type), child_cnt, op, 0))) {
      LOG_WARN("alloc physical operator failed", K(ret));
    } else {
      if (OB_FAIL(op->deserialize(buf, data_len, pos))) {
        LOG_WARN("fail to deserialize operator", K(ret), N_TYPE, phy_operator_type, K(op->id_));
      } else if ((PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == op->type_)
                 && OB_FAIL(deserialize_sub_plan(buf, data_len, pos, phy_plan, op))) {
        LOG_WARN("fail to deserialize sub plan", K(ret));
      } else {
        LOG_DEBUG("deserialize phy operator",
                  K(op->type_), "serialize_size",
                  op->get_serialize_size(), "data_len",
                  data_len, "pos",
                  pos, "takes", data_len-pos);
      }
    }
  }

  if (OB_SUCC(ret)) {
    if ((op->type_ <= PHY_INVALID || op->type_ >= PHY_END)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid operator type", N_TYPE, op->type_);
    } else if (PHY_TABLE_SCAN == op->type_) {
      ObTableScanSpec *tsc_spec = static_cast<ObTableScanSpec *>(op);
      if (OB_FAIL(tsc_ops.push_back(tsc_spec))) {
        LOG_WARN("Failed to push back table scan operator", K(ret));
      }
    }
  }

  // Terminate serialization when meet ObReceive, as this op indicates
  if (OB_SUCC(ret)) {
    for (int32_t i = 0; OB_SUCC(ret) && i < child_cnt; i++) {
      ObOpSpec *child = NULL;
      if (OB_FAIL(deserialize_tree(buf, data_len, pos, phy_plan, child, tsc_ops))) {
        LOG_WARN("fail to deserialize tree", K(ret));
      } else if (OB_FAIL(op->set_child(i, child))) {
        LOG_WARN("fail to set child", K(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    root = op;
  }
  return ret;
}
// Used for full-text index back-table lookup and global index back-table lookup, scenario is op contains sub-plans.
// Serialization/deserialization should be done together during serialization/deserialization
int ObPxTreeSerializer::deserialize_sub_plan(const char *buf,
                                             int64_t data_len,
                                             int64_t &pos,
                                             ObPhysicalPlan &phy_plan,
                                             ObOpSpec *&op)
{
  UNUSED(buf);
  UNUSED(data_len);
  UNUSED(pos);
  UNUSED(op);
  UNUSED(phy_plan);
  int ret = OB_SUCCESS;
  if (PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == op->get_type()) {
    // FIXME: TODO: Improve TableScanWithIndexBack op
    // ObOpSpec *index_scan_tree = nullptr;
    // ObTableScanWithIndexBack *table_scan = static_cast<ObTableScanWithIndexBack*>(op);
    // if (OB_FAIL(deserialize_op(buf, data_len, pos, phy_plan, index_scan_tree))) {
    //   LOG_WARN("deserialize tree failed", K(ret), K(data_len), K(pos));
    // } else {
    //   table_scan->set_index_scan_tree(index_scan_tree);
    // }
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "deserialize TableScanWithIdexBack");
  }
  return ret;
}



int ObPxTreeSerializer::deserialize_tree(const char *buf,
                                         int64_t data_len,
                                         int64_t &pos,
                                         ObPhysicalPlan &phy_plan,
                                         ObOpSpec *&root)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObTableScanSpec*, 8> tsc_ops;
  ret = deserialize_tree(buf, data_len, pos, phy_plan, root, tsc_ops);
  return ret;
}
// Used for full-text index back-table lookup and global index back-table lookup, scenario is op contains sub-plans.
// Serialization/deserialization should be done together during serialization/deserialization
int64_t ObPxTreeSerializer::get_tree_serialize_size(const ObOpSpec &root, bool is_fulltree,
    ObPhyOpSeriCtx *seri_ctx)
{
  int ret = OB_SUCCESS;
  int64_t len = 0;
  is_fulltree = is_fulltree || IS_PX_COORD(root.type_);
  int32_t child_cnt = (!is_fulltree && IS_RECEIVE(root.type_)) ? 0 : root.get_child_cnt();
  len += serialization::encoded_length_vi32(root.get_type());
  len += serialization::encoded_length(child_cnt);
  len += (NULL == seri_ctx ? root.get_serialize_size() :
         root.get_serialize_size(*seri_ctx));
  len += get_sub_plan_serialize_size(root);

  // Terminate serialization when meet ObReceive, as this op indicates
  for (int32_t i = 0; OB_SUCC(ret) && i < child_cnt; ++i) {
    const ObOpSpec *child_op = root.get_child(i);
    if (OB_ISNULL(child_op)) {
      // ignore ret
      // Here no error can be thrown, however, there will be another check for null child during the serialize stage.
      // So it is safe
      LOG_ERROR("null child op", K(i), K(root.get_child_cnt()), K(root.get_type()));
    } else {
      len += get_tree_serialize_size(*child_op, is_fulltree, seri_ctx);
    }
  }
  return len;
}
// Used for full-text index back-table lookup and global index back-table lookup, scenario is op contains sub-plans.
// Serialization/deserialization should be done together during serialization/deserialization
int ObPxTreeSerializer::serialize_sub_plan(char *buf,
                                           int64_t buf_len,
                                           int64_t &pos,
                                           const ObOpSpec &root)
{
  int ret = OB_SUCCESS;
  UNUSED(buf);
  UNUSED(buf_len);
  UNUSED(pos);
  if (PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == root.get_type()) {
    // FIXME: TODO:
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "deserialize TableScanWithIdexBack");

  }
  return ret;
}

int64_t ObPxTreeSerializer::get_sub_plan_serialize_size(const ObOpSpec &root)
{
  int64_t len = 0;
  // some op contain inner mini plan
  if (PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == root.get_type()) {
  }
  return len;
}
// Here we explain that each ObOpSpen serialization of the task is the same, so recursion is not used here
// but adopts a flattened approach, which is essentially fine, if there is special logic that needs to be changed here, such as setting certain variables etc
int ObPxTreeSerializer::deserialize_op_input(
  const char *buf,
  int64_t data_len,
  int64_t &pos,
  ObOpKitStore &op_kit_store)
{
  int ret = OB_SUCCESS;
  int32_t real_input_count = 0;
  if (OB_FAIL(serialization::decode_i32(buf, data_len, pos, &real_input_count))) {
    LOG_WARN("decode int32_t", K(ret), K(data_len), K(pos));
  }
  ObOperatorKit *kit = nullptr;
  ObPhyOperatorType phy_op_type;
  int64_t tmp_phy_op_type = 0;
  int64_t index = 0;
  int64_t ser_input_cnt = 0;
  for (int32_t i = 0; OB_SUCC(ret) && i < real_input_count; ++i) {
    OB_UNIS_DECODE(index);
    OB_UNIS_DECODE(tmp_phy_op_type);
    phy_op_type = static_cast<ObPhyOperatorType>(tmp_phy_op_type);
    kit = op_kit_store.get_operator_kit(index);
    if (nullptr == kit) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("op input is NULL", K(ret), K(index), K(phy_op_type), K(kit), K(real_input_count));
    } else if (nullptr == kit->input_) {
      LOG_WARN("op input is NULL", K(ret), K(index), K(phy_op_type), K(real_input_count));
    } else if (phy_op_type != kit->input_->get_type()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the type of op input is not match", K(ret), K(index),
        K(phy_op_type), K(kit->input_->get_type()));
    } else {
      OB_UNIS_DECODE(*kit->input_);
      ++ser_input_cnt;
    }
  }
  if (OB_SUCC(ret) && ser_input_cnt != real_input_count) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected status: ser input is not match", K(ret),
      K(ser_input_cnt), K(real_input_count));
  }
  return ret;
}

int64_t ObPxTreeSerializer::get_serialize_op_input_size(
  const ObOpSpec &op_spec, ObOpKitStore &op_kit_store)
{
  int64_t len = 0;
  int32_t real_input_count = 0;
  len += serialization::encoded_length_i32(real_input_count);
  len += get_serialize_op_input_tree_size(op_spec, op_kit_store, false /* is_fulltree */);
  return len;
}


int64_t ObPxTreeSerializer::get_serialize_op_input_subplan_size(
  const ObOpSpec &op_spec,
  ObOpKitStore &op_kit_store,
  bool is_fulltree)
{
  int ret = OB_SUCCESS;
  UNUSED(is_fulltree);
  UNUSED(op_spec);
  UNUSED(op_kit_store);
  int64_t len = 0;
  if (OB_SUCC(ret) && PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == op_spec.type_) {
    // do nothing
  }
  return len;
}

int64_t ObPxTreeSerializer::get_serialize_op_input_tree_size(
  const ObOpSpec &op_spec,
  ObOpKitStore &op_kit_store,
  bool is_fulltree)
{
  int ret = OB_SUCCESS;
  ObOpInput *op_spec_input = NULL;
  int64_t index = op_spec.id_;
  ObOperatorKit *kit = op_kit_store.get_operator_kit(index);
  int64_t len = 0;
  if (nullptr == kit) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("op input is NULL", K(ret), K(index));
  } else if (nullptr != (op_spec_input = kit->input_)) {
    OB_UNIS_ADD_LEN(index); //serialize index
    OB_UNIS_ADD_LEN(static_cast<int64_t>(op_spec_input->get_type())); //serialize operator type
    OB_UNIS_ADD_LEN(*op_spec_input); //serialize input parameter
  }
  if (PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == op_spec.type_) {
    len += get_serialize_op_input_subplan_size(op_spec, op_kit_store, true/*is_fulltree*/);
    // do nothing
  }
  is_fulltree = is_fulltree || IS_PX_COORD(op_spec.type_);
  // Terminate serialization when meet ObReceive, as this op indicates
  bool serialize_child = is_fulltree || (!IS_RECEIVE(op_spec.type_));
  if (OB_SUCC(ret) && serialize_child) {
    for (int32_t i = 0; OB_SUCC(ret) && i < op_spec.get_child_cnt(); ++i) {
      const ObOpSpec *child_op = op_spec.get_child(i);
      if (OB_ISNULL(child_op)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("null child operator", K(i), K(op_spec.type_));
      } else {
        len += get_serialize_op_input_tree_size(*child_op, op_kit_store, is_fulltree);
      }
    }
  }
  return len;
}

int ObPxTreeSerializer::serialize_op_input_subplan(
  char *buf,
  int64_t buf_len,
  int64_t &pos,
  const ObOpSpec &op_spec,
  ObOpKitStore &op_kit_store,
  bool is_fulltree,
  int32_t &real_input_count)
{
  UNUSED(buf);
  UNUSED(buf_len);
  UNUSED(pos);
  UNUSED(op_spec);
  UNUSED(op_kit_store);
  UNUSED(is_fulltree);
  UNUSED(real_input_count);
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == op_spec.type_) {
    // do nothing
  }
  return ret;
}

int ObPxTreeSerializer::serialize_op_input_tree(
  char *buf,
  int64_t buf_len,
  int64_t &pos,
  const ObOpSpec &op_spec,
  ObOpKitStore &op_kit_store,
  bool is_fulltree,
  int32_t &real_input_count)
{
  int ret = OB_SUCCESS;
  ObOpInput *op_spec_input = NULL;
  int64_t index = op_spec.id_;
  ObOperatorKit *kit = op_kit_store.get_operator_kit(index);
  if (nullptr == kit) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("op input is NULL", K(ret), K(index));
  } else if (nullptr != (op_spec_input = kit->input_)) {
    OB_UNIS_ENCODE(index); //serialize index
    OB_UNIS_ENCODE(static_cast<int64_t>(op_spec_input->get_type())); //serialize operator type
    OB_UNIS_ENCODE(*op_spec_input); //serialize input parameter
    if (OB_SUCC(ret)) {
      ++real_input_count;
    }
  }
  if (OB_SUCC(ret) && (PHY_TABLE_SCAN_WITH_DOMAIN_INDEX == op_spec.type_)
             && OB_FAIL(serialize_op_input_subplan(
               buf, buf_len, pos, op_spec, op_kit_store, true/*is_fulltree*/, real_input_count))) {
    LOG_WARN("fail to serialize sub plan", K(ret));
  }
  // Terminate serialization when meet ObReceive, as this op indicates
  is_fulltree = is_fulltree || IS_PX_COORD(op_spec.type_);
  bool serialize_child = is_fulltree || (!IS_RECEIVE(op_spec.type_));
  if (OB_SUCC(ret) && serialize_child) {
    for (int32_t i = 0; OB_SUCC(ret) && i < op_spec.get_child_cnt(); ++i) {
      const ObOpSpec *child_op = op_spec.get_child(i);
      if (OB_ISNULL(child_op)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("null child operator", K(i), K(op_spec.type_));
      } else if (OB_FAIL(serialize_op_input_tree(
          buf, buf_len, pos, *child_op, op_kit_store, is_fulltree, real_input_count))) {
        LOG_WARN("fail to serialize tree", K(ret));
      }
    }
  }
  return ret;
}

int ObPxTreeSerializer::serialize_op_input(
  char *buf,
  int64_t buf_len,
  int64_t &pos,
  const ObOpSpec &op_spec,
  ObOpKitStore &op_kit_store)
{
  int ret = OB_SUCCESS;
  int64_t input_start_pos = pos;
  int32_t real_input_count = 0;
  pos += serialization::encoded_length_i32(real_input_count);
  // Here we do not serialize input, later it will be done via copy, because the input required for deserialization is not available here, this is different from the old way
  if (OB_FAIL(serialize_op_input_tree(
      buf, buf_len, pos, op_spec, op_kit_store, false /* is_fulltree */, real_input_count))) {
    LOG_WARN("failed to serialize spec tree", K(ret));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialization::encode_i32(buf, buf_len, input_start_pos, real_input_count))) {
      LOG_WARN("encode int32_t", K(buf_len), K(input_start_pos), K(real_input_count));
    }
    LOG_TRACE("trace end ser input cnt", K(ret), K(real_input_count), K(op_kit_store.size_),
      K(pos), K(input_start_pos));
  }
  LOG_DEBUG("end trace ser kit store", K(buf_len), K(pos));
  return ret;
}

//------ end serialize plan tree for engine3.0

// first out timestamp of channel is the first in timestamp of receive
// last out timestamp of channel is the last in timestamp of receive

// transmit is same as channel
// eg: first in timestamp of channel is same as first in timestamp of transmit

int ObPxChannelUtil::unlink_ch_set(dtl::ObDtlChSet &ch_set, ObDtlFlowControl *dfc, bool batch_free_ch)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  dtl::ObDtlChannelInfo ci;
  /* ignore error, try to relase all channels */
  if (nullptr == dfc) {
    for (int64_t idx = 0; idx < ch_set.count(); ++idx) {
      if (OB_SUCCESS != (tmp_ret = ch_set.get_channel_info(idx, ci))) {
        ret = tmp_ret;
        LOG_ERROR("fail get channel info", K(idx), K(ret));
      }
      if (OB_SUCCESS != (tmp_ret = dtl::ObDtlChannelGroup::unlink_channel(ci))) {
        ret = tmp_ret;
        LOG_WARN("fail unlink channel", K(ci), K(ret));
      }
    }
  } else {
    // Detach every channel from the DTL registry before cleanup so local producers
    // cannot append messages while the PX worker releases flow-control state.
    ObSEArray<ObDtlChannel*, 16> chans;
    ObDtlChannel *ch = nullptr;
    ObDtlChannel *first_ch = nullptr;
    for (int64_t idx = 0; idx < ch_set.count(); ++idx) {
      if (OB_SUCCESS != (tmp_ret = ch_set.get_channel_info(idx, ci))) {
        ret = tmp_ret;
        LOG_ERROR("fail get channel info", K(idx), K(ret));
      }
      if (OB_SUCCESS != (tmp_ret = dtl::ObDtlChannelGroup::remove_channel(ci, ch))) {
        ret = tmp_ret;
        LOG_WARN("fail unlink channel", K(ci), K(ret));
      } else if (0 == idx) {
        first_ch = ch;
      }
      if (OB_SUCCESS != tmp_ret) {
        // do nothing
      } else if (OB_ISNULL(ch)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("channel is null", K(ci), K(ret));
      } else if (OB_SUCCESS != (tmp_ret = chans.push_back(ch))) {
        ret = tmp_ret;
        // If push back failed, we can only handle one channel at a time
        if (OB_SUCCESS != (tmp_ret = DTL.get_dfc_server().unregister_dfc_channel(*dfc, ch))) {
          ret = tmp_ret;
          LOG_ERROR("failed to unregister dfc channel", K(ci), K(ret), K(ret));
        }
        if (batch_free_ch) {
          if (NULL != ch) {
            ch->~ObDtlChannel();
            ch = NULL;
          }
        } else {
          ob_delete(ch);
        }
        LOG_WARN("failed to push back channels", K(ci), K(ret));
      }
    }
    if (OB_SUCCESS != (tmp_ret = DTL.get_dfc_server().deregister_dfc(*dfc))) {
      ret = tmp_ret;
      // the following unlink actions is not safe is any unregister failure happened
      LOG_ERROR("fail deregister all channel from dfc server", KR(tmp_ret));
    }
    if (0 < chans.count()) {
      for (int64_t idx = chans.count() - 1; 0 <= idx; --idx) {
        if (OB_SUCCESS != (tmp_ret = chans.pop_back(ch))) {
          ret = tmp_ret;
          LOG_ERROR("failed to unregister channel", K(ret));
        } else if (batch_free_ch) {
          if (NULL != ch) {
            ch->~ObDtlChannel();
            ch = NULL;
          }
        } else {
          ob_delete(ch);
        }
      }
    }
    if (batch_free_ch && nullptr != first_ch) {
      ob_free((void *)first_ch);
    }
  }
  return ret;
}


int ObPxChannelUtil::flush_rows(common::ObIArray<dtl::ObDtlChannel*> &channels)
{
  int ret = OB_SUCCESS;
  dtl::ObDtlChannel *ch = NULL;
  /* ignore error, try to flush all channels */
  for (int64_t slice_idx = 0; slice_idx < channels.count(); ++slice_idx) {
    if (NULL == (ch = channels.at(slice_idx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("unexpected NULL ptr", K(ret));
    } else {
      if (OB_FAIL(ch->flush())) {
        LOG_WARN("fail to flush row to slice channel", K(slice_idx), K(ret));
      }
    }
  }
  return ret;
}


int ObPxChannelUtil::sqcs_channles_asyn_wait(ObIArray<ObPxSqcMeta> &sqcs)
{
  int ret = OB_SUCCESS;
  ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
    ObDtlChannel *ch = sqcs.at(idx).get_qc_channel();
    if (OB_ISNULL(ch)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: channel is null", K(ret), K(idx));
    } else if (OB_FAIL(ch->flush())) {
      LOG_WARN("failed to wait for channel", K(ret), K(idx));
    }
  }
  return ret;
}

int ObPxAffinityByRandom::add_partition(int64_t tablet_id,
                                        int64_t tablet_idx,
                                        int64_t worker_cnt,
                                        ObPxTabletInfo &partition_row_info)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("add partition", K(tablet_id), K(tablet_idx), K(worker_cnt), K(this), K(order_partitions_));
  if (0 >= worker_cnt) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("The worker cnt is invalid", K(ret), K(worker_cnt));
  } else {
    TabletHashValue part_hash_value;
    if (order_partitions_) {
      part_hash_value.hash_value_ = 0;
    } else {
      uint64_t value = tablet_idx;
      part_hash_value.hash_value_ = common::murmurhash(&value, sizeof(value), worker_cnt);
    }
    part_hash_value.tablet_idx_ = tablet_idx;
    part_hash_value.tablet_id_ = tablet_id;
    part_hash_value.partition_info_ = partition_row_info;
    worker_cnt_ = worker_cnt;
    if (OB_FAIL(tablet_hash_values_.push_back(part_hash_value))) {
      LOG_WARN("Failed to push back item", K(ret));
    }
  }
  return ret;
}

int ObPxAffinityByRandom::do_random(bool use_partition_info)
{
  int ret = OB_SUCCESS;
  common::ObArray<int64_t> workers_load;
  if (OB_FAIL(workers_load.prepare_allocate(worker_cnt_))) {
    LOG_WARN("Failed to do prepare allocate", K(ret));
  } else if (!tablet_hash_values_.empty()) {
    ARRAY_FOREACH(workers_load, idx) {
      workers_load.at(idx) = 0;
    }
    /**
     * Why is it necessary to maintain the same order of partitions designed within the same worker?
     * Because in the case of global order, the partitions have already been sorted at the qc end.
     * If the order of partitions within a single worker is scrambled, it does not meet the expectation of global order,
     * meaning that the data within a single worker becomes unordered.
     */
    bool asc_order = true;
    if (tablet_hash_values_.count() > 1
        && (tablet_hash_values_.at(0).tablet_idx_ > tablet_hash_values_.at(1).tablet_idx_)) {
      asc_order = false;
    }
    if (order_partitions_) {
      // in partition wise affinity scenario, partition_idx of a pair of partitions may be different.
      // for example, T1 consists of p0, p1, p2 and T2 consists of p1, p2
      // T1.p1 <===> T2.p1  and T1.p2 <===> T2.p2
      // The partition_idx of T1.p1 is 1 and the partition_idx of T2.p1 is 0.
      // If we calculate hash value of partition_idx and sort partitions by the hash value,
      // T1.p1 and T2.p1 may be assigned to different worker.
      // So we sort partitions by partition_idx and generate a relative_idx which starts from zero.
      // Then calculate hash value with the relative_idx
      auto part_idx_compare_fun = [](TabletHashValue a, TabletHashValue b) -> bool { return a.tablet_idx_ > b.tablet_idx_; };
      lib::ob_sort(tablet_hash_values_.begin(),
                tablet_hash_values_.end(),
                part_idx_compare_fun);
      int64_t relative_idx = 0;
      for (int64_t i = 0; i < tablet_hash_values_.count(); i++) {
        uint64_t value = relative_idx;
        tablet_hash_values_.at(i).hash_value_ = common::murmurhash(&value, sizeof(value), worker_cnt_);
        relative_idx++;
      }
    }

    if (partition_random_affinitize_) {
    // First shuffle all the sequences
    auto compare_fun = [](TabletHashValue a, TabletHashValue b) -> bool { return a.hash_value_ > b.hash_value_; };
    lib::ob_sort(tablet_hash_values_.begin(),
              tablet_hash_values_.end(),
              compare_fun);
    LOG_TRACE("after sort partition_hash_values randomly", K(tablet_hash_values_), K(this), K(order_partitions_));
    } else {
      // donoting
    }
    // If there is no partition statistics information then place them round robin
    if (!use_partition_info) {
      int64_t worker_id = 0;
      for (int64_t idx = 0; idx < tablet_hash_values_.count(); ++idx) {
        if (worker_id > worker_cnt_ - 1) {
          worker_id = 0;
        }
        tablet_hash_values_.at(idx).worker_id_ = worker_id++;
      }
    } else {
      ARRAY_FOREACH(tablet_hash_values_, idx) {
        int64_t min_load_index = 0;
        int64_t min_load = INT64_MAX;
        ARRAY_FOREACH(workers_load, i) {
          if (workers_load.at(i) < min_load) {
            min_load_index = i;
            min_load = workers_load.at(i);
          }
        }
        tablet_hash_values_.at(idx).worker_id_ = min_load_index;
        // +1 handling is to prevent all partition statistics being 0
        workers_load.at(min_load_index) += tablet_hash_values_.at(idx).partition_info_.physical_row_count_ + 1;
      }
      LOG_DEBUG("Workers load", K(workers_load));
    }
    // Keep order
    if (asc_order) {
      auto compare_fun_order_by_part_asc = [](TabletHashValue a, TabletHashValue b) -> bool { return a.tablet_idx_ < b.tablet_idx_; };
      lib::ob_sort(tablet_hash_values_.begin(),
                tablet_hash_values_.end(),
                compare_fun_order_by_part_asc);
    } else {
      auto compare_fun_order_by_part_desc = [](TabletHashValue a, TabletHashValue b) -> bool { return a.tablet_idx_ > b.tablet_idx_; };
      lib::ob_sort(tablet_hash_values_.begin(),
                tablet_hash_values_.end(),
                compare_fun_order_by_part_desc);
    }
  }
  return ret;
}

int ObPxAffinityByRandom::get_tablet_info(int64_t tablet_id, ObIArray<ObPxTabletInfo> &tablets_info, ObPxTabletInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  bool find = false;
  if (!tablets_info.empty()) {
    ARRAY_FOREACH(tablets_info, idx) {
      if (tablets_info.at(idx).tablet_id_ == tablet_id) {
        find = true;
        tablet_info.assign(tablets_info.at(idx));
      }
    }
    if (!find) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Failed to find partition in partitions_info", K(ret), K(tablet_id), K(tablets_info));
    }
  }
  return ret;
}

double ObPxSqcUtil::get_sqc_partition_ratio(ObExecContext *exec_ctx)
{
  double ratio = 1.0;
  ObPxSqcHandler *sqc_handle = exec_ctx->get_sqc_handler();
  if (OB_NOT_NULL(sqc_handle)) {
    ObPxInitSqcArgs &sqc_args = sqc_handle->get_sqc_init_arg();
    int64_t total_part_cnt = sqc_args.sqc_.get_total_part_count();
    int64_t sqc_part_cnt = sqc_args.sqc_.get_access_table_locations().count();
    if (0 < total_part_cnt && 0 < sqc_part_cnt) {
      ratio = sqc_part_cnt * 1.0 / total_part_cnt;
    }
    LOG_TRACE("trace sqc partition ratio", K(total_part_cnt), K(sqc_part_cnt));
  }
  return ratio;
}

double ObPxSqcUtil::get_sqc_est_worker_ratio(ObExecContext *exec_ctx)
{
  double ratio = 1.0;
  ObPxSqcHandler *sqc_handle = exec_ctx->get_sqc_handler();
  if (OB_NOT_NULL(sqc_handle)) {
    ObPxInitSqcArgs &sqc_args = sqc_handle->get_sqc_init_arg();
    int64_t est_total_task_cnt = sqc_args.sqc_.get_total_task_count();
    int64_t est_sqc_task_cnt = sqc_args.sqc_.get_max_task_count();
    if (0 < est_total_task_cnt && 0 < est_sqc_task_cnt) {
      ratio = est_sqc_task_cnt * 1.0 / est_total_task_cnt;
    }
    LOG_TRACE("trace sqc estimate worker ratio", K(est_sqc_task_cnt), K(est_total_task_cnt));
  }
  return ratio;
}



int64_t ObPxSqcUtil::get_total_partition_count(ObExecContext *exec_ctx)
{
  int64_t total_part_cnt = 1;
  ObPxSqcHandler *sqc_handle = exec_ctx->get_sqc_handler();
  if (OB_NOT_NULL(sqc_handle)) {
    ObPxInitSqcArgs &sqc_args = sqc_handle->get_sqc_init_arg();
    int64_t tmp_total_part_cnt = sqc_args.sqc_.get_total_part_count();
    if (0 < tmp_total_part_cnt) {
      total_part_cnt = tmp_total_part_cnt;
    }
  }
  return total_part_cnt;
}

// SQC that belongs to leaf dfo should estimate size by GI or Tablescan
int64_t ObPxSqcUtil::get_actual_worker_count(ObExecContext *exec_ctx)
{
  int64_t sqc_actual_worker_cnt = 1;
  ObPxSqcHandler *sqc_handle = exec_ctx->get_sqc_handler();
  if (OB_NOT_NULL(sqc_handle)) {
    ObPxInitSqcArgs &sqc_args = sqc_handle->get_sqc_init_arg();
    sqc_actual_worker_cnt = sqc_args.sqc_.get_task_count();
  }
  return sqc_actual_worker_cnt;
}

uint64_t ObPxSqcUtil::get_plan_id(ObExecContext *exec_ctx)
{
  uint64_t plan_id = UINT64_MAX;
  if (OB_NOT_NULL(exec_ctx)) {
    ObPhysicalPlanCtx *plan_ctx = exec_ctx->get_physical_plan_ctx();
    if (OB_NOT_NULL(plan_ctx) && OB_NOT_NULL(plan_ctx->get_phy_plan())) {
      plan_id = plan_ctx->get_phy_plan()->get_plan_id();
    }
  }
  return plan_id;
}


uint64_t ObPxSqcUtil::get_exec_id(ObExecContext *exec_ctx)
{
  uint64_t exec_id = UINT64_MAX;
  if (OB_NOT_NULL(exec_ctx)) {
    exec_id = exec_ctx->get_execution_id();
  }
  return exec_id;
}

uint64_t ObPxSqcUtil::get_session_id(ObExecContext *exec_ctx)
{
  uint64_t session_id = UINT64_MAX;
  if (OB_NOT_NULL(exec_ctx)) {
    if (OB_NOT_NULL(exec_ctx->get_my_session())) {
      session_id = exec_ctx->get_my_session()->get_sessid_for_table();
    }
  }
  return session_id;
}

int ObPxEstimateSizeUtil::get_px_size(
  ObExecContext *exec_ctx,
  const PxOpSizeFactor factor,
  const int64_t total_size,
  int64_t &ret_size)
{
  int ret = OB_SUCCESS;
  int64_t actual_worker = 0;
  double sqc_part_ratio = 0;
  int64_t total_part_cnt = 0;
  double sqc_est_worker_ratio = 0;
  int64_t total_actual_worker_cnt = 0;
  ret_size = total_size;
  if (factor.has_leaf_granule()) {
    // leaf
    if (!factor.has_exchange()) {
      if (factor.has_block_granule()) {
        actual_worker = ObPxSqcUtil::get_actual_worker_count(exec_ctx);
        sqc_est_worker_ratio = ObPxSqcUtil::get_sqc_est_worker_ratio(exec_ctx);
        ret_size =  sqc_est_worker_ratio * total_size / actual_worker;
      } else if (factor.partition_granule_child_) {
        total_part_cnt = ObPxSqcUtil::get_total_partition_count(exec_ctx);
        ret_size = total_size / total_part_cnt;
      } else if (factor.partition_granule_parent_) {
        // total_size / total_part_cnt * sqc_part_cnt
        actual_worker = ObPxSqcUtil::get_actual_worker_count(exec_ctx);
        sqc_part_ratio = ObPxSqcUtil::get_sqc_partition_ratio(exec_ctx);
        ret_size = sqc_part_ratio * ret_size / actual_worker;
      }
    } else {
      if (factor.pk_exchange_) {
        if (factor.has_block_granule()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpect status: block granule with PK");
        } else {
          // total_size / total_part_cnt * sqc_part_cnt / sqc_autual_worker_count
          actual_worker = ObPxSqcUtil::get_actual_worker_count(exec_ctx);
          sqc_part_ratio = ObPxSqcUtil::get_sqc_partition_ratio(exec_ctx);
          ret_size = sqc_part_ratio * ret_size / actual_worker;
        }
      }
    }
  } else {
    if (factor.broadcast_exchange_) {
    } else {
      // Here it is currently not possible to allocate based on the actual number of threads, because the worker starts executing as soon as it is launched, and QC has not yet sent the total number of workers
      // Here it is assumed that all can be obtained
      actual_worker = ObPxSqcUtil::get_actual_worker_count(exec_ctx);
      sqc_est_worker_ratio = ObPxSqcUtil::get_sqc_est_worker_ratio(exec_ctx);
      ret_size = total_size * sqc_est_worker_ratio / actual_worker;
    }
  }
  if (ret_size > total_size || OB_FAIL(ret)) {
    LOG_WARN("unexpect status: estimate size is greater than total size",
      K(ret_size), K(total_size), K(ret));
    ret_size = total_size;
    ret = OB_SUCCESS;
  }
  if (0 >= ret_size) {
    ret_size = 1;
  }
  LOG_DEBUG("trace get px size", K(ret_size), K(total_size), K(factor), K(actual_worker),
    K(sqc_part_ratio), K(total_part_cnt), K(sqc_est_worker_ratio), K(total_actual_worker_cnt));
  return ret;
}

int ObSlaveMapUtil::build_mn_channel(
  ObPxChTotalInfos *dfo_ch_total_infos,
  ObDfo &child,
  ObDfo &parent)
{
  int ret = OB_SUCCESS;
  // Build channel ids and the local transmit/receive task layouts.
  if (OB_ISNULL(dfo_ch_total_infos)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("transmit or receive mn channel info is null", KP(dfo_ch_total_infos));
  } else {
    OZ(dfo_ch_total_infos->prepare_allocate(1));
    if (OB_SUCC(ret)) {
      ObDtlChTotalInfo &transmit_ch_info = dfo_ch_total_infos->at(0);
      OZ(ObDfo::fill_channel_info_by_sqc(transmit_ch_info.transmit_task_layout_, child.get_sqcs()));
      OZ(ObDfo::fill_channel_info_by_sqc(transmit_ch_info.receive_task_layout_, parent.get_sqcs()));
      transmit_ch_info.channel_count_ = transmit_ch_info.transmit_task_layout_.total_task_cnt_
                                      * transmit_ch_info.receive_task_layout_.total_task_cnt_;
      transmit_ch_info.start_channel_id_ = ObDtlChannel::generate_id(transmit_ch_info.channel_count_)
                                         - transmit_ch_info.channel_count_ + 1;
      
      if (transmit_ch_info.transmit_task_layout_.prefix_task_counts_.count() >
          transmit_ch_info.transmit_task_layout_.total_task_cnt_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected ch info", K(transmit_ch_info), K(child));
      }
    }
  }
  return ret;
}

int ObSlaveMapUtil::build_mn_channel_per_sqcs(
  ObPxChTotalInfos *dfo_ch_total_infos,
  ObDfo &child,
  ObDfo &parent,
  int64_t sqc_count)
{
  int ret = OB_SUCCESS;
  // Build one local channel layout for each slave-mapping SQC pair.
  if (OB_ISNULL(dfo_ch_total_infos)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("transmit or receive mn channel info is null", KP(dfo_ch_total_infos));
  } else if (OB_UNLIKELY(child.get_sqcs_count() != parent.get_sqcs_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sqc count not match in slave mapping plan", K(ret), K(parent), K(child));
  } else {
    OZ(dfo_ch_total_infos->prepare_allocate(sqc_count));
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < sqc_count && OB_SUCC(ret); ++i) {
        ObDtlChTotalInfo &transmit_ch_info = dfo_ch_total_infos->at(i);
        transmit_ch_info.is_local_shuffle_ = true;
        ObPxSqcMeta *child_sqc = &child.get_sqcs().at(i);
        ObPxSqcMeta *parent_sqc = &parent.get_sqcs().at(i);
        if (OB_SUCC(ret)) {
          OZ(ObDfo::fill_channel_info_by_sqc(transmit_ch_info.transmit_task_layout_, *child_sqc));
          OZ(ObDfo::fill_channel_info_by_sqc(transmit_ch_info.receive_task_layout_, *parent_sqc));
          transmit_ch_info.channel_count_ = transmit_ch_info.transmit_task_layout_.total_task_cnt_
                                          * transmit_ch_info.receive_task_layout_.total_task_cnt_;
          transmit_ch_info.start_channel_id_ = ObDtlChannel::generate_id(transmit_ch_info.channel_count_)
                                            - transmit_ch_info.channel_count_ + 1;
          
        }
      }
    }
  }
  LOG_DEBUG("build mn channel per sqcs", K(parent), K(child), KPC(dfo_ch_total_infos));
  return ret;
}
// The corresponding Plan is
// GI(Partition)
//    Hash Join           Hash Join
//      hash   ->>          Exchange(hash local)
//      hash                Exchange(hash local)
// Hash local means hashing is done only within the server, without shuffling across servers
int ObSlaveMapUtil::build_pwj_slave_map_mn_group(ObDfo &parent, ObDfo &child)
{
  int ret = OB_SUCCESS;
  ObPxChTotalInfos *dfo_ch_total_infos = &child.get_dfo_ch_total_infos();
  int64_t child_dfo_idx = -1;
  /**
   * According to the slave mapping design, we now group all threads within one machine as a group.
   * Here we will build the group based on the specific server executing the ch set.
   */
  if (parent.get_sqcs_count() != child.get_sqcs_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pwj must have the same sqc count", K(ret), K(parent.get_sqcs_count()), K(child.get_sqcs_count()));
  } else if (OB_FAIL(ObDfo::check_dfo_pair(parent, child, child_dfo_idx))) {
    LOG_WARN("failed to check dfo pair", K(ret));
  } else if (OB_FAIL(build_mn_channel_per_sqcs(
      dfo_ch_total_infos, child, parent, child.get_sqcs_count()))) {
    LOG_WARN("failed to build mn channel per sqc", K(ret));
  } else {
    LOG_DEBUG("build pwj slave map group", K(child.get_dfo_id()));
  }
  return ret;
}

int ObSlaveMapUtil::build_partition_map_by_sqcs(
  common::ObIArray<ObPxSqcMeta> &sqcs,
  ObDfo &child,
  ObIArray<int64_t> &prefix_task_counts,
  ObPxPartChMapArray &map)
{
  int ret = OB_SUCCESS;
  UNUSED(prefix_task_counts);
  DASTabletLocArray locations;
  if (prefix_task_counts.count() != sqcs.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("prefix task counts is not match sqcs count", K(ret));
  }
  for (int64_t i = 0; i < sqcs.count() && OB_SUCC(ret); i++) {
    ObPxSqcMeta &sqc = sqcs.at(i);
    locations.reset();
    if (OB_FAIL(get_pkey_table_locations(child.get_pkey_table_loc_id(),
        sqc, locations))) {
      LOG_WARN("fail to get pkey table locations", K(ret));
    }
    ARRAY_FOREACH_X(locations, loc_idx, loc_cnt, OB_SUCC(ret)) {
      const ObDASTabletLoc &location = *locations.at(loc_idx);
      int64_t tablet_id = location.tablet_id_.id();
      // int64_t prefix_task_count = prefix_task_counts.at(i);
      OZ(map.push_back(ObPxPartChMapItem(tablet_id, i)));
      LOG_DEBUG("debug push partition map", K(tablet_id), K(i), K(sqc.get_sqc_id()));
    }
  }
  LOG_DEBUG("debug push partition map", K(map));
  return ret;
}

int ObSlaveMapUtil::build_affinitized_partition_map_by_sqcs(
  common::ObIArray<ObPxSqcMeta> &sqcs,
  ObDfo &child,
  ObIArray<int64_t> &prefix_task_counts,
  int64_t total_task_cnt,
  ObPxPartChMapArray &map)
{
  int ret = OB_SUCCESS;
  DASTabletLocArray locations;
  if (OB_UNLIKELY(sqcs.count() <= 0 || prefix_task_counts.count() != sqcs.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("prefix task counts is not match sqcs count", K(sqcs.count()), K(ret));
  }
  for (int64_t i = 0; i < sqcs.count() && OB_SUCC(ret); i++) {
    // Target sqc has locations.count() partitions
    // Target sqc has sqc_task_count threads to process these partitions
    // Can adopt Round-Robin to distribute these partitions across threads
    // However, since it is assumed that the processing threads for the same part on the map side are always adjacent
    // This side is relatively easy to do hash operation.
    //
    // [p0][p1][p2][p3][p4]
    // [t0][t1][t0][t1][t0]
    //
    // [p0][p0][p1][p1][p2]
    // [t0][t1][t2][t3][t4]
    //
    // [p0][p1][p2][p3][p4]
    // [t0][t0][t1][t1][t2]
    //
    // task_per_part: number of threads serving each partition
    // double task_per_part = (double)sqc_task_count / locations.count();
    // for (int64_t idx = 0; idx < locations.count(); ++idx) {
    //  for (int j = 0; j < task_per_part; j++) {
    //    int64_t p = idx;
    //    int64_t t = idx * task_per_part + j;
    //    if (t >= sqc_task_count) {
    //      break;
    //    }
    //    emit(idx, t);
    //  }
    // }
    int64_t sqc_task_count = 0;
    int64_t prefix_task_count = prefix_task_counts.at(i);
    if (i + 1 == prefix_task_counts.count()) {
      sqc_task_count = total_task_cnt - prefix_task_counts.at(i);
    } else {
      sqc_task_count = prefix_task_counts.at(i + 1) - prefix_task_counts.at(i);
    }
    ObPxSqcMeta &sqc = sqcs.at(i);
    locations.reset();
    if (OB_FAIL(get_pkey_table_locations(child.get_pkey_table_loc_id(),
        sqc, locations))) {
      LOG_WARN("fail to get pkey table locations", K(ret));
    }
    if (locations.count() <= 0 || sqc_task_count <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the size of location is zero in one sqc", K(ret), K(sqc_task_count), K(sqc));
      break;
    }
    // task_per_part: how many threads serve each partition,
    // rest_task: these task each take on one more partition, don't waste cpu
    // For example:
    // 13 threads, 3 partitions, task_per_part = 4, rest_task = 1 to do the first partition
    // 3 threads, 5 partitions, task_per_part = 0, rest_task = 3 to do partitions 1, 2, 3
    int64_t task_per_part = sqc_task_count / locations.count();
    int64_t rest_task = sqc_task_count % locations.count();
    if (task_per_part > 0) {
      // Scenario 1: Number of threads > Number of partitions
      // Key point: the number of threads allocated to each partition should be consecutive
      int64_t t = 0;
      for (int64_t p = 0; OB_SUCC(ret) && p < locations.count(); ++p) {
        int64_t tablet_id = locations.at(p)->tablet_id_.id();
        int64_t next = (p >= rest_task) ? task_per_part : task_per_part + 1;
        for (int64_t loop = 0; OB_SUCC(ret) && loop < next; ++loop) {
          // first：tablet id, second: prefix_task_count, third: sqc_task_idx
          OZ(map.push_back(ObPxPartChMapItem(tablet_id, prefix_task_count, t)));
          LOG_DEBUG("t>p: push partition map", K(tablet_id), "sqc", i, "g_t", prefix_task_count + t, K(t));
          t++;
        }
      }
    } else {
      // Scenario 2: number of threads < number of partitions
      // Key point: ensure each partition has at least one thread processing
      for (int64_t p = 0; OB_SUCC(ret) && p < locations.count(); ++p) {
        int64_t t = p % rest_task;
        int64_t tablet_id = locations.at(p)->tablet_id_.id();
        // Specific meaning, reference ObPxPartChMapItem:
        // first：tablet_id, second: prefix_task_count, third: sqc_task_idx
        OZ(map.push_back(ObPxPartChMapItem(tablet_id, prefix_task_count, t)));
        LOG_DEBUG("t<=p: push partition map", K(tablet_id), "sqc", i, "g_t", prefix_task_count + t, K(t));
      }
    }
  }
  LOG_DEBUG("debug push partition map", K(map));
  return ret;
}

int ObSlaveMapUtil::build_ppwj_bcast_slave_mn_map(ObDfo &parent, ObDfo &child)
{
  int ret = OB_SUCCESS;
  int64_t child_dfo_idx = -1;
  ObPxChTotalInfos *dfo_ch_total_infos = &child.get_dfo_ch_total_infos();
  ObPxPartChMapArray &map = child.get_part_ch_map();
  LOG_DEBUG("build ppwj bcast slave map", K(parent.get_dfo_id()), K(parent.get_sqcs_count()),
      K(parent.get_total_task_count()));
  ObIArray<ObPxSqcMeta> &sqcs = parent.get_sqcs();
  if (OB_UNLIKELY(sqcs.count() <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the count of sqc is unexpected", K(ret), K(sqcs.count()));
  } else if (OB_FAIL(ObDfo::check_dfo_pair(parent, child, child_dfo_idx))) {
    LOG_WARN("failed to check dfo pair", K(ret));
  } else if (OB_FAIL(build_mn_channel(dfo_ch_total_infos, child, parent))) {
    LOG_WARN("failed to build mn channels", K(ret));
  } else if (OB_ISNULL(dfo_ch_total_infos) || 1 != dfo_ch_total_infos->count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected status: receive ch info is error", K(ret), KP(dfo_ch_total_infos));
  } else if (OB_FAIL(build_partition_map_by_sqcs(
      sqcs, child,
      dfo_ch_total_infos->at(0).receive_task_layout_.prefix_task_counts_, map))) {
    LOG_WARN("failed to build channel map by sqc", K(ret));
  } else if (map.count() <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the size of channel map is unexpected", K(ret), K(map.count()));
  }
  return ret;
}

int ObSlaveMapUtil::build_ppwj_slave_mn_map(ObDfo &parent, ObDfo &child)
{
  int ret = OB_SUCCESS;
  if (ObPQDistributeMethod::PARTITION_HASH == child.get_dist_method()) {
    ObDfo *reference_child = nullptr;
    int64_t child_dfo_idx = -1;
    ObPxChTotalInfos *dfo_ch_total_infos = &child.get_dfo_ch_total_infos();
    ObPxPartChMapArray &map = child.get_part_ch_map();
    if (OB_FAIL(ObPxSqcDistributionUtil::find_reference_child(parent, reference_child))) {
      LOG_WARN("find reference child", K(ret));
    } else if (OB_ISNULL(reference_child)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null child", K(ret));
    } else if (OB_FAIL(ObDfo::check_dfo_pair(parent, child, child_dfo_idx))) {
      LOG_WARN("failed to check dfo pair", K(ret));
    } else if (OB_FAIL(build_mn_channel(dfo_ch_total_infos, child, parent))) {
      LOG_WARN("failed to build mn channels", K(ret));
    } else if (OB_ISNULL(dfo_ch_total_infos) || 1 != dfo_ch_total_infos->count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: receive ch info is error", K(ret), KP(dfo_ch_total_infos));
    } else if (OB_UNLIKELY(reference_child->get_sqcs_count() <= 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the count of sqc is unexpected", K(ret));
    } else if (OB_FAIL(build_partition_map_by_sqcs(
        reference_child->get_sqcs(), child,
        dfo_ch_total_infos->at(0).receive_task_layout_.prefix_task_counts_, map))) {
      LOG_WARN("failed to build channel map by sqc", K(ret));
    }
  } else if (OB_FAIL(build_pwj_slave_map_mn_group(parent, child))) {
    LOG_WARN("failed to build ppwj slave map", K(ret));
  }
  return ret;
}
// This function is used for pdml scenario, supports:
//  1. Map pkey to one or more threads
//  2. Map multiple pkey to one thread
// And ensure: pkey minimized distribution (under full utilization of computing power, let as few threads as possible handle the same partition concurrently)
// pkey-hash, pkey-range etc can all use this map
int ObSlaveMapUtil::build_pkey_affinitized_ch_mn_map(ObDfo &parent,
                                                     ObDfo &child)
{
  int ret = OB_SUCCESS;
  if (1 != parent.get_child_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected dfo", K(ret), K(parent));
  } else if (ObPQDistributeMethod::PARTITION_HASH == child.get_dist_method()
      || ObPQDistributeMethod::PARTITION_RANGE == child.get_dist_method()) {
    LOG_TRACE("build pkey affinitized channel map",
      K(parent.get_dfo_id()), K(parent.get_sqcs_count()),
      K(child.get_dfo_id()), K(child.get_sqcs_count()));
      //  .....
      //    PDML
      //      EX (pkey hash)
      //  .....
      // Establish the channel map for child dfo to send data, in the pkey random model, each worker in every SQC of the parent dfo can
      // process all partitions contained in its corresponding SQC, so directly use `build_ch_map_by_sqcs`
      ObPxPartChMapArray &map = child.get_part_ch_map();
      ObIArray<ObPxSqcMeta> &sqcs = parent.get_sqcs();
      ObPxChTotalInfos *dfo_ch_total_infos = &child.get_dfo_ch_total_infos();
      int64_t child_dfo_idx = -1;
      if (OB_UNLIKELY(sqcs.count() <= 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the count of sqc is unexpected", K(ret), K(sqcs.count()));
      } else if (OB_FAIL(ObDfo::check_dfo_pair(parent, child, child_dfo_idx))) {
        LOG_WARN("failed to check dfo pair", K(ret));
      } else if (OB_FAIL(build_mn_channel(dfo_ch_total_infos, child, parent))) {
        LOG_WARN("failed to build mn channels", K(ret));
      } else if (OB_ISNULL(dfo_ch_total_infos) || 1 != dfo_ch_total_infos->count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected status: receive ch info is error", K(ret), KP(dfo_ch_total_infos));
      } else if (OB_FAIL(build_affinitized_partition_map_by_sqcs(
          sqcs,
          child,
          dfo_ch_total_infos->at(0).receive_task_layout_.prefix_task_counts_,
          dfo_ch_total_infos->at(0).receive_task_layout_.total_task_cnt_,
          map))) {
        LOG_WARN("failed to build channel map by sqc", K(ret));
      } else if (map.count() <= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the size of channel map is unexpected",
          K(ret), K(map.count()), K(parent.get_total_task_count()), K(sqcs));
      }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected child dfo", K(ret), K(child.get_dist_method()));
  }
  return ret;
}


int ObSlaveMapUtil::build_pkey_random_ch_mn_map(ObDfo &parent, ObDfo &child)
{
  int ret = OB_SUCCESS;
  if (1 != parent.get_child_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected dfo", K(ret), K(parent));
  } else if (ObPQDistributeMethod::PARTITION_RANDOM == child.get_dist_method()) {
    LOG_TRACE("build pkey random channel map",
      K(parent.get_dfo_id()), K(parent.get_sqcs_count()),
      K(child.get_dfo_id()), K(child.get_sqcs_count()));
      //  .....
      //    PDML
      //      EX (pkey random)
      //  .....
      // Establish the channel map for child dfo to send data, in the pkey random model, each worker in every SQC of the parent dfo can
      // process all partitions contained in its corresponding SQC, so directly use `build_ch_map_by_sqcs`
      ObPxPartChMapArray &map = child.get_part_ch_map();
      ObIArray<ObPxSqcMeta> &sqcs = parent.get_sqcs();
      ObPxChTotalInfos *dfo_ch_total_infos = &child.get_dfo_ch_total_infos();
      int64_t child_dfo_idx = -1;
      if (OB_UNLIKELY(sqcs.count() <= 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the count of sqc is unexpected", K(ret), K(sqcs.count()));
      } else if (OB_FAIL(ObDfo::check_dfo_pair(parent, child, child_dfo_idx))) {
        LOG_WARN("failed to check dfo pair", K(ret));
      } else if (OB_FAIL(build_mn_channel(dfo_ch_total_infos, child, parent))) {
        LOG_WARN("failed to build mn channels", K(ret));
      } else if (OB_ISNULL(dfo_ch_total_infos) || 1 != dfo_ch_total_infos->count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected status: receive ch info is error", K(ret), KP(dfo_ch_total_infos));
      } else if (OB_FAIL(build_partition_map_by_sqcs(
          sqcs, child,
          dfo_ch_total_infos->at(0).receive_task_layout_.prefix_task_counts_, map))) {
        LOG_WARN("failed to build channel map by sqc", K(ret));
      } else if (map.count() <= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the size of channel map is unexpected",
          K(ret), K(map.count()), K(parent.get_total_task_count()), K(sqcs));
      }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected child dfo", K(ret), K(child.get_dist_method()));
  }
  return ret;
}

int ObSlaveMapUtil::build_ppwj_ch_mn_map(ObExecContext &ctx, ObDfo &parent, ObDfo &child)
{
  int ret = OB_SUCCESS;
  // dfo can get all dfo related partitions
  // From which all related partition ids can be calculated
  // According to partition id use a determined hash algorithm to calculate the mapping relationship between id and channel
  // 1. Traverse all sqc in dfo, which includes partition id
  // 2. Hash the partition id to calculate the task_id
  // 3. Traverse tasks, find i, satisfying:
  //    tasks[i].server = sqc.server && tasks[i].task_id = hash(tablet_id)
  // 4. Record (tablet_id, i) to the map
  ObIArray<ObPxSqcMeta> &sqcs = parent.get_sqcs();
  ObPxPartChMapArray &map = child.get_part_ch_map();
  int64_t child_dfo_idx = -1;
  ObPxChTotalInfos *dfo_ch_total_infos = &child.get_dfo_ch_total_infos();
  if (OB_FAIL(map.reserve(parent.get_total_task_count()))) {
    LOG_WARN("fail reserve memory for map", K(ret), K(parent.get_total_task_count()));
  } else if (OB_FAIL(ObDfo::check_dfo_pair(parent, child, child_dfo_idx))) {
    LOG_WARN("failed to check dfo pair", K(ret));
  } else if (OB_FAIL(build_mn_channel(dfo_ch_total_infos, child, parent))) {
    LOG_WARN("failed to build mn channels", K(ret));
  } else if (OB_ISNULL(dfo_ch_total_infos) || 1 != dfo_ch_total_infos->count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected status: receive ch info is error", K(ret), KP(dfo_ch_total_infos));
  } else {
    share::schema::ObSchemaGetterGuard schema_guard;
    const share::schema::ObTableSchema *table_schema = NULL;
    ObTabletIdxMap idx_map;
    common::ObIArray<int64_t> &prefix_task_counts =
      dfo_ch_total_infos->at(0).receive_task_layout_.prefix_task_counts_;
    if (prefix_task_counts.count() != sqcs.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: prefix task count is not match sqcs count", K(ret),
        KP(prefix_task_counts.count()), K(sqcs.count()));
    }
    DASTabletLocArray locations;
    ARRAY_FOREACH_X(sqcs, idx, cnt, OB_SUCC(ret)) {
      // All affinitize calculations are SQC local, not global.
      ObPxSqcMeta &sqc = sqcs.at(idx);
      ObPxAffinityByRandom affinitize_rule(sqc.sqc_order_gi_tasks(),
                                           sqc.partition_random_affinitize());
      LOG_TRACE("build ppwj_ch_mn_map", K(sqc));
      ObPxTabletInfo partition_row_info;
      locations.reset();
      if (OB_FAIL(get_pkey_table_locations(child.get_pkey_table_loc_id(), sqc, locations))) {
        LOG_WARN("fail to get pkey table locations", K(ret));
      } else if (OB_FAIL(affinitize_rule.reserve(locations.count()))) {
        LOG_WARN("fail reserve memory", K(ret), K(locations.count()));
      }
      int64_t tablet_idx = OB_INVALID_ID;
      ARRAY_FOREACH_X(locations, loc_idx, loc_cnt, OB_SUCC(ret)) {
        const ObDASTabletLoc &location = *locations.at(loc_idx);
        if (NULL == table_schema) {
          uint64_t table_id = location.loc_meta_->ref_table_id_;
          if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(
                      schema_guard))) {
            LOG_WARN("faile to get schema guard", K(ret));
          } else if (OB_FAIL(schema_guard.get_table_schema(
                     table_id, table_schema))) {
            LOG_WARN("faile to get table schema", K(ret), K(table_id));
          } else if (OB_ISNULL(table_schema)) {
            ret = OB_TABLE_NOT_EXIST;
            LOG_WARN("table schema is null", K(ret), K(table_id));
          } else if (OB_FAIL(ObPxSqcDistributionUtil::build_tablet_idx_map(table_schema, idx_map))) {
            LOG_WARN("fail to build tablet idx map", K(ret));
          }
        }
        if (OB_FAIL(ret)) {
          // pass
        } else if (OB_FAIL(idx_map.get_refactored(location.tablet_id_.id(), tablet_idx))) {
          ret = OB_HASH_NOT_EXIST == ret ? OB_SCHEMA_ERROR : ret;
          LOG_WARN("fail to get tablet idx", K(ret));
        } else if (OB_FAIL(ObPxAffinityByRandom::get_tablet_info(location.tablet_id_.id(),
                                                                 sqc.get_partitions_info(),
                                                                 partition_row_info))) {
          LOG_WARN("Failed to get partition info", K(ret));
        } else if (OB_FAIL(affinitize_rule.add_partition(location.tablet_id_.id(),
                tablet_idx,
                sqc.get_task_count(),
                partition_row_info))) {
          LOG_WARN("fail calc task_id", K(location.tablet_id_), K(sqc), K(ret));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(affinitize_rule.do_random(!sqc.get_partitions_info().empty()))) {
        LOG_WARN("failed to do random", K(ret));
      } else {
        const ObIArray<ObPxAffinityByRandom::TabletHashValue> &partition_worker_pairs =
          affinitize_rule.get_result();
        int64_t prefix_task_count = prefix_task_counts.at(idx);
        ARRAY_FOREACH(partition_worker_pairs, idx) {
          int64_t tablet_id = partition_worker_pairs.at(idx).tablet_id_;
          int64_t task_id = partition_worker_pairs.at(idx).worker_id_;
          OZ(map.push_back(ObPxPartChMapItem(tablet_id, prefix_task_count, task_id)));
          LOG_DEBUG("debug push partition map", K(tablet_id), K(task_id));
        }
        LOG_DEBUG("Get all partition rows info", K(ret), K(sqc.get_partitions_info()));
      }
    }
  }
  return ret;
}

int ObSlaveMapUtil::build_slave_mapping_mn_ch_map(ObExecContext &ctx, ObDfo &child, ObDfo &parent)
{
  int ret = OB_SUCCESS;
  SlaveMappingType slave_type = parent.get_in_slave_mapping_type();
  switch(slave_type) {
  case SlaveMappingType::SM_PWJ_HASH_HASH : {
    if (OB_FAIL(build_pwj_slave_map_mn_group(parent, child))) {
      LOG_WARN("fail to build pwj slave map", K(ret));
    }
    break;
  }
  case SlaveMappingType::SM_PPWJ_BCAST_NONE :
  case SlaveMappingType::SM_PPWJ_NONE_BCAST : {
    if (OB_FAIL(build_ppwj_bcast_slave_mn_map(parent, child))) {
      LOG_WARN("fail to build pwj slave map", K(ret));
    }
    break;
  }
  case SlaveMappingType::SM_PPWJ_HASH_HASH : {
    if (OB_FAIL(build_ppwj_slave_mn_map(parent, child))) {
      LOG_WARN("fail to build pwj slave map", K(ret));
    }
    break;
  }
  case SlaveMappingType::SM_NONE : {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected slave mapping type", K(child.get_dfo_id()), K(parent.get_dfo_id()));
    break;
  }
  }
  LOG_DEBUG("debug distribute type", K(child.get_dfo_id()), K(parent.get_dfo_id()), K(slave_type));
  return ret;
}

int ObSlaveMapUtil::build_pkey_mn_ch_map(ObExecContext &ctx, ObDfo &child, ObDfo &parent)
{
  int ret = OB_SUCCESS;
  ObPQDistributeMethod::Type child_dist_method = child.get_dist_method();
  switch(child_dist_method) {
  case ObPQDistributeMethod::Type::PARTITION : {
    // for normal pkey
    if (OB_FAIL(build_ppwj_ch_mn_map(ctx, parent, child))) {
      LOG_WARN("failed to build partial partition wise join channel map", K(ret));
    }
    break;
  }
  case ObPQDistributeMethod::Type::PARTITION_HASH:
  case ObPQDistributeMethod::Type::PARTITION_RANGE: {
    if (OB_FAIL(build_pkey_affinitized_ch_mn_map(parent, child))) {
      LOG_WARN("failed to build pkey random channel map", K(ret));
    }
    break;
  }
  case ObPQDistributeMethod::Type::PARTITION_RANDOM: {
    // PDML: shuffle to any worker in parent dfo
    if (OB_FAIL(build_pkey_random_ch_mn_map(parent, child))) {
      LOG_WARN("failed to build pkey random channel map", K(ret));
    }
    break;
  }
  default: {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected child dist method type", K(child.get_dfo_id()), K(parent.get_dfo_id()),
             K(child_dist_method));
    break;
  }
  }
  LOG_DEBUG("debug distribute type", K(child.get_dfo_id()), K(parent.get_dfo_id()),
            K(child.get_dist_method()));
  return ret;
}

int ObSlaveMapUtil::get_pkey_table_locations(int64_t table_location_key,
    ObPxSqcMeta &sqc,
    DASTabletLocIArray &pkey_locations)
{
  int ret = OB_SUCCESS;
  pkey_locations.reset();
  DASTabletLocIArray &access_table_locations = sqc.get_access_table_locations_for_update();
  ObIArray<ObSqcTableLocationIndex> &location_indexes = sqc.get_access_table_location_indexes();
  int64_t cnt = location_indexes.count();
  // from end to start is necessary!
  for (int i = cnt - 1; i >= 0 && OB_SUCC(ret); --i) {
    if (table_location_key == location_indexes.at(i).table_location_key_) {
      int64_t start = location_indexes.at(i).location_start_pos_;
      int64_t end = location_indexes.at(i).location_end_pos_;
      for (int j = start;j <= end && OB_SUCC(ret); ++j) {
        if (j >= 0 && j < access_table_locations.count()) {
          OZ(pkey_locations.push_back(access_table_locations.at(j)));
        }
      }
      break;
    }
  }
  if (OB_SUCC(ret) && pkey_locations.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected locations", K(location_indexes), K(access_table_locations), K(table_location_key), K(ret));
  }
  return ret;
}

int ObDtlChannelUtil::get_mn_receive_dtl_channel_set(
  const int64_t sqc_id,
  const int64_t task_id,
  ObDtlChTotalInfo &ch_total_info,
  ObDtlChSet &ch_set)
{
  int ret = OB_SUCCESS;
  // receive
  int64_t ch_cnt = 0;
  if (0 > sqc_id || sqc_id >= ch_total_info.receive_task_layout_.prefix_task_counts_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid sqc id", K(sqc_id), K(ret),
      K(ch_total_info.receive_task_layout_.prefix_task_counts_.count()));
  } else {
    int64_t start_task_id = ch_total_info.receive_task_layout_.prefix_task_counts_.at(sqc_id);
    int64_t receive_task_cnt = ch_total_info.receive_task_layout_.total_task_cnt_;
    int64_t base_chid = ch_total_info.start_channel_id_ + (start_task_id + task_id);
    int64_t chid = 0;
    ObIArray<int64_t> &prefix_task_counts = ch_total_info.transmit_task_layout_.prefix_task_counts_;
    int64_t pre_prefix_task_count = 0;
    if (OB_FAIL(ch_set.reserve(ch_total_info.transmit_task_layout_.total_task_cnt_))) {
      LOG_WARN("fail reserve memory for channels", K(ret),
               "channels", ch_total_info.transmit_task_layout_.total_task_cnt_);
    }
    // Traverse all servers of transmit, and construct the channel between this receive task and each of them
    for (int64_t i = 0; i < prefix_task_counts.count() && OB_SUCC(ret); ++i) {
      int64_t prefix_task_count = 0;
      if (i + 1 == prefix_task_counts.count()) {
        prefix_task_count = ch_total_info.transmit_task_layout_.total_task_cnt_;
      } else {
        prefix_task_count = prefix_task_counts.at(i + 1);
      }
      // [pre_prefix_task_count, prefix_task_count) represents the transmit tasks in the i-th sqc of transmit,
      // The index number in all sqcs' transmit tasks
      for (int64_t j = pre_prefix_task_count; j < prefix_task_count && OB_SUCC(ret); ++j) {
        ObDtlChannelInfo ch_info;
        chid = base_chid + receive_task_cnt * j;
        ObDtlChannelGroup::make_receive_channel(chid, ch_info);
        OZ(ch_set.add_channel_info(ch_info));
        LOG_DEBUG("debug receive channel", KP(chid), K(ch_info), K(sqc_id), K(task_id),
          K(ch_total_info.start_channel_id_));
        ++ch_cnt;
      }
      pre_prefix_task_count = prefix_task_count;
    }
    if (OB_SUCC(ret) && ch_cnt != ch_total_info.transmit_task_layout_.total_task_cnt_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: channel count is not match", K(ch_cnt),
        K(ch_total_info.channel_count_), K(receive_task_cnt), K(prefix_task_counts),
        K(ch_total_info.transmit_task_layout_.total_task_cnt_), K(sqc_id), K(task_id));
    }
  }
  LOG_DEBUG("get mn receive dtl channel set", K(sqc_id), K(task_id), K(ch_total_info), K(ch_set));
  return ret;
}

int ObDtlChannelUtil::get_sm_receive_dtl_channel_set(
  const int64_t sqc_id,
  const int64_t task_id,
  ObDtlChTotalInfo &ch_total_info,
  ObDtlChSet &ch_set)
{
  int ret = OB_SUCCESS;
  UNUSED(sqc_id);
  int64_t receive_task_cnt = ch_total_info.receive_task_layout_.total_task_cnt_;
  int64_t transmit_task_cnt = ch_total_info.transmit_task_layout_.total_task_cnt_;
  if (OB_UNLIKELY(1 != ch_total_info.receive_task_layout_.prefix_task_counts_.count()
                  || 1 != ch_total_info.transmit_task_layout_.prefix_task_counts_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected local task group count", K(ret), K(ch_total_info));
  } else if (OB_FAIL(ch_set.reserve(transmit_task_cnt))) {
    LOG_WARN("fail reserve memory for channels", K(ret), K(transmit_task_cnt));
  } else {
    int64_t chid = 0;
    for (int64_t i = 0; i < transmit_task_cnt && OB_SUCC(ret); ++i) {
      ObDtlChannelInfo ch_info;
      chid = ch_total_info.start_channel_id_ + task_id + receive_task_cnt * i;
      ObDtlChannelGroup::make_receive_channel(chid, ch_info);
      OZ(ch_set.add_channel_info(ch_info));
    }
  }
  LOG_DEBUG("get sm receive dtl channel set", K(sqc_id), K(task_id), K(ch_total_info), K(ch_set));
  return ret;
}

int ObDtlChannelUtil::get_mn_transmit_dtl_channel_set(
  const int64_t sqc_id,
  const int64_t task_id,
  ObDtlChTotalInfo &ch_total_info,
  ObDtlChSet &ch_set)
{
  int ret = OB_SUCCESS;
  if (0 > sqc_id || sqc_id >= ch_total_info.transmit_task_layout_.prefix_task_counts_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid sqc id", K(sqc_id), K(ret),
      K(ch_total_info.transmit_task_layout_.prefix_task_counts_.count()));
  } else {
    int64_t start_task_id = ch_total_info.transmit_task_layout_.prefix_task_counts_.at(sqc_id);
    int64_t base_chid = ch_total_info.start_channel_id_
          + (start_task_id + task_id) * ch_total_info.receive_task_layout_.total_task_cnt_;
    ObIArray<int64_t> &prefix_task_counts = ch_total_info.receive_task_layout_.prefix_task_counts_;
    int64_t ch_cnt = 0;
    int64_t pre_prefix_task_count = 0;
    int64_t chid = 0;
    if (OB_FAIL(ch_set.reserve(ch_total_info.receive_task_layout_.total_task_cnt_))) {
      LOG_WARN("fail reserve memory for channels", K(ret),
               "channels", ch_total_info.receive_task_layout_.total_task_cnt_);
    }
    for (int64_t i = 0; i < prefix_task_counts.count() && OB_SUCC(ret); ++i) {
      int64_t prefix_task_count = 0;
      if (i + 1 == prefix_task_counts.count()) {
        prefix_task_count = ch_total_info.receive_task_layout_.total_task_cnt_;
      } else {
        prefix_task_count = prefix_task_counts.at(i + 1);
      }
      for (int64_t j = pre_prefix_task_count; j < prefix_task_count && OB_SUCC(ret); ++j) {
        ObDtlChannelInfo ch_info;
        chid = base_chid + j;
        ObDtlChannelGroup::make_transmit_channel(chid, ch_info);
        OZ(ch_set.add_channel_info(ch_info));
        ++ch_cnt;
        LOG_DEBUG("debug transmit channel", KP(chid), K(ch_info), K(sqc_id), K(task_id),
          K(ch_total_info.start_channel_id_));
      }
      pre_prefix_task_count = prefix_task_count;
    }
    if (OB_SUCC(ret) && ch_cnt != ch_total_info.receive_task_layout_.total_task_cnt_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: channel count is not match", K(ch_cnt),
        K(ch_total_info.transmit_task_layout_.total_task_cnt_));
    }
  }
  LOG_DEBUG("get transmit dtl channel set", K(sqc_id), K(task_id), K(ch_total_info), K(ch_set));
  return ret;
}

int ObDtlChannelUtil::get_sm_transmit_dtl_channel_set(
  const int64_t sqc_id,
  const int64_t task_id,
  ObDtlChTotalInfo &ch_total_info,
  ObDtlChSet &ch_set)
{
  int ret = OB_SUCCESS;
  UNUSED(sqc_id);
  int64_t receive_task_cnt = ch_total_info.receive_task_layout_.total_task_cnt_;
  int64_t transmit_task_cnt = ch_total_info.transmit_task_layout_.total_task_cnt_;
  if (OB_UNLIKELY(1 != ch_total_info.receive_task_layout_.prefix_task_counts_.count()
                  || 1 != ch_total_info.transmit_task_layout_.prefix_task_counts_.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected local task group count", K(ret), K(ch_total_info));
  } else if (OB_FAIL(ch_set.reserve(receive_task_cnt))) {
    LOG_WARN("fail reserve memory for channels", K(ret), K(receive_task_cnt));
  } else {
    int64_t chid = 0;
    for (int64_t i = 0; i < receive_task_cnt && OB_SUCC(ret); ++i) {
      ObDtlChannelInfo ch_info;
      chid = ch_total_info.start_channel_id_ + receive_task_cnt * task_id + i;
      ObDtlChannelGroup::make_transmit_channel(chid, ch_info);
      OZ(ch_set.add_channel_info(ch_info));
    }
  }
  LOG_DEBUG("get sm receive dtl channel set", K(sqc_id), K(task_id), K(transmit_task_cnt),
           K(receive_task_cnt), K(ch_total_info), K(ch_set));
  return ret;
}


bool ObVirtualTableErrorWhitelist::should_ignore_vtable_error(int error_code)
{
  bool should_ignore = false;
  switch (error_code) {
    CASE_IGNORE_ERR_HELPER(OB_ALLOCATE_MEMORY_FAILED)
    default: {
      if (is_schema_error(error_code)) {
        should_ignore = true;
        const int ret = error_code;
        LOG_WARN("ignore schema error", KR(ret));
      }
      break;
    }
  }
  return should_ignore;
}

int LowestCommonAncestorFinder::find_op_common_ancestor(
    const ObOpSpec *left, const ObOpSpec *right, const ObOpSpec *&ancestor)
{
  int ret = OB_SUCCESS;
  ObSEArray<const ObOpSpec *, 32> ancestors;

  const ObOpSpec *parent = left;
  while (OB_NOT_NULL(parent) && OB_SUCC(ret)) {
    if (OB_FAIL(ancestors.push_back(parent))) {
      LOG_WARN("failed to push back");
    } else {
      parent = parent->get_parent();
    }
  }

  parent = right;
  bool find = false;
  while (OB_NOT_NULL(parent) && OB_SUCC(ret) && !find) {
    for (int64_t i = 0; i < ancestors.count() && OB_SUCC(ret); ++i) {
      if (parent == ancestors.at(i)) {
        find = true;
        ancestor = parent;
        break;
      }
    }
    parent = parent->get_parent();
  }
  return ret;
}

int LowestCommonAncestorFinder::get_op_dfo(const ObOpSpec *op, ObDfo *root_dfo, ObDfo *&op_dfo)
{
  int ret = OB_SUCCESS;
  const ObOpSpec *parent = op;
  const ObOpSpec *dfo_root_op = nullptr;
  while (OB_NOT_NULL(parent) && OB_SUCC(ret)) {
    if (IS_PX_COORD(parent->type_) || IS_PX_TRANSMIT(parent->type_)) {
      dfo_root_op = parent;
      break;
    } else {
      parent = parent->get_parent();
    }
  }
  ObDfo *dfo = nullptr;
  bool find = false;

  ObSEArray<ObDfo *, 16> dfo_queue;
  int64_t cur_que_front = 0;
  if (OB_FAIL(dfo_queue.push_back(root_dfo))) {
    LOG_WARN("failed to push back");
  }

  while (cur_que_front < dfo_queue.count() && !find && OB_SUCC(ret)) {
    int64_t cur_que_size = dfo_queue.count() - cur_que_front;
    for (int64_t i = 0; i < cur_que_size && OB_SUCC(ret); ++i) {
      dfo = dfo_queue.at(cur_que_front);
      if (dfo->get_root_op_spec() == dfo_root_op) {
        op_dfo = dfo;
        find = true;
        break;
      } else {
        // push child into the queue
        for (int64_t child_idx = 0; OB_SUCC(ret) && child_idx < dfo->get_child_count(); ++child_idx) {
          if (OB_FAIL(dfo_queue.push_back(dfo->get_child_dfos().at(child_idx)))) {
            LOG_WARN("failed to push back child dfo");
          }
        }
      }
      if (OB_SUCC(ret)) {
        cur_que_front++;
      }
    }
  }
  return ret;
}

int ObPxSqcDistributionUtil::check_slave_mapping_location_constraint(ObDfo &child, ObDfo &parent)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(child.get_sqcs_count() != parent.get_sqcs_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sqc count not match for slave_mapping", K(child.get_dfo_id()), K(parent.get_dfo_id()));
  } else if (OB_UNLIKELY(child.get_sqcs_count() > 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expected at most one local sqc", K(ret), K(child.get_sqcs_count()));
  }
  return ret;
}
