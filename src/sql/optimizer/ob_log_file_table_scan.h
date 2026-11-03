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

#pragma once

#include "sql/optimizer/ob_logical_operator.h"
#include "sql/resolver/dml/ob_dml_stmt.h"

namespace oceanbase
{
namespace sql
{
struct ObFileTableDef;
class FileTablePath;
class ObLogFileTableScan : public ObLogicalOperator
{
public:
  ObLogFileTableScan(ObLogPlan &plan)
    : ObLogicalOperator(plan),
      table_id_(common::OB_INVALID_ID),
      table_def_(NULL),
      table_name_(),
      file_path_(),
      column_exprs_() {}
  virtual ~ObLogFileTableScan() {}
  virtual int get_op_exprs(ObIArray<ObRawExpr *> &all_exprs) override;
  virtual int is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed) override;
  virtual int allocate_expr_post(ObAllocExprContext &ctx) override;
  virtual int get_plan_item_info(PlanText &plan_text, ObSqlPlanItem &plan_item) override;
  virtual uint64_t hash(uint64_t seed) const override;
  int generate_access_exprs();

  inline void set_table_id(const uint64_t table_id) { table_id_ = table_id; }
  inline uint64_t get_table_id() const { return table_id_; }
  inline void set_file_table_def(ObFileTableDef *table_def) { table_def_ = table_def; }
  inline ObFileTableDef *get_file_table_def() const { return table_def_; }
  inline void set_table_name(const ObString &table_name) { table_name_ = table_name; }
  inline const ObString &get_table_name() const { return table_name_; }
  inline void set_file_path(const ObString &file_path) { file_path_ = file_path; }
  inline const ObString &get_file_path() const { return file_path_; }
  inline const ObIArray<ObColumnRefRawExpr *> &get_column_exprs() const { return column_exprs_; }
  inline ObIArray<ObColumnRefRawExpr *> &get_column_exprs() { return column_exprs_; }
private:
  uint64_t table_id_;
  ObFileTableDef *table_def_;
  ObString table_name_;
  ObString file_path_;
  common::ObSEArray<ObColumnRefRawExpr *, 8, common::ModulePageAllocator, true> column_exprs_;
  DISALLOW_COPY_AND_ASSIGN(ObLogFileTableScan);
};
} // namespace sql
} // namespace oceanbase
