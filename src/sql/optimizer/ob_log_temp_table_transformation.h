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

#ifndef OCEANBASE_SQL_OB_LOG_TEMP_TABLE_TRANSFORMATION_H
#define OCEANBASE_SQL_OB_LOG_TEMP_TABLE_TRANSFORMATION_H

#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/optimizer/ob_logical_operator.h"

namespace oceanbase
{
namespace sql
{
class ObLogTempTableTransformation : public ObLogicalOperator
{
public:
  ObLogTempTableTransformation(ObLogPlan &plan);
  virtual ~ObLogTempTableTransformation();
  virtual int compute_op_ordering() override;
  virtual bool is_consume_child_1by1() const { return true; }
  virtual int compute_fd_item_set() override;
  virtual int est_cost() override;
  virtual int est_width() override;
  virtual bool is_block_op() const override { return false; }
  virtual bool is_block_input(const int64_t child_idx) const override { return child_idx != get_num_of_child() - 1; }
  virtual int compute_op_parallel_and_server_info() override;
  virtual int do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost) override;
  virtual int est_ambient_card() override;
  int get_temp_table_exprs(ObIArray<ObRawExpr *> &set_exprs) const;
  int allocate_startup_expr_post() override;
  virtual int get_card_without_filter(double &card) override;
};

} // end of namespace sql
} // end of namespace oceanbase

#endif // OCEANBASE_SQL_OB_LOG_TEMP_TABLE_TRANSFORMATION_H
