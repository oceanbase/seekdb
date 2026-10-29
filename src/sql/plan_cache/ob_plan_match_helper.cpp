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
#include "sql/plan_cache/ob_plan_match_helper.h"
#include "sql/plan_cache/ob_plan_set.h"
#include "sql/optimizer/ob_log_plan.h"

using namespace oceanbase::share;

namespace oceanbase {
namespace sql {

int ObPlanMatchHelper::match_plan(const ObPlanCacheCtx &pc_ctx,
                                  const ObPhysicalPlan *plan,
                                  bool &is_matched,
                                  ObIArray<TableAccessTargets> &phy_tbl_infos,
                                  ObIArray<TableAccessPlan> &out_access_plans) const
{
  int ret = OB_SUCCESS;
  is_matched = true;
  const ObIArray<TableAccessConstraint>& base_cons = plan->get_base_constraints();
  const ObIArray<ObPlanPwjConstraint>& strict_cons = plan->get_strict_constraints();
  const ObIArray<ObPlanPwjConstraint>& non_strict_cons = plan->get_non_strict_constraints();
  const ObIArray<TableAccessPlan> &plan_access_plans = plan->get_table_access_plans();
  PWJTabletIdMap pwj_map;
  bool use_pwj_map = false;

  if (OB_ISNULL(GET_MY_SESSION(pc_ctx.exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (0 == base_cons.count()) {
    // match all
    is_matched = true;
  } else {
    if (OB_SUCC(ret)) {
      // check base table constraints
      if (OB_FAIL(calculate_tablet_targets(base_cons, plan_access_plans, pc_ctx,
                                      out_access_plans, phy_tbl_infos))) {
      } else if (OB_FAIL(cmp_table_types(base_cons, out_access_plans,
                                        phy_tbl_infos, is_matched))) {
      } else if (!is_matched) {
      } else if (OB_FAIL(check_partition_constraint(pc_ctx, base_cons, phy_tbl_infos, is_matched))) {
      } else if (!is_matched) {
      } else if (strict_cons.count() <= 0 && non_strict_cons.count() <= 0) {
        // do nothing
      } else if (OB_FAIL(pwj_map.create(8, ObModIds::OB_PLAN_EXECUTE))) {
      } else if (OB_FAIL(check_inner_constraints(strict_cons, non_strict_cons, phy_tbl_infos,
                                                pc_ctx, pwj_map, is_matched))) {
      } else {
        use_pwj_map = true;
      }

      if (OB_SUCC(ret) && is_matched && use_pwj_map) {
        GroupPWJTabletIdMap *exec_group_pwj_map = nullptr;
        if (OB_FAIL(pc_ctx.exec_ctx_.get_group_pwj_map(exec_group_pwj_map))) {
        } else if (OB_FAIL(exec_group_pwj_map->reuse())) {
        }
        GroupPWJTabletIdInfo group_pwj_tablet_id_info;
        TabletIdArray &tablet_id_array = group_pwj_tablet_id_info.tablet_id_array_;
        for (int64_t group_id = 0; OB_SUCC(ret) && group_id < strict_cons.count(); ++group_id) {
          group_pwj_tablet_id_info.group_id_ = group_id;
          const ObPlanPwjConstraint &pwj_cons = strict_cons.at(group_id);
          for (int64_t i = 0; OB_SUCC(ret) && i < pwj_cons.count(); ++i) {
            const int64_t table_idx = pwj_cons.at(i);
            uint64_t table_id = base_cons.at(table_idx).key_.table_id_;
            tablet_id_array.reset();
            if (!base_cons.at(table_idx).is_multi_part_insert()) {
              if (OB_FAIL(pwj_map.get_refactored(table_idx, tablet_id_array))) {
                if (OB_HASH_NOT_EXIST == ret) {
                  // means this is not a partition wise join table
                  ret = OB_SUCCESS;
                } else {
                }
              } else if (OB_FAIL(exec_group_pwj_map->set_refactored(table_id, group_pwj_tablet_id_info))) {
              }
            }
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    is_matched = false;
  }
  if (pwj_map.created()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_UNLIKELY(OB_SUCCESS != (tmp_ret = pwj_map.destroy()))) {
    }
  }
  return ret;
}

int ObPlanMatchHelper::find_access_plan(const TableAccessKey key,
                                            const ObIArray<TableAccessPlan> &table_access_plans,
                                            const TableAccessPlan *&result_access_plan) const
{
  int ret = OB_SUCCESS;
  result_access_plan = NULL;
  const TableAccessPlan *access_plan;
  for (int i = 0; i < table_access_plans.count(); i++) {
    access_plan = &table_access_plans.at(i);
    if (access_plan->get_table_id() == key.table_id_ &&
        access_plan->get_ref_table_id() == key.ref_table_id_) {
      result_access_plan = access_plan;
      break;
    }
  }
  if (OB_ISNULL(result_access_plan)) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPlanMatchHelper::calculate_tablet_targets(
    const ObIArray<TableAccessConstraint> &access_constraints,
    const ObIArray<TableAccessPlan> &in_access_plans,
    const ObPlanCacheCtx &pc_ctx,
    common::ObIArray<TableAccessPlan> &out_access_plans,
    common::ObIArray<TableAccessTargets> &phy_tbl_infos) const
{
  int ret = OB_SUCCESS;
  if (access_constraints.count() <= 0 || in_access_plans.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < access_constraints.count(); i++) {
      const TableAccessPlan *tmp_access_plan;
      if (OB_FAIL(find_access_plan(access_constraints.at(i).key_,
                                       in_access_plans,
                                       tmp_access_plan))) {
      } else if (OB_ISNULL(tmp_access_plan)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(out_access_plans.push_back(*tmp_access_plan))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(TableAccessTargetBuilder::collect_access_targets(out_access_plans,
                                                         pc_ctx,
                                                         phy_tbl_infos))) {
      } else {
      }
    }
  }

  if (OB_FAIL(ret)) {
    out_access_plans.reset();
    phy_tbl_infos.reset();
  }
  return ret;
}

int ObPlanMatchHelper::cmp_table_types(
    const ObIArray<TableAccessConstraint> &access_constraints,
    const common::ObIArray<TableAccessPlan> &access_plans,
    const common::ObIArray<TableAccessTargets> &phy_tbl_infos,
    bool &is_same) const
{
  int ret = OB_SUCCESS;
  is_same = true;
  if (access_constraints.count() != phy_tbl_infos.count() ||
      access_plans.count() != phy_tbl_infos.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < access_constraints.count(); i++) {
      const TabletAccessTargetIArray &tablet_targets =
        phy_tbl_infos.at(i).get_tablets();
      const TableAccessPlan &access_plan = access_plans.at(i);
      const TableAccessShape access_shape = access_plan.get_access_shape(tablet_targets);
      is_same = (access_shape == access_constraints.at(i).access_shape_);
    }
  }
  if (OB_FAIL(ret)) {
    is_same = false;
  }
  return ret;
}

int ObPlanMatchHelper::check_partition_constraint(
    const ObPlanCacheCtx &pc_ctx,
    const ObIArray<TableAccessConstraint> &access_constraints,
    const common::ObIArray<TableAccessTargets> &phy_tbl_infos,
    bool &is_match) const
{
  int ret = OB_SUCCESS;
  is_match = true;
  share::schema::ObSchemaGetterGuard *schema_guard = pc_ctx.sql_ctx_.schema_guard_;
  const share::schema::ObTableSchema *table_schema = NULL;
  if (access_constraints.count() != phy_tbl_infos.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(schema_guard)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(GET_MY_SESSION(pc_ctx.exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    
    for (int64_t i = 0; OB_SUCC(ret) && is_match && i < access_constraints.count(); i++) {
      const TabletAccessTargetIArray &tablet_targets =
        phy_tbl_infos.at(i).get_tablets();
      if (!access_constraints.at(i).is_partition_single() && !access_constraints.at(i).is_subpartition_single()) {
        // do nothing
      } else if (OB_FAIL(schema_guard->get_table_schema( phy_tbl_infos.at(i).get_ref_table_id(), table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (access_constraints.at(i).is_partition_single()) {
        // is_partition_single requires that the current secondary partition
        // table only involves one primary partition
        int64_t first_part_id = OB_INVALID_PARTITION_ID;
        for (int64_t j = 0; OB_SUCC(ret) && is_match && j < tablet_targets.count(); ++j) {
          ObTabletID cur_tablet_id =
              tablet_targets.at(j).get_tablet_id();
          int64_t cur_part_id = OB_INVALID_ID;
          int64_t cur_subpart_id = OB_INVALID_ID;
          if (OB_FAIL(table_schema->get_part_id_by_tablet(cur_tablet_id, cur_part_id, cur_subpart_id))) {
          } else if (OB_INVALID_PARTITION_ID == first_part_id) {
            first_part_id = cur_part_id;
          } else if (cur_part_id != first_part_id) {
            is_match = false;
          }
        }
      } else if (access_constraints.at(i).is_subpartition_single()) {
        // is_subpartition_single requires that each primary partition of the current
        // secondary partition table involves only one secondary partition
        ObSEArray<int64_t, 4> part_ids;
        for (int64_t j = 0; OB_SUCC(ret) && is_match && j < tablet_targets.count(); ++j) {
          ObTabletID cur_tablet_id =
              tablet_targets.at(j).get_tablet_id();
          int64_t cur_part_id = OB_INVALID_ID;
          int64_t cur_subpart_id = OB_INVALID_ID;
          if (OB_FAIL(table_schema->get_part_id_by_tablet(cur_tablet_id, cur_part_id, cur_subpart_id))) {
          } else {
            for (int64_t k = 0; OB_SUCC(ret) && is_match && k < part_ids.count(); ++k) {
              if (part_ids.at(k) == cur_part_id) {
                is_match = false;
              }
            }

            if (OB_FAIL(ret)) {
              // do nothing
            } else if (!is_match) {
              // do nothing
            } else if (OB_FAIL(part_ids.push_back(cur_part_id))) {
            } else {
              // do nothing
            }
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    is_match = false;
  }
  return ret;
}

int ObPlanMatchHelper::check_inner_constraints(
    const ObIArray<ObPlanPwjConstraint> &strict_cons,
    const ObIArray<ObPlanPwjConstraint> &non_strict_cons,
    const common::ObIArray<TableAccessTargets> &phy_tbl_infos,
    const ObPlanCacheCtx &pc_ctx,
    PWJTabletIdMap &pwj_map,
    bool &is_same) const
{
  int ret = OB_SUCCESS;
  is_same = true;
  if (strict_cons.count() >0 || non_strict_cons.count() > 0) {
    SMART_VAR(ObStrictPwjComparer, strict_pwj_comparer) {
      for (int64_t i = 0; OB_SUCC(ret) && is_same && i < strict_cons.count(); ++i) {
        const ObPlanPwjConstraint &pwj_cons = strict_cons.at(i);
        if (OB_UNLIKELY(pwj_cons.count() <= 1)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(check_strict_pwj_cons(pc_ctx, pwj_cons, phy_tbl_infos,
                                                 strict_pwj_comparer, pwj_map, is_same))) {
        } else {
        }
      }

      for (int64_t i = 0; OB_SUCC(ret) && is_same && i < non_strict_cons.count(); ++i) {
        const ObPlanPwjConstraint &pwj_cons = non_strict_cons.at(i);
        if (OB_UNLIKELY(pwj_cons.count() <= 1)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          // Every valid tablet is local in seekdb. A non-strict PWJ constraint
          // therefore only needs each referenced table to have a local tablet.
          for (int64_t j = 0; OB_SUCC(ret) && is_same && j < pwj_cons.count(); ++j) {
            const int64_t table_idx = pwj_cons.at(j);
            if (OB_UNLIKELY(table_idx < 0 || table_idx >= phy_tbl_infos.count())) {
              ret = OB_ERR_UNEXPECTED;
            } else if (phy_tbl_infos.at(table_idx).get_tablet_count() <= 0) {
              is_same = false;
            }
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    is_same = false;
  }
  return ret;
}

int ObPlanMatchHelper::check_strict_pwj_cons(
        const ObPlanCacheCtx &pc_ctx,
        const ObPlanPwjConstraint &pwj_cons,
        const ObIArray<TableAccessTargets> &phy_tbl_infos,
        ObStrictPwjComparer &pwj_comparer,
        PWJTabletIdMap &pwj_map,
        bool &is_same) const
{
  int ret = OB_SUCCESS;
  // check all table in same pwj constraint have same partition count
  const int64_t part_count = phy_tbl_infos.at(pwj_cons.at(0)).get_tablet_count();
  for (int64_t i = 1; is_same && i < pwj_cons.count(); ++i) {
    if (part_count != phy_tbl_infos.at(pwj_cons.at(i)).get_tablet_count()) {
      is_same = false;
    }
  }

  if (1 == part_count) {
    // All tables in this PWJ constraint are single-partition tables.
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < pwj_cons.count() - 1; ++i) {
      const TableAccessTargets &l_phy_tbl_info = phy_tbl_infos.at(pwj_cons.at(i));
      const TableAccessTargets &r_phy_tbl_info = phy_tbl_infos.at(pwj_cons.at(i+1));
      if (OB_FAIL(match_tablet_counts(l_phy_tbl_info, r_phy_tbl_info, is_same))) {
      }
    }
  } else if (OB_ISNULL(GET_MY_SESSION(pc_ctx.exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // distribute partition wise join
    pwj_comparer.reset();
    
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < pwj_cons.count(); ++i) {
      const int64_t table_idx = pwj_cons.at(i);
      PwjTable pwj_table;
      const TableAccessTargets &phy_tbl_info = phy_tbl_infos.at(table_idx);
      share::schema::ObSchemaGetterGuard *schema_guard = pc_ctx.sql_ctx_.schema_guard_;
      const share::schema::ObTableSchema *table_schema = NULL;
      if (OB_ISNULL(schema_guard)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(schema_guard->get_table_schema( phy_tbl_info.get_ref_table_id(), table_schema))) {
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(pwj_table.init(*table_schema, phy_tbl_info))) {
      } else if (OB_FAIL(pwj_comparer.add_table(pwj_table, is_same))) {
      } else if (is_same &&
                 OB_FAIL(pwj_map.set_refactored(table_idx, pwj_comparer.get_tablet_id_group().at(i)))) {
      }
    }
  }
  return ret;
}

int ObPlanMatchHelper::match_tablet_counts(const TableAccessTargets &left,
                                         const TableAccessTargets &right,
                                         bool &is_matched) const
{
  int ret = OB_SUCCESS;
  is_matched = true;
  if (left.get_tablet_count() != right.get_tablet_count()) {
    is_matched = false;
  } else if (left.get_tablet_count() <= 0) {
    ret = OB_ERR_UNEXPECTED;
  }
  // All validated targets are local. Equal counts satisfy the co-access
  // constraint; strict partition-wise matching is checked separately.

  if (OB_FAIL(ret)) {
    is_matched = false;
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
