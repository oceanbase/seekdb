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

#ifndef OCEANBASE_STORAGE_OB_ROW_RESHAPE
#define OCEANBASE_STORAGE_OB_ROW_RESHAPE

#include "storage/ob_relative_table.h"

namespace oceanbase {
namespace storage {

class ObRowReshape final {
public:
  ObRowReshape();
  ~ObRowReshape() = default;

private:
  DISALLOW_COPY_AND_ASSIGN(ObRowReshape);
  friend class ObRowReshapeUtil;

private:
  int64_t row_reshape_cells_len_;
  common::ObObj *row_reshape_cells_;
  bool char_only_;
  int64_t binary_buffer_len_;
  char *binary_buffer_ptr_;
  // pair: binary column idx in row, binary column len
  common::ObSEArray<std::pair<int32_t, int32_t>, common::OB_ROW_DEFAULT_COLUMNS_COUNT> binary_len_array_;
};

class ObRowReshapeUtil {
public:
  static int need_reshape_table_row(
      const ObNewRow &row,
      ObRowReshape *row_reshape_ins,
      int64_t row_reshape_cells_count,
      ObSQLMode sql_mode,
      bool &need_reshape);
  static int need_reshape_table_row(
      const ObNewRow &row,
      const int64_t column_cnt,
      ObSQLMode sql_mode,
      bool &need_reshape);
  static int reshape_row(
      const ObNewRow &row,
      const int64_t column_cnt,
      ObRowReshape *row_reshape_ins,
      bool need_reshape,
      ObSQLMode sql_mode,
      ObStoreRow &tbl_row);
};

}  // end namespace storage
}  // end namespace oceanbase

#endif  // OCEANBASE_STORAGE_OB_ROW_RESHAPE
