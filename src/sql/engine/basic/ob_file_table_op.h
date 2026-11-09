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

#include "sql/engine/basic/ob_file_scan_utils.h"
#include "sql/engine/ob_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExpr;

class ObFileTableSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(1);
public:
  ObFileTableSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
      column_exprs_(alloc),
      file_path_(alloc),
      format_(FILE_SCAN_FORMAT_CSV),
      has_header_(true),
      field_delim_(','),
      quote_char_('"'),
      escape_char_('"'),
      is_glob_(false),
      is_describe_(false)
  {}
  common::ObFixedArray<ObExpr *, common::ObIAllocator> column_exprs_;
  // The null terminated path of the file, kept serializable on purpose.
  common::ObFixedArray<char, common::ObIAllocator> file_path_;
  int8_t format_;
  bool has_header_;
  char field_delim_;
  char quote_char_;
  char escape_char_;
  bool is_glob_;
  bool is_describe_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObFileTableSpec);
};

class ObFileTableOp : public ObOperator
{
public:
  ObFileTableOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input),
      reader_(NULL),
      allocator_(NULL),
      glob_files_(),
      glob_index_(0),
      describe_columns_(),
      describe_index_(0)
  {}
  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_close() override;
  virtual void destroy() override;
private:
  int open_reader();
  FileScanOptions options() const;
  ObString file_path() const;
  int write_row(const FileScanFields &fields);
  int write_glob_row();
  int write_describe_row();
  int describe_schema();
private:
  FileScanReader *reader_;
  ObIAllocator *allocator_;
  common::ObSEArray<ObString, 8, common::ModulePageAllocator, true> glob_files_;
  int64_t glob_index_;
  common::ObSEArray<FileScanColumn, 16, common::ModulePageAllocator, true> describe_columns_;
  int64_t describe_index_;
  DISALLOW_COPY_AND_ASSIGN(ObFileTableOp);
};

} // namespace sql
} // namespace oceanbase
