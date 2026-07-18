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

#ifndef _OB_LOG_SUBQUERY_SCAN_H_
#define _OB_LOG_SUBQUERY_SCAN_H_
#include "sql/optimizer/ob_logical_operator.h"

namespace oceanbase
{
namespace sql
{
class ObLogSubPlanScan : public ObLogicalOperator
{
public:
  ObLogSubPlanScan(ObLogPlan &plan)
      : ObLogicalOperator(plan),
        subquery_id_(common::OB_INVALID_ID),
        subquery_name_(),
        access_exprs_()
  {}

  ~ObLogSubPlanScan() {};
  int generate_access_exprs();
  virtual int get_op_exprs(ObIArray<ObRawExpr*> &all_exprs) override;
  virtual int is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed) override;
  virtual int allocate_expr_post(ObAllocExprContext &ctx) override;
  void set_subquery_id(uint64_t subquery_id) { subquery_id_ = subquery_id; }
  inline const uint64_t &get_subquery_id() const { return subquery_id_; }
  inline common::ObString &get_subquery_name() { return subquery_name_; }
  inline const common::ObIArray<ObRawExpr *> &get_access_exprs() const { return access_exprs_; }
  inline common::ObIArray<ObRawExpr *> &get_access_exprs() { return access_exprs_; }
  virtual int do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost) override;
  virtual int check_output_dependance(ObIArray<ObRawExpr *> &child_output, PPDeps &deps) override;
  virtual int get_plan_item_info(PlanText &plan_text, 
                                ObSqlPlanItem &plan_item) override;
private:
  uint64_t subquery_id_;
  common::ObString subquery_name_;
  common::ObSEArray<ObRawExpr*, 4, common::ModulePageAllocator, true> access_exprs_;
  DISALLOW_COPY_AND_ASSIGN(ObLogSubPlanScan);
};

} // end of namespace sql
} // end of namespace oceanbase


#endif /* OB_LOG_SUBQUERY_SCAN_H_ */
