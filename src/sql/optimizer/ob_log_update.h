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

#ifndef _OB_LOG_UPDATE_H
#define _OB_LOG_UPDATE_H 1
#include "ob_log_del_upd.h"

namespace oceanbase
{
namespace sql
{
class ObUpdateStmt;
class ObLogUpdate : public ObLogDelUpd
{
public:
  ObLogUpdate(ObDelUpdLogPlan &plan)
      : ObLogDelUpd(plan)
  {}
  virtual ~ObLogUpdate() {}
  virtual int est_cost() override;
  virtual int do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost) override;
  int inner_est_cost(double child_card, double &op_cost);
  static int inner_est_cost(const ObOptimizerContext &opt_ctx,
                            const ObIArray<IndexDMLInfo*> &index_infos,
                            const double child_card,
                            double &op_cost);
  virtual int get_op_exprs(ObIArray<ObRawExpr*> &all_exprs) override;
  virtual int is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed) override;
  virtual const char *get_name() const override;
  virtual int get_plan_item_info(PlanText &plan_text, 
                                ObSqlPlanItem &plan_item) override;
  virtual int op_is_update_pk_with_dop(bool &is_update) override;
private:
  virtual int generate_part_id_expr_for_foreign_key(ObIArray<ObRawExpr*> &all_exprs) override;
  virtual int generate_multi_part_partition_id_expr() override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogUpdate);
};
}
}
#endif
