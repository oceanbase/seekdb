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

#define USING_LOG_PREFIX SQL_PC
#include "ob_dist_plans.h"
#include "sql/plan_cache/ob_plan_cache_value.h"
#include "sql/plan_cache/ob_plan_match_helper.h"
#include "sql/engine/ob_exec_context.h"
using namespace oceanbase::share;

namespace oceanbase {
namespace sql {
int ObDistPlans::init(ObSqlPlanSet *ps)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ps)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    plan_set_ = ps;
  }
  return ret;
}
// For copying tables, the copy index will be modified during the check for matching process
int ObDistPlans::get_plan(ObPlanCacheCtx &pc_ctx,
                          ObPhysicalPlan *&plan)
{
  int ret = OB_SUCCESS;
  plan = NULL;
  bool is_matched = false;

  LOG_DEBUG("Get Plan", K(dist_plans_.count()));
  //need to clear all location info before calculate candi tablet locations
  //because get_phy_locations will build the related_tablet_map in ObDASCtx
  //and add candi table location into DASCtx
  if (!pc_ctx.try_get_plan_) {
    DAS_CTX(pc_ctx.exec_ctx_).clear_all_location_info();
  }
  if (OB_ISNULL(plan_set_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid null plan_set_", K(ret));
  } else if ((plan_set_->is_multi_stmt_plan())) {
    // If it is a multi stmt plan, it must be a single-table distributed plan, and the physical partition address can be directly calculated based on table location
    // single table should just return plan, do not match
    if (0 == dist_plans_.count()) {
      ret = OB_SQL_PC_NOT_EXIST;
      LOG_DEBUG("dist plan list is empty", K(ret), K(dist_plans_.count()));
    } else if (OB_ISNULL(dist_plans_.at(0))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get an unexpected null plan", K(ret), K(dist_plans_.at(0)));
    } else if (is_plan_available(*dist_plans_.at(0), pc_ctx)) {
      dist_plans_.at(0)->set_dynamic_ref_handle(pc_ctx.handle_id_);
      plan = dist_plans_.at(0);
      is_matched = true;

      // fill table location for single plan using px
      // for single dist plan without px, we already fill the phy locations while calculating plan type
      // for multi table px plan, physical location is calculated in match step
      ObArray<ObCandiTableLoc> candi_table_locs;
      if (OB_ISNULL(plan_set_)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid null plan set", K(ret), K(plan_set_));
      } else if (!plan_set_->enable_inner_part_parallel()) {
        // do nothing
      } else if (OB_FAIL(ObPhyLocationGetter::get_phy_locations(plan->get_table_locations(),
                                                                pc_ctx,
                                                                candi_table_locs))) {
        LOG_WARN("failed to get physical table locations", K(ret));
      } else if (candi_table_locs.empty()) {
        // do nothing.
      } else if (!pc_ctx.try_get_plan_
                 && OB_FAIL(ObPhyLocationGetter::build_table_locs(
                      pc_ctx.exec_ctx_.get_das_ctx(), plan->get_table_locations(),
                      candi_table_locs))) {
        LOG_WARN("fail to init table locs", K(ret));
      }
    }
  }

  ObPlanMatchHelper helper(plan_set_);
  ObArray<ObCandiTableLoc> phy_tbl_infos;
  ObArray<ObTableLocation> out_tbl_locations;
  for (int64_t i = 0; OB_SUCC(ret) && !is_matched && i < dist_plans_.count();
       i++) {
    ObPhysicalPlan *tmp_plan = dist_plans_.at(i);
    phy_tbl_infos.reuse();
    out_tbl_locations.reuse();
    if (OB_ISNULL(tmp_plan)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(tmp_plan));
    } else if (OB_FAIL(helper.match_plan(pc_ctx, tmp_plan, is_matched, phy_tbl_infos, out_tbl_locations))) {
      LOG_WARN("fail to match dist plan", K(ret));
    } else if (is_matched) {
      if (!is_plan_available(*tmp_plan, pc_ctx)) {
        is_matched = false;
      } else {
        tmp_plan->set_dynamic_ref_handle(pc_ctx.handle_id_);
        plan = tmp_plan;
        if (!pc_ctx.try_get_plan_
            && OB_FAIL(ObPhyLocationGetter::build_table_locs(DAS_CTX(pc_ctx.exec_ctx_),
                                                             out_tbl_locations, phy_tbl_infos))) {
          LOG_WARN("fail to init table locs", K(ret));
        }
      }
    }
  }

  if (OB_SUCC(ret) && plan == NULL) {
    ret = OB_SQL_PC_NOT_EXIST;
  }

  return ret;
}


int ObDistPlans::add_plan(ObPhysicalPlan &plan,
                          ObPlanCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  bool is_matched = false;
  ObPlanMatchHelper helper(plan_set_);
  ObArray<ObCandiTableLoc> phy_tbl_infos;
  ObArray<ObTableLocation> out_tbl_locations;

  for (int64_t i = 0; OB_SUCC(ret) && !is_matched && i < dist_plans_.count(); i++) {
    // Check if another thread has successfully added this plan
    phy_tbl_infos.reuse();
    out_tbl_locations.reuse();
    const ObPhysicalPlan *tmp_plan = dist_plans_.at(i);
    if (OB_ISNULL(tmp_plan)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(tmp_plan));
    } else if (OB_FAIL(helper.match_plan(pc_ctx, tmp_plan, is_matched, phy_tbl_infos, out_tbl_locations))) {
      LOG_WARN("fail to match dist plan", K(ret));
    } else {
      is_matched = is_matched && tmp_plan->has_same_location_constraints(plan)
                   && is_same_plan(plan, *tmp_plan, pc_ctx);
    }

    if (!is_matched) {
      // do nothing
    } else {
      ret = OB_SQL_PC_PLAN_DUPLICATE;
    }
  }

  if (OB_SUCC(ret) && !is_matched) {
    if (OB_FAIL(dist_plans_.push_back(&plan))) {
      LOG_WARN("fail to add plan", K(ret));
    }
  }

  return ret;
}

bool ObDistPlans::is_plan_available(const ObPhysicalPlan &plan, ObPlanCacheCtx &pc_ctx) const
{
  bool can_use = true;
  if (pc_ctx.try_get_plan_) {
    if (pc_ctx.compare_plan_->get_plan_hash_value() != plan.get_plan_hash_value()) {
      can_use = false;
    }
  } else if (pc_ctx.enable_adaptive_plan_cache_) {
    if (!plan.is_active_status()) {
      can_use = false;
      pc_ctx.has_inactive_plan_ = true;
    } else {
      pc_ctx.has_inactive_plan_ = false;
    }
  }
  return can_use;
}

bool ObDistPlans::is_same_plan(const ObPhysicalPlan &plan, const ObPhysicalPlan &compare_plan,
                               ObPlanCacheCtx &pc_ctx) const
{
  bool is_same = true;
  if (pc_ctx.add_with_compare_
      && plan.get_plan_hash_value() != compare_plan.get_plan_hash_value()) {
    is_same = false;
  }
  return is_same;
}
// Delete all plan and corresponding plan stat
int ObDistPlans::remove_all_plan()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  for (int64_t i = 0; i < dist_plans_.count(); i++) {
    if (OB_ISNULL(dist_plans_.at(i))) {
      tmp_ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get un expected null in dist_plans",
               K(tmp_ret), K(dist_plans_.at(i)));
    } else {
      dist_plans_.at(i) = NULL;
    }
  }

  dist_plans_.reset();
  ret = tmp_ret;

  return ret;
}
// Get all plan memory usage
int64_t ObDistPlans::get_mem_size() const
{
  int64_t plan_set_mem = 0;
  for (int64_t i = 0; i < dist_plans_.count(); i++) {
    if (OB_ISNULL(dist_plans_.at(i))) {
      BACKTRACE_RET(ERROR, OB_ERR_UNEXPECTED, true, "null physical plan");
    } else {
      plan_set_mem += dist_plans_.at(i)->get_mem_size();
    }
  }
  return plan_set_mem;
}


} // namespace sql
} // namespace oceanbase
