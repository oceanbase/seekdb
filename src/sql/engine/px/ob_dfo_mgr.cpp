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

#include "ob_dfo_mgr.h"
#include "sql/engine/basic/ob_temp_table_access_op.h"
#include "sql/engine/basic/ob_temp_table_access_vec_op.h"
#include "sql/engine/basic/ob_material_op.h"
#include "sql/engine/join/ob_join_filter_op.h"
#include "src/sql/engine/px/exchange/ob_px_transmit_op.h"
#include "sql/engine/px/ob_px_coord_op.h"
#include "sql/engine/basic/ob_material_vec_op.h"
#include "sql/engine/basic/ob_select_into_op.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

int ObDfoSchedOrderGenerator::generate_sched_order(ObDfoMgr &dfo_mgr)
{
   int ret = OB_SUCCESS;
   ObDfo *dfo_tree = dfo_mgr.get_root_dfo();
   if (OB_ISNULL(dfo_tree)) {
     ret = OB_ERR_UNEXPECTED;
     LOG_WARN("NULL unexpected", K(ret));
   } else if (OB_FAIL(DfoTreeNormalizer<ObDfo>::normalize(*dfo_tree))) {
     LOG_WARN("fail normalize dfo tree", K(ret));
   } else if (OB_FAIL(do_generate_sched_order(dfo_mgr, *dfo_tree))) {
     LOG_WARN("fail generate dfo edges", K(ret));
   }
   return ret;
}
// Normalized dfo_tree post-order traversal sequence, i.e., the scheduling order
// Use edge array to represent this order
int ObDfoSchedOrderGenerator::do_generate_sched_order(ObDfoMgr &dfo_mgr, ObDfo &root)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < root.get_child_count(); ++i) {
    ObDfo *child = NULL;
    if (OB_FAIL(root.get_child_dfo(i, child))) {
      LOG_WARN("fail get child dfo", K(i), K(root), K(ret));
    } else if (OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(do_generate_sched_order(dfo_mgr, *child))) {
      LOG_WARN("fail do generate edge", K(*child), K(ret));
    } else if (OB_FAIL(dfo_mgr.add_dfo_edge(child))) {
      LOG_WARN("fail add edge to array", K(ret));
    }
  }
  return ret;
}

int ObDfoSchedDepthGenerator::generate_sched_depth(ObExecContext &exec_ctx,
                                                   ObDfoMgr &dfo_mgr)
{
  int ret = OB_SUCCESS;
  if (GCONF._px_max_pipeline_depth > 2) {
    ObDfo *dfo_tree = dfo_mgr.get_root_dfo();
    if (OB_ISNULL(dfo_tree)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("NULL unexpected", K(ret));
    } else if (OB_FAIL(do_generate_sched_depth(exec_ctx, dfo_mgr, *dfo_tree))) {
      LOG_WARN("fail generate dfo edges", K(ret));
    }
  }
  return ret;
}
// dfo_tree post-order traversal, determine which dfo can do material op bypass
int ObDfoSchedDepthGenerator::do_generate_sched_depth(ObExecContext &exec_ctx,
                                                      ObDfoMgr &dfo_mgr,
                                                      ObDfo &parent)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < parent.get_child_count(); ++i) {
    ObDfo *child = NULL;
    if (OB_FAIL(parent.get_child_dfo(i, child))) {
      LOG_WARN("fail get child dfo", K(i), K(parent), K(ret));
    } else if (OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(do_generate_sched_depth(exec_ctx, dfo_mgr, *child))) {
      LOG_WARN("fail do generate edge", K(*child), K(ret));
    } else {
      bool need_earlier_sched = check_if_need_do_earlier_sched(*child);
      if (need_earlier_sched) {
        // child inside the material has been transformed into bypass, so parent must be scheduled in advance
        // At the same time, if there is also material in parent, it must be marked as block, cannot bypass. Otherwise, it will hang.
        if (OB_FAIL(try_set_dfo_unblock(exec_ctx, *child))) {
          // Since parent was scheduled in advance, parent must block
          // Otherwise parent outputs data, it will get stuck
          LOG_WARN("fail set dfo block", K(ret), K(*child), K(parent));
        } else if (OB_FAIL(try_set_dfo_block(exec_ctx, parent))) {
          // Since parent was scheduled in advance, parent must block
          // Otherwise parent outputs data, it will get stuck
          LOG_WARN("fail set dfo block", K(ret), K(*child), K(parent));
        } else {
          parent.set_earlier_sched(true);
          LOG_DEBUG("parent dfo can do earlier scheduling", K(*child), K(parent));
        }
      }
    }
  }
  return ret;
}

bool ObDfoSchedDepthGenerator::check_if_need_do_earlier_sched(ObDfo &child)
{
  bool do_earlier_sched = false;
  if (child.is_earlier_sched() == false) {
    const ObOpSpec *phy_op = child.get_root_op_spec();
    if (OB_NOT_NULL(phy_op) && IS_PX_TRANSMIT(phy_op->type_)) {
      phy_op = static_cast<const ObTransmitSpec *>(phy_op)->get_child();
      do_earlier_sched = phy_op && (PHY_MATERIAL == phy_op->type_ || PHY_MATERIAL == phy_op->type_);
    }
  } else {
    // dfo (child) is earlier sched, then we can know that dfo's material will block data output.
    // At this time, there is no need to preemptively schedule dfo's parent because there is no data for it to consume. parent relies
    // Later 2-DFO normal scheduling will suffice.
  }
  return do_earlier_sched;
}

int ObDfoSchedDepthGenerator::try_set_dfo_unblock(ObExecContext &exec_ctx, ObDfo &dfo)
{
  return try_set_dfo_block(exec_ctx, dfo, false/*unblock*/);
}

int ObDfoSchedDepthGenerator::try_set_dfo_block(ObExecContext &exec_ctx, ObDfo &dfo, bool block)
{
  int ret = OB_SUCCESS;
  const ObOpSpec *phy_op = dfo.get_root_op_spec();
  if (OB_ISNULL(phy_op)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("phy_op is null", K(ret));
  } else {
    const ObTransmitSpec *transmit = static_cast<const ObTransmitSpec *>(phy_op);
    const ObOpSpec *child = transmit->get_child();
    if (OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("phy_op is null", K(ret));
    } else if (PHY_MATERIAL == child->type_) {
      const ObMaterialSpec *mat = static_cast<const ObMaterialSpec *>(child);
      ObOperatorKit *kit = exec_ctx.get_operator_kit(mat->id_);
      if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("operator is NULL", K(ret), KP(kit));
      } else {
        ObMaterialOpInput *mat_input = static_cast<ObMaterialOpInput *>(kit->input_);
        mat_input->set_bypass(!block); // so that this dfo will have a blocked material op
      }
    } else if (PHY_VEC_MATERIAL == child->type_) {
      const ObMaterialVecSpec *mat = static_cast<const ObMaterialVecSpec *>(child);
      ObOperatorKit *kit = exec_ctx.get_operator_kit(mat->id_);
      if (OB_ISNULL(kit) || OB_ISNULL(kit->input_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("operator is NULL", K(ret), KP(kit));
      } else {
        ObMaterialVecOpInput *mat_input = static_cast<ObMaterialVecOpInput *>(kit->input_);
        mat_input->set_bypass(!block); // so that this dfo will have a blocked material op
      }
    }
  }
  return ret;
}

int ObDfoWorkerAssignment::calc_admited_worker_count(const ObIArray<ObDfo*> &dfos,
                                                     ObExecContext &exec_ctx,
                                                     const ObOpSpec &root_op_spec,
                                                     int64_t &px_expected,
                                                     int64_t &px_minimal,
                                                     int64_t &px_admited)
{
  int ret = OB_SUCCESS;
  px_admited = 0;
  const ObTaskExecutorCtx *task_exec_ctx = NULL;
  if (OB_ISNULL(task_exec_ctx = GET_TASK_EXECUTOR_CTX(exec_ctx))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task exec ctx NULL", K(ret));
  } else if (OB_FAIL(ObDfoWorkerAssignment::get_dfos_worker_count(dfos, true, px_minimal))) {
    LOG_WARN("failed to get dfos worker count", K(ret));
  } else {
    // px level, indicates the number calculated by the optimizer, the current px theoretically requires how many threads
    px_expected = static_cast<const ObPxCoordSpec*>(&root_op_spec)->get_expected_worker_count();
    // query level, indicates the number calculated by the optimizer
    const int64_t query_expected = task_exec_ctx->get_expected_worker_cnt();
    // query level, indicates the minimum number required for scheduling
    const int64_t query_minimal = task_exec_ctx->get_minimal_worker_cnt();
    // query level, indicates the actual number of admissions allocated
    const int64_t query_admited = task_exec_ctx->get_admited_worker_cnt();
    if (query_expected > 0 && 0 >= query_admited) {
      ret = OB_ERR_INSUFFICIENT_PX_WORKER;
      ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(dop_, px_expected);
      ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(required_px_workers_number_, query_expected);
      ACTIVE_SESSION_RETRY_DIAG_INFO_SETTER(admitted_px_workers_number_, query_admited);
      LOG_WARN("not enough thread resource", K(ret), K(px_expected), K(query_admited), K(query_expected));
    } else if (0 == query_expected) {
      // note: For single table, dop=1 queries, it will take the fast dfo path, at this time query_expected = 0
      px_admited = 0;
    } else if (query_admited >= query_expected) {
      px_admited = px_expected;
    } else if (OB_UNLIKELY(query_minimal <= 0)) {
      // compatible with version before 4.2
      px_admited = static_cast<int64_t>((double) query_admited * (double)px_expected / (double) query_expected);
    } else if (OB_UNLIKELY(query_admited < query_minimal || query_expected <= query_minimal)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected query admited worker count", K(ret), K(query_minimal), K(query_admited), K(query_expected));
    } else {
      const int64_t extra_worker = query_admited - query_minimal;
      const int64_t extra_expected = px_expected - px_minimal;
      px_admited = px_minimal + extra_expected * extra_worker / (query_expected - query_minimal);
    }
    LOG_TRACE("calc px worker count", K(query_expected), K(query_minimal), K(query_admited),
                            K(px_expected), K(px_minimal), K(px_admited));
  }
  return ret;
}

int ObDfoWorkerAssignment::assign_worker(ObDfoMgr &dfo_mgr,
                                         int64_t expected_worker_count,
                                         int64_t minimal_worker_count,
                                         int64_t admited_worker_count,
                                         bool use_adaptive_px_dop)
{
  int ret = OB_SUCCESS;
  /*  algorithm: */
  /*  1. Based on the dop provided by the optimizer, assign workers to each dfo */
  /*  2. The ideal situation is dop = number of workers */
  /*  3. However, if the number of workers is insufficient, then a downgrade is needed. Downgrade strategy: each DFO uses fewer workers in a geometric progression */
  /*  4. To determine the ratio, we need to find the group of dop that occupies the most workers when scheduled simultaneously, using it as the benchmark */
  /*     Calculate the ratio, to ensure that all other dfo can get enough workers */

  /*  Points for algorithm improvement (TODO): */
  /*  Consider expected_worker_count > admitted_worker_count scenario, */
  /*  This algorithm calculates a scale rate < 1, which will result in each dfo doing a dop downgrade */
  /*  Actually, it is possible to allow some dfo execution without degradation. Consider the following scenario: */
  /*  */
  /*          dfo5 */
  /*         /   \ */
  /*       dfo1  dfo4 */
  /*               \ */
  /*                dfo3 */
  /*                 \ */
  /*                 dfo2 */
  /*  */
  /*  Assume dop = 5, then expected_worker_count = 3 * 5 = 15 */
  /*  */
  /*  Consider the scenario of insufficient threads, set admited_worker_count = 10, */
  /*  Then, in the optimal case of the algorithm, we can allocate as follows: */
  /*  */
  /*          dfo5 (3 threads) */
  /*         /   \ */
  /*   (3)dfo1  dfo4 (4) */
  /*               \ */
  /*                dfo3 (5) */
  /*                 \ */
  /*                 dfo2 (5) */
  /*  */
  /*  The current implementation, due to dop being reduced proportionally, the reduced dop = 5 * 10 / 15 = 3, the actual allocation result is: */
  /*  */
  /*          dfo5 (3) */
  /*         /   \ */
  /*   (3)dfo1  dfo4 (3) */
  /*               \ */
  /*                dfo3 (3) */
  /*                 \ */
  /*                 dfo2 (3) */
  /*  */
  /*  Obviously, the current implementation is not the most efficient in terms of CPU resource utilization. This part of the work can be refined later. For now, it will suffice as is */

  const ObIArray<ObDfo *> & dfos = dfo_mgr.get_all_dfos();
  // Based on the dop provided by the optimizer, assign workers to each dfo
  // The actual number of allocated workers is definitely no greater than dop, but may be less than the given dop value
  // admited_worker_count in rpc as worker scenario, the value is 0.
  double scale_rate = 1.0;
  bool match_expected = false;
  bool compatible_before_420 = false;
  if (OB_UNLIKELY(admited_worker_count < 0 || expected_worker_count <= 0 || minimal_worker_count <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("should have at least one worker",  K(ret), K(admited_worker_count),
                                        K(expected_worker_count), K(minimal_worker_count));
  } else if (admited_worker_count >= expected_worker_count) {
    match_expected = true;
  } else if (minimal_worker_count <= 0) {
    // compatible with version before 4.2
    compatible_before_420 = true;
    scale_rate = static_cast<double>(admited_worker_count) / static_cast<double>(expected_worker_count);
  } else if (0 >= admited_worker_count) {
    scale_rate = 1.0;
  } else if (minimal_worker_count == admited_worker_count) {
    scale_rate = 0.0;
  } else if (OB_UNLIKELY(minimal_worker_count > admited_worker_count
                         || minimal_worker_count >= expected_worker_count)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected params", K(ret), K(minimal_worker_count), K(admited_worker_count), K(expected_worker_count));
  } else {
    scale_rate = static_cast<double>(admited_worker_count - minimal_worker_count)
                 / static_cast<double>(expected_worker_count - minimal_worker_count);
  }
  ARRAY_FOREACH_X(dfos, idx, cnt, OB_SUCC(ret)) {
    ObDfo *child = dfos.at(idx);
    int64_t val = 0;
    if (match_expected) {
      val = child->get_dop();
    } else if (compatible_before_420) {
      val = std::max(1L, static_cast<int64_t>(static_cast<double>(child->get_dop()) * scale_rate));
    } else {
      val = 1L + static_cast<int64_t>(std::max(static_cast<double>(child->get_dop() - 1), 0.0) * scale_rate);
    }
    child->set_assigned_worker_count(val);
    if (child->is_single() && val > 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("local dfo do should not have more than 1", K(*child), K(val), K(ret));
    }
    LOG_TRACE("assign worker count to dfo",
              "dfo_id", child->get_dfo_id(), K(admited_worker_count),
              K(expected_worker_count), K(minimal_worker_count),
              "dop", child->get_dop(), K(scale_rate), K(val));
  }
  // Because the max was taken above, the actually assigned may exceed the admission number, at which point an error should be reported
  int64_t total_assigned = 0;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(get_dfos_worker_count(dfos, false, total_assigned))) {
    LOG_WARN("failed to get dfos worker count", K(ret));
  } else if (!use_adaptive_px_dop && total_assigned > admited_worker_count
             && admited_worker_count != 0) {
    // means that some dfo theoretically cannot be assigned to any thread
    ret = OB_ERR_PARALLEL_SERVERS_TARGET_NOT_ENOUGH;
    LOG_USER_ERROR(OB_ERR_PARALLEL_SERVERS_TARGET_NOT_ENOUGH, total_assigned);
    LOG_WARN("total assigned worker to dfos is more than admited_worker_count",
             K(total_assigned),
             K(admited_worker_count),
             K(minimal_worker_count),
             K(expected_worker_count),
             K(ret));
  }
  return ret;
}

int ObDfoWorkerAssignment::get_dfos_worker_count(const ObIArray<ObDfo*> &dfos,
                                                 const bool get_minimal,
                                                 int64_t &total_assigned)
{
  int ret = OB_SUCCESS;
  total_assigned = 0;
  ARRAY_FOREACH_X(dfos, idx, cnt, OB_SUCC(ret)) {
    const ObDfo *child  = dfos.at(idx);
    const ObDfo *parent = child->parent();
    // Calculate the number of threads consumed when scheduling the current dfo and its "children" together
    // Find the group with the largest expected worker cnt value
    if (OB_ISNULL(parent) || OB_ISNULL(child)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dfo edges expect to have parent", KPC(parent), KPC(child), K(ret));
    } else {
      int64_t child_assigned = get_minimal ? 1 : child->get_assigned_worker_count();
      int64_t parent_assigned = get_minimal ? 1 : parent->get_assigned_worker_count();
      int64_t assigned = parent_assigned + child_assigned;
      // Local right-deep tree scenario, depend_sibling and current child dfo will both be scheduled
      /* Why need extra flag has_depend_sibling_? Why not use NULL != depend_sibling_?
       *               dfo5 (dop=2)
       *         /      |      \
       *      dfo1(2) dfo2 (4) dfo4 (1)
       *                        |
       *                      dfo3 (2)
       * Schedule order: (4,3) => (4,5) => (4,5,1) => (4,5,2) => (4,5) => (5). max_dop = (4,5,2) = 1 + 2 + 4 = 7.
       * Depend sibling: dfo4 -> dfo1 -> dfo2.
       * Depend sibling is stored as a list.
       * We thought dfo2 is depend sibling of dfo1, and calculated incorrect max_dop = (1,2,5) = 2 + 4 + 2 = 8.
       * Actually, dfo1 and dfo2 are depend sibling of dfo4, but dfo2 is not depend sibling of dfo1.
       * So we use has_depend_sibling_ record whether the dfo is the header of list.
      */
      int64_t max_depend_sibling_assigned_worker = 0;
      if (child->has_depend_sibling()) {
        while (NULL != child->depend_sibling()) {
          child = child->depend_sibling();
          child_assigned = get_minimal ? 1 : child->get_assigned_worker_count();
          if (max_depend_sibling_assigned_worker < child_assigned) {
            max_depend_sibling_assigned_worker = child_assigned;
          }
        }
      }
      assigned += max_depend_sibling_assigned_worker;
      if (assigned > total_assigned) {
        total_assigned = assigned;
        LOG_TRACE("update total assigned", K(idx), K(get_minimal), K(parent_assigned),
                K(child_assigned), K(max_depend_sibling_assigned_worker), K(total_assigned));
      }
    }
  }
  return ret;
}

void ObDfoMgr::destroy()
{
  // release all dfos
  for (int64_t i = 0; i < edges_.count(); ++i) {
    ObDfo *dfo = edges_.at(i);
    ObDfo::reset_resource(dfo);
  }
  edges_.reset();
  // release root dfo
  ObDfo::reset_resource(root_dfo_);
  root_dfo_ = nullptr;
  inited_ = false;
}

int ObDfoMgr::init(ObExecContext &exec_ctx,
                   const ObOpSpec &root_op_spec,
                   const ObDfoInterruptIdGen &dfo_int_gen,
                   ObPxCoordInfo &px_coord_info)
{
  int ret = OB_SUCCESS;
  root_dfo_ = NULL;
  ObDfo *rpc_dfo = nullptr;
  int64_t px_expected = 0;
  int64_t px_minimal = 0;
  int64_t px_admited = 0;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("dfo mgr init twice", K(ret));
  } else if (OB_FAIL(do_split(exec_ctx, allocator_, &root_op_spec, root_dfo_, dfo_int_gen, px_coord_info))) {
    LOG_WARN("fail split ops into dfo", K(ret));
  } else if (OB_ISNULL(root_dfo_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL dfo unexpected", K(ret));
  } else if (!px_coord_info.rf_dpd_info_.is_empty()
      && OB_FAIL(px_coord_info.rf_dpd_info_.describe_dependency(root_dfo_))) {
    LOG_WARN("failed to describe rf dependency");
  } else if (OB_FAIL(ObDfoSchedOrderGenerator::generate_sched_order(*this))) {
    LOG_WARN("fail init dfo mgr", K(ret));
  } else if (OB_FAIL(ObDfoSchedDepthGenerator::generate_sched_depth(exec_ctx, *this))) {
    LOG_WARN("fail init dfo mgr", K(ret));
  } else if (OB_FAIL(ObDfoWorkerAssignment::calc_admited_worker_count(get_all_dfos(),
                                                                      exec_ctx,
                                                                      root_op_spec,
                                                                      px_expected,
                                                                      px_minimal,
                                                                      px_admited))) {
    LOG_WARN("fail to calc admited worler count", K(ret));
  } else if (OB_FAIL(ObDfoWorkerAssignment::assign_worker(
               *this, px_expected, px_minimal, px_admited, exec_ctx.is_use_adaptive_px_dop()))) {
    LOG_WARN("fail assign worker to dfos", K(ret), K(px_expected), K(px_minimal), K(px_admited));
  } else {
    inited_ = true;
  }
  return ret;
}
// parent_dfo as input and output parameter, it is only used as an output parameter when the first op is coord, otherwise it is always used as an input parameter
int ObDfoMgr::do_split(ObExecContext &exec_ctx,
                       ObIAllocator &allocator,
                       const ObOpSpec *phy_op,
                       ObDfo *&parent_dfo,
                       const ObDfoInterruptIdGen &dfo_int_gen,
                       ObPxCoordInfo &px_coord_info) const
{
  int ret = OB_SUCCESS;
  bool partition_random_affinitize = true;
  bool top_px = (nullptr == parent_dfo);
  bool got_fulltree_dfo = false;
  ObDfo *dfo = NULL;
  bool is_stack_overflow = false;
  if (OB_NOT_NULL(phy_op->get_phy_plan())) {
    partition_random_affinitize = false;
  }
  if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
    LOG_WARN("failed to check stack overflow", K(ret));
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("stack overflow, maybe too deep recursive", K(ret));
  } else if (OB_ISNULL(phy_op)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL unexpected", K(ret));
  } else if (NULL == parent_dfo && !IS_PX_COORD(phy_op->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the first phy_op must be a coord op", K(ret), K(phy_op->type_));
  } else if (phy_op->is_table_scan() && NULL != parent_dfo) {
    if (static_cast<const ObTableScanSpec*>(phy_op)->use_dist_das()) {
      parent_dfo->set_das(true);
    } else {
      parent_dfo->set_scan(true);
    }
    parent_dfo->inc_tsc_op_cnt();
    auto tsc_op = static_cast<const ObTableScanSpec *>(phy_op);
    if (TableAccessType::HAS_USER_TABLE == px_coord_info.table_access_type_){
      // nop
    } else if (!is_virtual_table(tsc_op->get_ref_table_id())) {
      px_coord_info.table_access_type_ = TableAccessType::HAS_USER_TABLE;
    } else {
      px_coord_info.table_access_type_ = TableAccessType::PURE_VIRTUAL_TABLE;
    }
    if (parent_dfo->need_p2p_info_ && parent_dfo->get_p2p_dh_addrs().empty()) {
      ObDASTableLoc *table_loc = nullptr;
       if (OB_ISNULL(table_loc = DAS_CTX(exec_ctx).get_table_loc_by_id(
            tsc_op->get_table_loc_id(), tsc_op->get_loc_ref_table_id()))) {
         OZ(ObTableLocation::get_full_leader_table_loc(DAS_CTX(exec_ctx).get_location_router(),
                                                       exec_ctx.get_allocator(),
                                                       exec_ctx.get_my_session()->get_effective_tenant_id(),
                                                       tsc_op->get_table_loc_id(),
                                                       tsc_op->get_loc_ref_table_id(),
                                                       table_loc));
      }
      if (OB_FAIL(ret)) {
      } else {
        const DASTabletLocList &locations = table_loc->get_tablet_locs();
        parent_dfo->set_p2p_dh_loc(table_loc);
        if (OB_FAIL(get_location_addrs<DASTabletLocList>(locations,
            parent_dfo->get_p2p_dh_addrs()))) {
          LOG_WARN("fail get location addrs", K(ret));
        }
      }
    }
  } else if (phy_op->is_dml_operator() && NULL != parent_dfo) {
    // The current op is a dml operator, need to set the attributes of dfo
    if (ObPXServerAddrUtil::check_build_dfo_with_dml(*phy_op)) {
      parent_dfo->set_dml_op(true);
    }
    const ObPhyOperatorType op_type = phy_op->get_type();
    LOG_TRACE("set DFO need_branch_id", K(op_type));
    parent_dfo->set_need_branch_id_op(op_type == PHY_INSERT_ON_DUP
                                      || op_type == PHY_REPLACE
                                      || op_type == PHY_LOCK);
  } else if (phy_op->get_type() == PHY_TEMP_TABLE_ACCESS && NULL != parent_dfo) {
    parent_dfo->set_temp_table_scan(true);
    const ObTempTableAccessOpSpec *access = static_cast<const ObTempTableAccessOpSpec*>(phy_op);
    parent_dfo->set_temp_table_id(access->get_table_id());
    if (parent_dfo->need_p2p_info_ && parent_dfo->get_p2p_dh_addrs().empty()) {
      OZ(px_coord_info.p2p_temp_table_info_.temp_access_ops_.push_back(phy_op));
      OZ(px_coord_info.p2p_temp_table_info_.dfos_.push_back(parent_dfo));
    }
  } else if (phy_op->get_type() == PHY_VEC_TEMP_TABLE_ACCESS && NULL != parent_dfo) {
    parent_dfo->set_temp_table_scan(true);
    const ObTempTableAccessVecOpSpec *access = static_cast<const ObTempTableAccessVecOpSpec*>(phy_op);
    parent_dfo->set_temp_table_id(access->get_table_id());
    if (parent_dfo->need_p2p_info_ && parent_dfo->get_p2p_dh_addrs().empty()) {
      OZ(px_coord_info.p2p_temp_table_info_.temp_access_ops_.push_back(phy_op));
      OZ(px_coord_info.p2p_temp_table_info_.dfos_.push_back(parent_dfo));
    }
  } else if (phy_op->get_type() == PHY_SELECT_INTO && NULL != parent_dfo) {
    // odps only supports parallelism on one machine can only have one sqc
    const ObSelectIntoSpec *select_into_spec = static_cast<const ObSelectIntoSpec*>(phy_op);
    ObExternalFileFormat external_properties;
    if (!select_into_spec->external_properties_.str_.empty()) {
      if (OB_FAIL(external_properties.load_from_string(select_into_spec->external_properties_.str_,
                                                       allocator))) {
        LOG_WARN("failed to load external properties", K(ret));
      } else if (ObExternalFileFormat::FormatType::ODPS_FORMAT == external_properties.format_type_) {
        parent_dfo->set_into_odps(true);
      }
    }
  } else if (IS_PX_GI(phy_op->get_type()) && NULL != parent_dfo) {
    const ObGranuleIteratorSpec *gi_spec =
        static_cast<const ObGranuleIteratorSpec *>(phy_op);
    if (gi_spec->bf_info_.is_inited_) {
      ObP2PDfoMapNode node;
      node.target_dfo_id_ = parent_dfo->get_dfo_id();
      if (OB_FAIL(px_coord_info.p2p_dfo_map_.set_refactored(
          gi_spec->bf_info_.p2p_dh_id_,
          node))) {
        LOG_WARN("fail to set p2p dh id to map", K(ret));
      } else if (OB_FAIL(px_coord_info.rf_dpd_info_.rf_use_ops_.push_back(phy_op))) {
        LOG_WARN("failed to push back parition filter gi op");
      } else {
        parent_dfo->set_need_p2p_info(true);
      }
    }
  } else if (IS_PX_JOIN_FILTER(phy_op->get_type()) && NULL != parent_dfo) {
    const ObJoinFilterSpec *filter_spec = static_cast<const ObJoinFilterSpec *>(phy_op);
    if (filter_spec->is_create_mode() && OB_FAIL(px_coord_info.rf_dpd_info_.rf_create_ops_.push_back(phy_op))) {
      LOG_WARN("failed to push back create op");
    } else if (filter_spec->is_use_mode() && OB_FAIL(px_coord_info.rf_dpd_info_.rf_use_ops_.push_back(phy_op))) {
      LOG_WARN("failed to push back use op");
    }
    if (OB_SUCC(ret) && filter_spec->is_shared_join_filter() && filter_spec->is_shuffle_) {
      ObP2PDfoMapNode node;
      node.target_dfo_id_ = parent_dfo->get_dfo_id();
      for (int i = 0; i < filter_spec->rf_infos_.count() && OB_SUCC(ret); ++i) {
        if (filter_spec->is_create_mode()) {
          if (OB_FAIL(parent_dfo->add_p2p_dh_ids(
              filter_spec->rf_infos_.at(i).p2p_datahub_id_))) {
            LOG_WARN("fail to add p2p dh ids", K(ret));
          }
        } else if (OB_FAIL(px_coord_info.p2p_dfo_map_.set_refactored(
              filter_spec->rf_infos_.at(i).p2p_datahub_id_,
              node))) {
          LOG_WARN("fail to set p2p dh id to map", K(ret));
        } else {
          parent_dfo->set_need_p2p_info(true);
        }
      }
    }
  } else if (IS_PX_COORD(phy_op->type_)) {
    if (top_px) {
      if (OB_FAIL(create_dfo(allocator, phy_op, dfo))) {
        LOG_WARN("fail create dfo", K(ret));
      }
    } else {
      // Not nested px coord is allocated dfo here
      // For nested px coord, it is only used as a normal operator called by leaf dfo
      // leaf dfo will call its next_row interface, drive nested px coord to start scheduling
      got_fulltree_dfo = true;
    }
  } else if (IS_PX_TRANSMIT(phy_op->type_)) {
    if (OB_FAIL(create_dfo(allocator, phy_op, dfo))) {
      LOG_WARN("fail create dfo", K(ret));
    } else {
      dfo->set_parent(parent_dfo);
      if (NULL != parent_dfo) {
        if (OB_FAIL(parent_dfo->append_child_dfo(dfo))) {
          LOG_WARN("fail append child dfo", K(ret));
        }
      }
    }
  }

  if (OB_SUCC(ret) && nullptr != dfo) {
    if (IS_PX_COORD(phy_op->type_)) {
      dfo->set_coord_info_ptr(&px_coord_info);
      dfo->set_root_dfo(true);
      dfo->set_single(true);
      dfo->set_dop(1);
      dfo->set_execution_id(exec_ctx.get_my_session()->get_current_execution_id());
      dfo->set_px_sequence_id(dfo_int_gen.get_px_sequence_id());
      dfo->set_partition_random_affinitize(partition_random_affinitize);
      dfo->set_query_sql(px_coord_info.coord_.query_sql());
      if (OB_SUCC(ret)) {
        // If there is a nested situation, then dfo may have already been set with some information, so it will not be overwritten here
        if (OB_INVALID_ID == dfo->get_dfo_id()) {
          // Only the top-level dfo's receive does not have the dfo id set, even for nested dfo, it will be set because it will be determined based on transmit
          dfo->set_dfo_id(ObDfo::MAX_DFO_ID);
        }
        if (OB_INVALID_ID == dfo->get_qc_id()) {
          // receive record of px stored on transmit
          const ObTransmitSpec *transmit = static_cast<const ObTransmitSpec *>(phy_op->get_child());
          if (OB_INVALID_ID != transmit->get_px_id()) {
            dfo->set_qc_id(transmit->get_px_id());
          }
        }
        // For root dfo, it is not a real dfo, no id is assigned
        // So use ObDfo::MAX_DFO_ID to represent
        if (OB_FAIL(dfo_int_gen.gen_id(dfo->get_dfo_id(), dfo->get_interrupt_id()))) {
          LOG_WARN("fail gen dfo int id", K(ret));
        }
        LOG_TRACE("cur dfo info", K(dfo->get_qc_id()), K(dfo->get_dfo_id()), K(dfo->get_dop()));
      }
    } else {
      const ObTransmitSpec *transmit = static_cast<const ObTransmitSpec *>(phy_op);
      // If the subtree below transmit contains px coord operator, then the following settings will be
      // Modify to is_local = true, dop = 1
      dfo->set_coord_info_ptr(&px_coord_info);
      dfo->set_single(transmit->is_px_single());
      dfo->set_dop(transmit->is_px_single() ? transmit->get_px_dop() :
                                              get_adaptive_px_dop(*transmit, exec_ctx));
      dfo->set_qc_id(transmit->get_px_id());
      dfo->set_dfo_id(transmit->get_dfo_id());
      dfo->set_execution_id(exec_ctx.get_my_session()->get_current_execution_id());
      dfo->set_px_sequence_id(dfo_int_gen.get_px_sequence_id());
      dfo->set_query_sql(px_coord_info.coord_.query_sql());
      if (OB_SUCC(ret)) {
        dfo->set_dist_method(transmit->dist_method_);
        if (transmit->is_slave_mapping()) {
          dfo->set_out_slave_mapping_type(transmit->get_slave_mapping_type());
          parent_dfo->set_in_slave_mapping_type(transmit->get_slave_mapping_type());
        }
        dfo->set_pkey_table_loc_id(
          (reinterpret_cast<const ObPxTransmitSpec *>(transmit))->repartition_table_id_);
        if (OB_ISNULL(parent_dfo)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("parent dfo should not be null", K(ret));
        } else if (transmit->get_px_dop() <= 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("should have dop set by optimizer", K(ret), K(transmit->get_px_dop()));
        } else if (OB_FAIL(dfo_int_gen.gen_id(transmit->get_dfo_id(),
                                              dfo->get_interrupt_id()))) {
          LOG_WARN("fail gen dfo int id", K(ret));
        } else {
          dfo->set_qc_server_id(GCTX.get_server_index());
          dfo->set_parent_dfo_id(parent_dfo->get_dfo_id());
          LOG_TRACE("cur dfo dop",
                    "dfo_id", dfo->get_dfo_id(),
                    "is_local", transmit->is_px_single(),
                    "dop", transmit->get_px_dop(),
                    K(dfo->get_qc_id()),
                    "parent dfo_id", parent_dfo->get_dfo_id(),
                    "slave mapping", transmit->is_slave_mapping());
        }
      }
    }
  }


  if (nullptr != dfo) {
    parent_dfo = dfo;
  }

  if (OB_SUCC(ret)) {
    if (got_fulltree_dfo) {
      // Serialize the dfo containing nested px coord operator, you need to serialize all its sub dfo below
      // Serialize everything out, that is, include the entire subtree (fulltree)
      if (OB_ISNULL(parent_dfo)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inner px coord op should be in a dfo", K(ret));
      } else {
        parent_dfo->set_fulltree(true);
        parent_dfo->set_single(true);
        parent_dfo->set_dop(1);
      }
      // we have reach to a inner qc operator,
      // no more monkeys jumping on the bed!
    } else {
      for (int32_t i = 0; OB_SUCC(ret) && i < phy_op->get_child_cnt(); ++i) {
        ObDfo *tmp_parent_dfo = parent_dfo;
        if (OB_FAIL(do_split(exec_ctx, allocator, phy_op->get_child(i),
                             tmp_parent_dfo, dfo_int_gen, px_coord_info))) {
          LOG_WARN("fail split op into dfo", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObDfoMgr::create_dfo(ObIAllocator &allocator,
                         const ObOpSpec *dfo_root_op,
                         ObDfo *&dfo) const
{
  int ret = OB_SUCCESS;
  void *tmp = NULL;
  dfo = NULL;
  if (OB_ISNULL(dfo_root_op)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL unexpected", K(ret));
  } else if (OB_ISNULL(tmp = allocator.alloc(sizeof(ObDfo)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to alloc ObDfo", K(ret));
  } else if (OB_ISNULL(dfo = new(tmp) ObDfo(allocator))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to new ObDfo", K(ret));
  } else {
    dfo->set_root_op_spec(dfo_root_op);
    dfo->set_phy_plan(dfo_root_op->get_phy_plan());
  }
  return ret;
}
// get_ready_dfo interface is only used for single-layer dfo scheduling.
// Each iteration outputs a dfo.
int ObDfoMgr::get_ready_dfo(ObDfo *&dfo) const
{
  int ret = OB_SUCCESS;
  bool all_finish = true;
  dfo = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < edges_.count(); ++i) {
    ObDfo *edge = edges_.at(i);
    if (edge->is_thread_finish()) {
      continue;
    } else {
      all_finish = false;
      if (!edge->is_active()) {
        dfo = edge;
        dfo->set_active();
        break;
      }
    }
  }
  if (OB_SUCC(ret) && all_finish) {
    ret = OB_ITER_END;
  }
  return ret;
}
// Note the difference between two return statuses:
//   - If there is an edge that has not finished, and more dfo cannot be scheduled, then return an empty set
//   - If all edge are already finish, then return ITER_END
// Each iteration outputs only one pair of DFO, child & parent
int ObDfoMgr::get_ready_dfos(ObIArray<ObDfo*> &dfos) const
{
  int ret = OB_SUCCESS;
  bool all_finish = true;
  bool got_pair_dfo = false;
  dfos.reset();

  LOG_TRACE("ready dfos", K(edges_.count()));
  // edges have already been sorted by scheduling order, those listed first are scheduled first
  for (int64_t i = 0; OB_SUCC(ret) && i < edges_.count(); ++i) {
    ObDfo *edge = edges_.at(i);
    ObDfo *root_edge = edges_.at(edges_.count() - 1);
    if (edge->is_thread_finish()) {
      LOG_TRACE("finish dfo", K(*edge));
      continue;
    } else {
      // edge not completed, scheduling goal is to facilitate the completion of this edge as soon as possible, including scheduling the DFO it depends on, i.e.:
      //  - edge is not scheduled, schedule immediately
      //  - edge has already been scheduled, then check if this edge depends on other dfo to complete execution
      all_finish = false;
      if (!edge->is_active()) {
        if (OB_FAIL(dfos.push_back(edge))) {
          LOG_WARN("fail push dfo", K(ret));
        } else if (NULL == edge->parent()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("parent is NULL, unexpected", K(ret));
        } else if (OB_FAIL(dfos.push_back(edge->parent()))) {
          LOG_WARN("fail push dfo", K(ret));
        } else {
          edge->set_active();
          got_pair_dfo = true;
        }
      } else if (edge->has_depend_sibling()) {
        // Find the dfo with higher priority in the dependence chain,
        // Require this dfo to be in an unfinished state
        ObDfo *sibling_edge = edge->depend_sibling();
        for (/* nop */;
             nullptr != sibling_edge && sibling_edge->is_thread_finish();
             sibling_edge = sibling_edge->depend_sibling()) {
          // search forward: [leaf] --> [leaf] --> [leaf]
        }
        if (OB_UNLIKELY(nullptr == sibling_edge)) {
          // nop, all sibling finish
        } else if (sibling_edge->is_active()) {
          // nop, wait for a sibling finish.
          // after then can we shedule next edge
        } else if (OB_FAIL(dfos.push_back(sibling_edge))) {
          LOG_WARN("fail push dfo", K(ret));
        } else if (NULL == sibling_edge->parent()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("parent is NULL, unexpected", K(ret));
        } else if (OB_FAIL(dfos.push_back(sibling_edge->parent()))) {
          LOG_WARN("fail push dfo", K(ret));
        } else {
          sibling_edge->set_active();
          got_pair_dfo = true;
          LOG_TRACE("start schedule dfo", K(*sibling_edge), K(*sibling_edge->parent()));
        }
      }else {
        // The current edge has not been completed, and there are no sibling edges to schedule, return an empty dfos, continue waiting
      }
      // Three-layer DFO scheduling logic
      // Note: Even if a sibling has been scheduled above, 3 DFOs have already been scheduled
      // will still attempt to schedule the 4th depend parent dfo
      if (OB_SUCC(ret) && !got_pair_dfo && GCONF._px_max_pipeline_depth > 2) {
        ObDfo *parent_edge = edge->parent();
        if (NULL != parent_edge &&
            !parent_edge->is_active() &&
            NULL != parent_edge->parent() &&
            parent_edge->parent()->is_earlier_sched()) {
          /* For easy description, consider the following scenario:
           *       parent-parent
           *           |
           *         parent
           *           |
           *          edge
           * When the code runs to this branch, edge is already active, edge and parent two dfo have been scheduled
           * and the execution of parent depends on parent-parent being scheduled as well (2+dfo scheduling optimization, hash join result
           * can be directly output without adding a material operator above)
           */
          if (OB_FAIL(dfos.push_back(parent_edge))) {
            LOG_WARN("fail push dfo", K(ret));
          } else if (OB_FAIL(dfos.push_back(parent_edge->parent()))) {
            LOG_WARN("fail push dfo", K(ret));
          } else {
            parent_edge->set_active();
            got_pair_dfo = true;
            LOG_DEBUG("dfo do earlier scheduling", K(*parent_edge->parent()));
          }
        }
      }

      // If one of root_edge's child has scheduled, we try to start the root_dfo.
      if (OB_SUCC(ret) && !got_pair_dfo) {
        if (edge->is_active() &&
            !root_edge->is_active() &&
            edge->has_parent() &&
            edge->parent() == root_edge) {
          // This branch is an optimization, preemptively scheduling the root dfo, so that the root dfo
          // Can timely pull the data below. In some scenarios, it can avoid adding dfo at the lower level
          // Unnecessary material operator blocks data flow
          //
          // The reason this can be done is because the root dfo occupies resources regardless of whether it is scheduled or not,
          // No whitening no adjustment
          if (OB_ISNULL(root_edge->parent()) || root_edge->parent() != root_dfo_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("The root edge is null or it's parent not root dfo", K(ret));
          } else if (OB_FAIL(dfos.push_back(root_edge))) {
            LOG_WARN("Fail to push dfo", K(ret));
          } else if (OB_FAIL(dfos.push_back(root_dfo_))) {
            LOG_WARN("Fail to push dfo", K(ret));
          } else {
            root_edge->set_active();
            got_pair_dfo = true;
            LOG_TRACE("Try to schedule root dfo", KP(root_edge), KP(root_dfo_));
          }
        }
      }
      // Each time only iterate one pair of results and return them out
      //
      // If:
      //   - The current edge has not been completed,
      //   -  there is no sibling edge to schedule,
      //   - No depend parent edge needs scheduling
      //   - root dfo does not need scheduling
      // Then return dfos empty set, continue waiting
      break;
    }
  }
  if (all_finish && OB_SUCCESS == ret) {
    ret = OB_ITER_END;
  }
  return ret;
}

int ObDfoMgr::add_dfo_edge(ObDfo *edge)
{
  int ret = OB_SUCCESS;
  if (edges_.count() >= ObDfo::MAX_DFO_ID) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "plan with more than 128 DFOs");
  } else if (OB_FAIL(edges_.push_back(edge))) {
    LOG_WARN("fail to push back dfo", K(*edge), K(ret));
    // release the memory
    ObDfo::reset_resource(edge);
    edge = nullptr;
  }
  return ret;
}

int ObDfoMgr::find_dfo_edge(int64_t id, ObDfo *&edge)
{
  int ret = OB_SUCCESS;
  if (id < 0 || id >= ObDfo::MAX_DFO_ID || id >= edges_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid dfo id", K(id), K(edges_.count()), K(ret));
  } else {
    bool found = false;
    int64_t cnt = edges_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
      if (id == edges_.at(i)->get_dfo_id()) {
        edge = edges_.at(i);
        found = true;
        break;
      }
    }
    if (!found) {
      ret = OB_ENTRY_NOT_EXIST;
      LOG_WARN("not found dfo", K(id), K(cnt), K(ret));
    }
  }
  return ret;
}

int ObDfoMgr::get_active_dfos(ObIArray<ObDfo*> &dfos) const
{
  int ret = OB_SUCCESS;
  dfos.reset();
  // edges have already been sorted by scheduling order, those listed first are scheduled first
  for (int64_t i = 0; OB_SUCC(ret) && i < edges_.count(); ++i) {
    ObDfo *edge = edges_.at(i);
    // edge not completed, the scheduling goal is to facilitate the completion of this edge as soon as possible
    if (edge->is_active()) {
      if (OB_FAIL(dfos.push_back(edge))) {
        LOG_WARN("fail push back edge", K(ret));
      }
    }
  }
  return ret;
}

int ObDfoMgr::get_scheduled_dfos(ObIArray<ObDfo*> &dfos) const
{
  int ret = OB_SUCCESS;
  dfos.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < edges_.count(); ++i) {
    ObDfo *edge = edges_.at(i);
    // Called the schedule_dfo interface, regardless of success or failure, dfo will be set to scheduled state
    if (edge->is_scheduled()) {
      if (OB_FAIL(dfos.push_back(edge))) {
        LOG_WARN("fail push back edge", K(ret));
      }
    }
  }
  return ret;
}

int ObDfoMgr::get_running_dfos(ObIArray<ObDfo*> &dfos) const
{
  int ret = OB_SUCCESS;
  dfos.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < edges_.count(); ++i) {
    ObDfo *edge = edges_.at(i);
    // Called the schedule_dfo interface, regardless of success or failure, dfo will be set to scheduled state
    if (edge->is_scheduled() && !edge->is_thread_finish()) {
      if (OB_FAIL(dfos.push_back(edge))) {
        LOG_WARN("fail push back edge", K(ret));
      }
    }
  }
  return ret;
}

int64_t ObDfoMgr::get_adaptive_px_dop(const ObTransmitSpec &spec, ObExecContext &exec_ctx) const
{
  int ret = OB_SUCCESS;
  int64_t px_dop = spec.get_px_dop();
  AutoDopHashMap &auto_dop_map = exec_ctx.get_auto_dop_map();
  if (!auto_dop_map.created()) {
    // do nothing
  } else if (OB_FAIL(auto_dop_map.get_refactored(spec.get_id(), px_dop))) {
    LOG_WARN("failed to get refactored", K(ret));
  } else {
    px_dop = px_dop >= 1 ? px_dop : spec.get_px_dop();
  }
  LOG_TRACE("adaptive px dop", K(spec.get_id()), K(px_dop));
  return px_dop;
}
