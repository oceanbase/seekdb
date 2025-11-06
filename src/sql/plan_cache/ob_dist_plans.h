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

#ifndef _OB_DIST_PLANS_H
#define _OB_DIST_PLANS_H

#include "lib/container/ob_se_array.h"
#include "sql/ob_sql_context.h"
#include "sql/optimizer/ob_pwj_comparer.h"

namespace oceanbase {
namespace sql {
struct ObPlanCacheCtx;
class ObPhysicalPlan;
class ObTableLocation;
class ObPhyTableLocation;
class ObCandiTableLoc;
class ObSqlPlanSet;

class ObDistPlans {
public:
  ObDistPlans()
    : plan_set_(NULL)
  { }

  /**
   * @brief init a ObDistPlans
   */
  int init(ObSqlPlanSet *ps);

  /**
   * @brief add a plan to dist plan list
   *
   * @param plan distribute physical plan to add
   * @param pc_ctx plan cache context
   */
  int add_plan(ObPhysicalPlan &plan,
               ObPlanCacheCtx &pc_ctx);


  /**
   * @brief get plan from dist plan list
   *
   * @param pc_ctx plan cache contex
   * @retval plan physical plan returned if matched
   */
  int get_plan(ObPlanCacheCtx &pc_ctx,
               ObPhysicalPlan *&plan);

  /**
   * @brief remove all the plans the corresponding plan stats
   *
   */
  int remove_all_plan();

  /**
   * @brief return the memory used by dist plan list
   *
   * @return memory size
   */
  int64_t get_mem_size() const;

  /**
   * @brief remove all the plan stats
   *
   */

  /**
   * @brief get # of plans
   *
   */
  int64_t count() const { return dist_plans_.count(); }

  /**
   * @brief reset ob dist plan list
   *
   */
  void reset()
  {
    dist_plans_.reset();
    plan_set_ = nullptr;
  }

  // get first plan in the array if possible
  ObPhysicalPlan* get_first_plan();

private:
  bool is_plan_available(const ObPhysicalPlan &plan, ObPlanCacheCtx &pc_ctx) const;
  bool is_same_plan(const ObPhysicalPlan &plan, const ObPhysicalPlan &compare_plan,
                    ObPlanCacheCtx &pc_ctx) const;

  /**
   * @brief Set table location for pc_ctx.exec_ctx
   *
   */
  int set_phy_table_locations_for_ctx(ObPlanCacheCtx &pc_ctx,
                                      const ObIArray<ObPhyTableLocation> &table_locations);

private:
  common::ObSEArray<ObPhysicalPlan *, 4> dist_plans_;
  ObSqlPlanSet *plan_set_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObDistPlans);
};

} // namespace sql
} // namespace oceanbase

#endif /* _OB_DIST_PLANS_H */
