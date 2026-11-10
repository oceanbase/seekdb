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

#include "observer/virtual_table/ob_all_data_type_table.h"

namespace oceanbase
{
using namespace common;

namespace observer
{

ObAllDataTypeTable::ObAllDataTypeTable() :
    ObVirtualTableScannerIterator()
{
}

ObAllDataTypeTable::~ObAllDataTypeTable()
{
}

int ObAllDataTypeTable::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator is NULL", K(ret));
  } else {
    if (!start_to_read_) {
      DatumValue *cells = NULL;
      const int64_t col_count = output_column_ids_.count();
      if (OB_ISNULL(cells = cur_row_.cells_)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "cur row cell is NULL", K(ret));
      } else if (OB_UNLIKELY(col_count < 0 || col_count > DATA_TYPE_COLUMN_COUNT)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "column count error ", K(ret), K(col_count));
      } else if (OB_UNLIKELY(col_count > reserved_column_cnt_)) {
        ret = OB_ERR_UNEXPECTED;
        SERVER_LOG(WARN, "cells count error", K(ret), K(col_count),
                   K(reserved_column_cnt_));
      } else {
        for (SqlValueType type = ObNullType; OB_SUCC(ret) && type < ObMaxType;
             type = static_cast<SqlValueType>(type + 1)) {
          uint64_t cell_idx = 0;
          for (int64_t k = 0; OB_SUCC(ret) && k < col_count; ++k) {
            uint64_t col_id = output_column_ids_.at(k);
            switch (col_id) {
            case DATA_TYPE: {
                (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, type) : ret);
                break;
              }
            case DATA_TYPE_STR: {
                  (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, inner_sql_value_type_to_string(type)) : ret);
                  cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                                         ObCharset::get_default_charset()));
                break;
              }
            case DATA_TYPE_CLASS: {
                  (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_type_class(type)) : ret);
                break;
              }
            default: {
                ret = OB_ERR_UNEXPECTED;
                SERVER_LOG(WARN, "invalid column id", K(ret), K(cell_idx),
                  K(output_column_ids_), K(col_id));
                break;
              }
            }
            if (OB_SUCC(ret)) {
              ++cell_idx;
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(scanner_.add_row(cur_row_))) {
            }
          }
        }
        if (OB_SUCC(ret)) {
          scanner_it_ = scanner_.begin();
          start_to_read_ = true;
        }
      }
    }

    if (OB_SUCCESS == ret && start_to_read_) {
      if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
        if (OB_ITER_END != ret) {
          SERVER_LOG(WARN, "fail to get next row", K(ret));
        }
      } else {
        row = &cur_row_;
      }
    }
  }

  return ret;
}
} // namespace observer
} // namespace oceanbase
