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

#include "sql/engine/ob_operator.h"
#include "sql/engine/basic/ob_file_scan_utils.h"
#include "sql/resolver/dml/ob_file_table_def.h"

namespace oceanbase
{
namespace sql
{

/**
 * @brief The specification of the operator that reads a local file as a table.
 *
 * The specification holds the description of the file and the expressions of
 * its columns. The operator streams the rows of the file and fills the datums
 * of the column expressions.
 */
class ObFileTableSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObFileTableSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      column_exprs_(alloc),
      path_(),
      format_(ObFileScanFormat::CSV),
      header_(true),
      delimiter_(1, ","),
      quote_(1, "\""),
      escape_(1, "\""),
      null_str_(),
      json_format_(17, "newline_delimited"),
      union_by_name_(true),
      file_size_(-1),
      file_mtime_(-1),
      column_names_(alloc),
      column_types_(alloc),
      describe_(false),
      alloc_(&alloc)
  {}
  INHERIT_TO_STRING_KV("op_spec", ObOpSpec, K_(path), K_(format), K_(column_exprs));

  // the expression of every column of the file, the operator fills their datums
  common::ObFixedArray<ObExpr *, common::ObIAllocator> column_exprs_;
  // the description of the file
  common::ObString path_;
  ObFileScanFormat format_;
  bool header_;
  common::ObString delimiter_;
  common::ObString quote_;
  common::ObString escape_;
  common::ObString null_str_;
  common::ObString json_format_;
  bool union_by_name_;
  // the size and the modification time of the file when its schema was inferred
  int64_t file_size_;
  int64_t file_mtime_;
  // the names and the types of the columns, they are inferred from the file
  common::ObFixedArray<common::ObString, common::ObIAllocator> column_names_;
  common::ObFixedArray<common::ObObjType, common::ObIAllocator> column_types_;
  // the operator describes the columns of the file instead of producing its rows
  bool describe_;

private:
  // the allocator used to copy the texts of the spec when it is deserialized
  common::ObIAllocator *alloc_;
  // copy a text into the memory of the allocator of the spec
  int copy_string(common::ObString &str);
  // the texts of a deserialized spec point to the buffer of the deserialization,
  // they are copied into the memory of the allocator of the spec
  int repoint_strings();
  DISALLOW_COPY_AND_ASSIGN(ObFileTableSpec);
};

/**
 * @brief The operator that reads a local file as if it were a table.
 *
 * The operator never materializes the whole file, it produces one row at a
 * time. A rescan reopens the file and streams it again.
 */
class ObFileTableOp : public ObOperator
{
public:
  ObFileTableOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);
  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int switch_iterator() override;
  virtual int inner_get_next_row() override;
  virtual int inner_close() override;
  virtual void destroy() override { ObOperator::destroy(); }

private:
  int build_options_(ObFileScanOptions &options);
  int build_columns_(common::ObIArray<ObFileColumnDef> &columns);
  int open_reader_();
  int close_reader_();
  // check that the file did not change since its schema was inferred
  int check_file_changed_();
  // infer the columns of the file and build the description of its columns
  int build_describe_values_();
  int calc_next_row();
  int write_column_(const int64_t col_idx, const common::ObObj &obj);

private:
  ObFileScanReader *reader_;
  // the description of the columns of the file, it holds one row of six values
  // per column of the file
  common::ObArray<common::ObObj, common::ModulePageAllocator, true> describe_values_;
  // the number of the rows of the description and the index of the row that
  // is produced
  int64_t describe_row_cnt_;
  int64_t describe_row_idx_;
  DISALLOW_COPY_AND_ASSIGN(ObFileTableOp);
};

} // end namespace sql
} // end namespace oceanbase
