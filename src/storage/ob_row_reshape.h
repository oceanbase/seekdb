/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
