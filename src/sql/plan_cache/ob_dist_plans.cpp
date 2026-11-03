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
  //need to clear all access info before calculate candi tablet accesses
  //because collect_access_targets will build the related_tablet_map in ObDASCtx
  //and add candi table access into DASCtx
  DAS_CTX(pc_ctx.exec_ctx_).clear_table_accesses();
  if (OB_ISNULL(plan_set_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if ((plan_set_->is_multi_stmt_plan())) {
    // If it is a multi stmt plan, it must be a single-table distributed plan, and the physical partition address can be directly calculated based on table access
    // single table should just return plan, do not match
    if (0 == dist_plans_.count()) {
      ret = OB_SQL_PC_NOT_EXIST;
      LOG_DEBUG("dist plan list is empty", K(ret), K(dist_plans_.count()));
    } else if (OB_ISNULL(dist_plans_.at(0))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      plan = dist_plans_.at(0);
      is_matched = true;

      // fill table access for single plan using px
      // for single dist plan without px, we already fill the phy accesses while calculating plan type
      // for multi table px plan, physical access is calculated in match step
      ObArray<TableAccessTargets> table_targets;
      if (OB_ISNULL(plan_set_)) {
        ret = OB_INVALID_ARGUMENT;
      } else if (!plan_set_->enable_inner_part_parallel()) {
        // do nothing
      } else if (OB_FAIL(TableAccessTargetBuilder::collect_access_targets(plan->get_table_access_plans(),
                                                                pc_ctx,
                                                                table_targets))) {
      } else if (table_targets.empty()) {
        // do nothing.
      } else if (OB_FAIL(TableAccessTargetBuilder::build_runtime_accesses(
                   pc_ctx.exec_ctx_.get_das_ctx(), plan->get_table_access_plans(),
                   table_targets))) {
      }
    }
  }

  ObPlanMatchHelper helper;
  ObArray<TableAccessTargets> phy_tbl_infos;
  ObArray<TableAccessPlan> out_access_plans;
  for (int64_t i = 0; OB_SUCC(ret) && !is_matched && i < dist_plans_.count();
       i++) {
    ObPhysicalPlan *tmp_plan = dist_plans_.at(i);
    phy_tbl_infos.reuse();
    out_access_plans.reuse();
    if (OB_ISNULL(tmp_plan)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(helper.match_plan(pc_ctx, tmp_plan, is_matched, phy_tbl_infos, out_access_plans))) {
    } else if (is_matched) {
      plan = tmp_plan;
      if (OB_FAIL(TableAccessTargetBuilder::build_runtime_accesses(DAS_CTX(pc_ctx.exec_ctx_),
                                                        out_access_plans, phy_tbl_infos))) {
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
  ObPlanMatchHelper helper;
  ObArray<TableAccessTargets> phy_tbl_infos;
  ObArray<TableAccessPlan> out_access_plans;

  for (int64_t i = 0; OB_SUCC(ret) && !is_matched && i < dist_plans_.count(); i++) {
    // Check if another thread has successfully added this plan
    phy_tbl_infos.reuse();
    out_access_plans.reuse();
    const ObPhysicalPlan *tmp_plan = dist_plans_.at(i);
    if (OB_ISNULL(tmp_plan)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(helper.match_plan(pc_ctx, tmp_plan, is_matched, phy_tbl_infos, out_access_plans))) {
    } else {
      is_matched = is_matched && tmp_plan->has_same_access_constraints(plan);
    }

    if (!is_matched) {
      // do nothing
    } else {
      ret = OB_SQL_PC_PLAN_DUPLICATE;
    }
  }

  if (OB_SUCC(ret) && !is_matched) {
    if (OB_FAIL(dist_plans_.push_back(&plan))) {
    }
  }

  return ret;
}

// Delete all plan and corresponding plan stat
int ObDistPlans::remove_all_plan()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  for (int64_t i = 0; i < dist_plans_.count(); i++) {
    if (OB_ISNULL(dist_plans_.at(i))) {
      tmp_ret = OB_ERR_UNEXPECTED;
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
