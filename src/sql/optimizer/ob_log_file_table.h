/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _OB_LOG_FILE_TABLE_H
#define _OB_LOG_FILE_TABLE_H
#include "sql/optimizer/ob_logical_operator.h"
#include "sql/resolver/dml/ob_dml_stmt.h"

namespace oceanbase
{
namespace sql
{

// The logical operator of a file table. The table function of the from clause
// is resolved into a table item with the columns of the file, this operator
// scans the file and produces the rows of the table.
class ObLogFileTable : public ObLogicalOperator
{
public:
  ObLogFileTable(ObLogPlan &plan)
      : ObLogicalOperator(plan), table_id_(OB_INVALID_ID), table_name_(), file_def_(NULL) {}
  virtual ~ObLogFileTable() {}
  void set_table_id(uint64_t table_id) { table_id_ = table_id; }
  uint64_t get_table_id() const { return table_id_; }
  void set_table_name(const common::ObString &name) { table_name_ = name; }
  const common::ObString &get_table_name() const { return table_name_; }
  void set_file_def(const ObFileTableDef *file_def) { file_def_ = file_def; }
  const ObFileTableDef *get_file_def() const { return file_def_; }
  virtual uint64_t hash(uint64_t seed) const override;
private:
  uint64_t table_id_;
  common::ObString table_name_;
  // The definition belongs to the table item of the statement.
  const ObFileTableDef *file_def_;
  DISALLOW_COPY_AND_ASSIGN(ObLogFileTable);
};

} // namespace sql
} // namespace oceanbase

#endif // _OB_LOG_FILE_TABLE_H
