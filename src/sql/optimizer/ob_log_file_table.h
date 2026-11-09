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
#include "sql/resolver/dml/ob_file_table_def.h"

namespace oceanbase
{
namespace sql
{

class FileTablePath;

/**
 * @brief The logical operator that reads a local file as if it were a table.
 *
 * The operator does not hold any data, it only describes the file that has to
 * be scanned and the columns that the file provides. The rows are produced by
 * the physical operator ObFileTableOp.
 */
class ObLogFileTable : public ObLogicalOperator
{
public:
  ObLogFileTable(ObLogPlan &plan)
      : ObLogicalOperator(plan),
        table_def_(NULL),
        table_name_(),
        table_id_(common::OB_INVALID_ID),
        file_path_(NULL)
  {}
  virtual ~ObLogFileTable() {}
  virtual int est_cost() override;
  virtual int do_re_est_cost(EstimateCostInfo &param, double &card, double &op_cost, double &cost);
  virtual int compute_equal_set() override;
  virtual int compute_table_set() override;
  virtual int get_op_exprs(ObIArray<ObRawExpr*> &all_exprs) override;
  virtual int is_my_fixed_expr(const ObRawExpr *expr, bool &is_fixed) override;
  virtual int allocate_expr_post(ObAllocExprContext &ctx) override;
  virtual int inner_replace_op_exprs(ObRawExprReplacer &replacer) override;
  virtual int get_plan_item_info(PlanText &plan_text, ObSqlPlanItem &plan_item) override;
  int allocate_dummy_output();

  const common::ObIArray<ObColumnRefRawExpr *> &get_column_exprs() const { return column_exprs_; }
  common::ObIArray<ObColumnRefRawExpr *> &get_column_exprs() { return column_exprs_; }
  inline const ObFileTableDef *get_file_table_def() const { return table_def_; }
  inline void set_file_table_def(ObFileTableDef *table_def) { table_def_ = table_def; }
  inline common::ObString &get_table_name() { return table_name_; }
  inline const common::ObString &get_table_name() const { return table_name_; }
  inline void set_table_name(const common::ObString &table_name) { table_name_ = table_name; }
  inline uint64_t get_table_id() const { return table_id_; }
  inline void set_table_id(const uint64_t table_id) { table_id_ = table_id; }
  inline void set_file_path(FileTablePath *file_path) { file_path_ = file_path; }
  inline const FileTablePath *get_file_path() const { return file_path_; }
private:
  common::ObSEArray<ObColumnRefRawExpr*, 4, common::ModulePageAllocator, true> column_exprs_;
  ObFileTableDef *table_def_;
  common::ObString table_name_;
  uint64_t table_id_;
  FileTablePath *file_path_;
  DISALLOW_COPY_AND_ASSIGN(ObLogFileTable);
};

} // end namespace sql
} // end namespace oceanbase
