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

#define USING_LOG_PREFIX STORAGE

#include "storage/ob_row_reshape.h"
#include "common/sql_mode/ob_sql_mode_utils.h"

namespace oceanbase {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace storage {

ObRowReshape::ObRowReshape()
  : row_reshape_cells_len_(0),
    row_reshape_cells_(nullptr),
    char_only_(false),
    binary_buffer_len_(0),
    binary_buffer_ptr_(nullptr),
    binary_len_array_()
{
}




int ObRowReshapeUtil::need_reshape_table_row(
    const ObNewRow &row,
    ObRowReshape *row_reshape_ins,
    int64_t row_reshape_cells_count,
    ObSQLMode sql_mode,
    bool &need_reshape)
{
  int ret = OB_SUCCESS;
  need_reshape = false;
  if (!row.is_valid() || row_reshape_cells_count != row.get_count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(row), K(row.count_), K(row_reshape_cells_count), K(ret));
  } else {
    if (NULL == row_reshape_ins) {
      // do not need reshape
    } else if (row_reshape_ins->char_only_) {
      if (OB_FAIL(need_reshape_table_row(row, row.get_count(), sql_mode, need_reshape))) {
        LOG_WARN("failed to check need reshape row", K(ret), K(row), K(sql_mode));
      }
    } else {
      need_reshape = true;  // with binary, we do not check it
    }
  }
  return ret;
}

int ObRowReshapeUtil::need_reshape_table_row(
    const ObNewRow &row,
    const int64_t column_cnt,
    ObSQLMode sql_mode,
    bool &need_reshape)
{
  int ret = OB_SUCCESS;
  need_reshape = false;
  if (!row.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(row), K(row.count_));
  } else {
    ObString space_pattern;
    for (int64_t i = 0; !need_reshape && i < column_cnt; ++i) {
      const ObObj &cell = row.get_cell(i);
      if (cell.is_fixed_len_char_type()) {
        space_pattern = ObCharsetUtils::get_const_str(cell.get_collation_type(), ' ');
      }
      if (cell.is_fixed_len_char_type() && cell.get_string_len() >= space_pattern.length() &&
          0 == MEMCMP(cell.get_string_ptr() + cell.get_string_len() - space_pattern.length(),
                   space_pattern.ptr(),
                   space_pattern.length())) {
        need_reshape = true;
      } else if (is_oracle_compatible(sql_mode) && cell.is_character_type() && cell.get_string_len() == 0) {
        // Oracle compatibility mode: '' as null
        need_reshape = true;
        LOG_DEBUG("Pstor2", K(cell), K(cell.get_string()), K(need_reshape));
      } else if (cell.is_binary()) {
        need_reshape = true;
      }
    }
  }
  return ret;
}

int ObRowReshapeUtil::reshape_row(
    const ObNewRow &row,
    const int64_t column_cnt,
    ObRowReshape *row_reshape_ins,
    bool need_reshape,
    ObSQLMode sql_mode,
    ObStoreRow &tbl_row)
{
  int ret = OB_SUCCESS;
  if (column_cnt > row.get_count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("column cnt can not be larger than row column cnt", K(ret), K(column_cnt), K(row));
  } else if (!need_reshape) {
    tbl_row.row_val_ = row;
  } else if (OB_ISNULL(row_reshape_ins)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null reshape ptr", K(ret));
  } else if (OB_UNLIKELY(column_cnt > row_reshape_ins->row_reshape_cells_len_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid column count", K(ret), K(column_cnt), K(row_reshape_ins->row_reshape_cells_len_));
  } else {
    int64_t binary_len_array_count = row_reshape_ins->binary_len_array_.count();
    ObDataBuffer data_buffer(row_reshape_ins->binary_buffer_ptr_, row_reshape_ins->binary_buffer_len_);
    for (int64_t i = 0, j = 0; OB_SUCC(ret) && i < column_cnt; ++i) {
      const ObObj &cell = row.get_cell(i);
      if (cell.is_binary()) {
        for (; j < binary_len_array_count; j++) {
          if (row_reshape_ins->binary_len_array_.at(j).first == i) {
            break;
          }
        }
        if (OB_UNLIKELY(binary_len_array_count <= j)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid argument", K(binary_len_array_count), K(ret));
        } else {
          const char *str = cell.get_string_ptr();
          const int32_t len = cell.get_string_len();
          char *dest_str = NULL;
          const int32_t binary_len = row_reshape_ins->binary_len_array_.at(j++).second;
          if (binary_len > len) {
            if (OB_ISNULL(dest_str = (char *)(data_buffer.alloc(binary_len)))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_ERROR("fail to alloc mem to binary", K(ret), K(i), K(j), K(binary_len));
            } else {
              char pad_char = '\0';
              MEMCPY(dest_str, str, len);
              MEMSET(dest_str + len, pad_char, binary_len - len);
            }
          } else if (binary_len == len) {
            dest_str = const_cast<char *>(str);
          } else if (binary_len < len) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("binary_len should be greater than len", K(ret), K(binary_len), K(len));
          }
          if (OB_SUCC(ret)) {
            // set_binary set both type_ and cs_type
            row_reshape_ins->row_reshape_cells_[i].set_binary(ObString(binary_len, dest_str));
          }
        }
      } else if (is_oracle_compatible(sql_mode) && cell.is_character_type() && cell.get_string_len() == 0) {
        // Oracle compatibility mode: '' as null
        LOG_DEBUG("reshape empty string to null", K(cell));
        row_reshape_ins->row_reshape_cells_[i].set_null();
      } else if (cell.is_fixed_len_char_type()) {
        const char *str = cell.get_string_ptr();
        int32_t len = cell.get_string_len();
        ObString space_pattern = ObCharsetUtils::get_const_str(cell.get_collation_type(), ' ');
        for (; len >= space_pattern.length(); len -= space_pattern.length()) {
          if (0 != MEMCMP(str + len - space_pattern.length(), space_pattern.ptr(), space_pattern.length())) {
            break;
          }
        }
        // need to set collation type
        row_reshape_ins->row_reshape_cells_[i].set_string(cell.get_type(), ObString(len, str));
        row_reshape_ins->row_reshape_cells_[i].set_collation_type(cell.get_collation_type());
      } else {
        row_reshape_ins->row_reshape_cells_[i] = cell;
      }
    }
    tbl_row.row_val_.cells_ = row_reshape_ins->row_reshape_cells_;
    tbl_row.row_val_.count_ = column_cnt;
  }
  return ret;
}


}  // end namespace storage
}  // end namespace oceanbase
