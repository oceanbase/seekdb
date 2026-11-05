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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_PLAN_MATCH_HELPER_
#define OCEANBASE_SQL_PLAN_CACHE_OB_PLAN_MATCH_HELPER_

#include "lib/container/ob_se_array.h"
#include "sql/ob_sql_context.h"
#include "sql/optimizer/ob_pwj_comparer.h"

namespace oceanbase {
namespace sql {
class ObPhysicalPlan;
class ObPlanCacheCtx;
class TableAccessPlan;

class ObPlanMatchHelper
{
public:
  ObPlanMatchHelper() = default;
  int match_plan(const ObPlanCacheCtx &pc_ctx,
                 const ObPhysicalPlan *plan,
                 bool &is_matched,
                 common::ObIArray<TableAccessTargets> &phy_tbl_infos,
                 common::ObIArray<TableAccessPlan> &out_access_plans) const;

  /**
   * @brief Get the table accesses of all relational tables involved in TableAccessConstraint
   * from the table access cached by Physical Plan
   *
   */
  int calculate_tablet_targets(
      const ObIArray<TableAccessConstraint> &access_constraints,
      const common::ObIArray<TableAccessPlan> &in_access_plans,
      const ObPlanCacheCtx &pc_ctx,
      common::ObIArray<TableAccessPlan> &out_access_plans,
      common::ObIArray<TableAccessTargets> &phy_tbl_infos) const;

private:
  /**
   * @brief Get table access with table access key
   *
   */
  int find_access_plan(
      const TableAccessKey key,
      const ObIArray<TableAccessPlan> &table_access_plans,
      const TableAccessPlan *&result_access_plan) const;
  /**
   * @brief Compare table access types
   *
   */
  int cmp_table_types(
      const ObIArray<TableAccessConstraint> &access_constraints,
      const common::ObIArray<TableAccessPlan> &access_plans,
      const common::ObIArray<TableAccessTargets> &phy_tbl_infos,
      bool &is_same) const;
  /**
   * @brief Check only_one_first_part constraint and only_one_sub_part constraint
   * in base table constraints
   *
   */
  int check_partition_constraint(const ObPlanCacheCtx &pc_ctx,
                                 const ObIArray<TableAccessConstraint> &access_constraints,
                                 const common::ObIArray<TableAccessTargets> &phy_tbl_infos,
                                 bool &is_match) const;
  /**
   * @brief Check that tables in strict pwj constraints and non-strict pwj constraints
   * conform to physical partition consistent constraints. And save the mapping relationship
   * of partition_id of the base table with constraint relationship to pwj_map
   *
   */
  int check_inner_constraints(const common::ObIArray<ObPlanPwjConstraint>& strict_cons,
                              const common::ObIArray<ObPlanPwjConstraint>& non_strict_cons,
                              const common::ObIArray<TableAccessTargets> &phy_tbl_infos,
                              const ObPlanCacheCtx &pc_ctx,
                              PWJTabletIdMap &pwj_map,
                              bool &is_same) const;
  /**
   * @brief Check if strict pwj constraints are satisfied
   *
   */
  int check_strict_pwj_cons(const ObPlanCacheCtx &pc_ctx,
                            const ObPlanPwjConstraint &pwj_cons,
                            const ObIArray<TableAccessTargets> &phy_tbl_infos,
                            ObStrictPwjComparer &pwj_comparer,
                            PWJTabletIdMap &pwj_map,
                            bool &is_same) const;
  // Check single-tablet co-access after local target validation.
  int match_tablet_counts(const TableAccessTargets &left,
                         const TableAccessTargets &right,
                         bool &is_matched) const;
};
} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_PLAN_CACHE_OB_PLAN_MATCH_HELPER_ */
