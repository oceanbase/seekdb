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

#define USING_LOG_PREFIX SQL_RESV

#include "sql/resolver/dml/ob_file_table_def.h"

namespace oceanbase
{
namespace sql
{

const char *const OB_FILE_TABLE_DESCRIBE_COLUMNS[6] = {
  "column_name", "column_type", "null", "key", "default", "extra"
};

int ObFileTableDef::deep_copy(const ObFileTableDef &other, common::ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    options_ = other.options_;
    if (OB_FAIL(ob_file_copy_string(other.options_.path_, *allocator, options_.path_))) {
    } else if (OB_FAIL(ob_file_copy_string(other.options_.delimiter_, *allocator, options_.delimiter_))) {
    } else if (OB_FAIL(ob_file_copy_string(other.options_.quote_, *allocator, options_.quote_))) {
    } else if (OB_FAIL(ob_file_copy_string(other.options_.escape_, *allocator, options_.escape_))) {
    } else if (OB_FAIL(ob_file_copy_string(other.options_.null_str_, *allocator, options_.null_str_))) {
    } else if (OB_FAIL(ob_file_copy_string(other.options_.json_format_, *allocator, options_.json_format_))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < other.columns_.count(); ++i) {
        ObFileColumnDef column = other.columns_.at(i);
        if (OB_FAIL(ob_file_copy_string(other.columns_.at(i).name_, *allocator, column.name_))) {
        } else if (OB_FAIL(columns_.push_back(column))) {
          LOG_WARN("cannot copy the column of the file table", K(ret));
        }
      }
      row_cnt_ = other.row_cnt_;
      describe_ = other.describe_;
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
