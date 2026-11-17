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
#ifndef OCEANBASE_BASIC_OB_FILE_TABLE_OP_H_
#define OCEANBASE_BASIC_OB_FILE_TABLE_OP_H_

#include "sql/engine/ob_operator.h"
#include "sql/engine/basic/file_sql/file_sql_reader.h"
#include "sql/engine/basic/file_sql/file_sql_scan.h"
#include "oblib/lib/allocator/page_arena.h"

#include <cstdio>
#include <vector>

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
      path_(),
      format_(),
      header_(true),
      sample_size_(-1),
      null_string_(),
      delimiter_(','),
      quote_('"'),
      escape_('"'),
      skip_blank_lines_(false),
      file_format_(0) {}

  // The columns of the file, the operator writes the values into them.
  common::ObFixedArray<ObExpr *, common::ObIAllocator> column_exprs_;
  // The canonical path of the file, it passed the secure_file_priv check when
  // the statement was resolved.
  common::ObString path_;
  common::ObString format_;
  bool header_;
  int64_t sample_size_;
  common::ObString null_string_;
  char delimiter_;
  char quote_;
  char escape_;
  bool skip_blank_lines_;
  int8_t file_format_;
};

class ObFileTableOp : public ObOperator
{
public:
  ObFileTableOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input)
    : ObOperator(exec_ctx, spec, input),
      scanner_(NULL),
      input_(NULL),
      rows_(),
      row_index_(0),
      row_alloc_() {}
  virtual ~ObFileTableOp();
  virtual int inner_open() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_close() override;
  virtual void destroy() override;
private:
  int open_file();
  int close_file();
  int check_schema();
  int fetch_batch();
  int write_row(const file_sql::FileTableRow &row);
private:
  file_sql::FileTableScanner *scanner_;
  FILE *input_;
  std::vector<file_sql::FileTableRow> rows_;
  int64_t row_index_;
  // The text of the string columns is copied here, the allocator is reset
  // when the next batch is read so that the memory does not grow with the
  // size of the file.
  common::ObArenaAllocator row_alloc_;
  DISALLOW_COPY_AND_ASSIGN(ObFileTableOp);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_BASIC_OB_FILE_TABLE_OP_H_
