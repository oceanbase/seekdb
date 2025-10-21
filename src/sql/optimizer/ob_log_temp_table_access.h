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

#ifndef OCEANBASE_SQL_OB_LOG_TEMP_TABLE_ACCESS_H
#define OCEANBASE_SQL_OB_LOG_TEMP_TABLE_ACCESS_H 1

#include "sql/optimizer/ob_logical_operator.h"
#include "sql/optimizer/ob_log_operator_factory.h"
#include "sql/resolver/dml/ob_sql_hint.h"

namespace oceanbase
{
namespace sql
{
class ObLogTempTableAccess : public ObLogicalOperator
{
public:
  ObLogTempTableAccess(ObLogPlan &plan);
  virtual ~ObLogTempTableAccess();
  int generate_access_expr();
  virtual int get_op_exprs(ObIArray<ObRawExpr*> &all_exprs) override;
  virtual int allocate_expr_post(ObAllocExprContext &ctx) override;
  virtual int do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost) override;
  virtual bool is_block_op() const override { return false; }
  void set_table_id(uint64_t table_id) { table_id_ = table_id; }
  uint64_t get_table_id() const { return table_id_; }
  inline void set_temp_table_id(uint64_t temp_table_id){ temp_table_id_ = temp_table_id; }
  uint64_t get_temp_table_id() const { return temp_table_id_; }
  inline common::ObString &get_table_name() { return temp_table_name_; }
  inline common::ObString &get_access_name() { return access_name_; }
  inline const common::ObIArray<ObRawExpr *> &get_access_exprs() const
  { return access_exprs_; }
  inline common::ObIArray<ObRawExpr *> &get_access_exprs()
  { return access_exprs_; }
  virtual int get_plan_item_info(PlanText &plan_text, 
                                ObSqlPlanItem &plan_item) override;
  int get_temp_table_plan(ObLogicalOperator *& insert_op);
  virtual int get_card_without_filter(double &card) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObLogTempTableAccess);

private:
  uint64_t table_id_;
  uint64_t temp_table_id_;
  common::ObString temp_table_name_;
  common::ObString access_name_;
  // base columns to scan
  common::ObSEArray<ObRawExpr *, 4, common::ModulePageAllocator, true> access_exprs_;
};

}
}

#endif // OCEANBASE_SQL_OB_LOG_TEMP_TABLE_ACCESS_H
