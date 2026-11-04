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

#ifndef OCEANBASE_SQL_ENGINE_OB_FILE_TABLE_ITERATOR_H_
#define OCEANBASE_SQL_ENGINE_OB_FILE_TABLE_ITERATOR_H_

#include "common/row/ob_row_iterator.h"
#include "sql/engine/expr/ob_expr_file_sql/ob_csv_parser.h"
#include "sql/engine/expr/ob_expr_file_sql/ob_file_reader.h"
#include "lib/container/ob_se_array.h"
#include "common/row/ob_row.h"

namespace oceanbase
{
namespace sql
{

class ObFileTableIterator : public common::ObNewRowIterator
{
public:
  ObFileTableIterator(common::ObIAllocator& alloc);
  virtual ~ObFileTableIterator();

  // Initialize iterator with file path and options
  int init(const common::ObString& file_path,
           const CSVOptions& csv_opts,
           common::ObIAllocator& allocator);

  // Iterator interface
  virtual int get_next_row(common::ObNewRow*& row) override;
  virtual void reset();
  virtual void reuse();
  virtual void close();

  // Get column information
  int get_column_count() const { return column_count_; }
  int get_column_name(int64_t idx, common::ObString& name) const;
  int get_column_type(int64_t idx, common::ObObjType& type) const;

private:
  // Parse current row into column values
  int parse_current_row();

  // Convert string value to appropriate type
  int convert_value(const common::ObString& str_val,
                    common::ObObjType target_type,
                    common::ObObj& obj);

private:
  common::ObIAllocator& allocator_;
  common::ObString file_path_;
  CSVOptions csv_opts_;

  // File content and parsed data
  common::ObString file_content_;
  common::ObSEArray<common::ObString, 16> headers_;
  common::ObSEArray<common::ObString, 16> data_rows_;
  common::ObSEArray<common::ObObjType, 16> column_types_;

  // Current position
  int64_t current_row_idx_;
  int64_t column_count_;

  // Current row data
  common::ObNewRow current_row_;
  common::ObSEArray<common::ObObj, 16> row_cells_;

  DISALLOW_COPY_AND_ASSIGN(ObFileTableIterator);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_OB_FILE_TABLE_ITERATOR_H_
