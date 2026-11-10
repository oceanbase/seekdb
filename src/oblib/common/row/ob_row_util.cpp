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

#define USING_LOG_PREFIX COMMON

#include "common/row/ob_row_util.h"
#include "common/cell/ob_cell_reader.h"
#include "common/datum/datum_key_ops.h"

namespace oceanbase
{
namespace common
{

int ObRowUtil::convert(const ObString &compact_row, ObNewRow &row, DatumValue *scalar_slots, int64_t slot_capacity)
{
  return convert(compact_row.ptr(), compact_row.length(), row, scalar_slots, slot_capacity);
}

int ObRowUtil::convert(const char *compact_row, int64_t buf_len, ObNewRow &row, DatumValue *scalar_slots, int64_t slot_capacity)
{
  int ret = OB_SUCCESS;
  ObCellReader cell_reader;
  bool is_row_finished = false;
  uint64_t column_id = OB_INVALID_ID;
  int64_t cell_idx = 0;
  DatumValue cell;
  if (OB_FAIL(cell_reader.init(compact_row, buf_len, DENSE))) {
  }
  while (OB_SUCC(ret) && !is_row_finished && OB_SUCC(cell_reader.next_cell())) {
    if (OB_FAIL(cell_reader.get_cell(column_id, cell, &is_row_finished))) {
    } else if (is_row_finished) {
      ret = OB_SUCCESS;
    } else if (cell_idx < row.count_ && cell_idx < slot_capacity && row.cells_ != nullptr) {
      if (OB_FAIL(DatumValueBuffer::assign_view(cell, scalar_slots, slot_capacity, cell_idx))) {}
      else { row.cells_[cell_idx] = scalar_slots[cell_idx]; ++cell_idx; }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObRowUtil::compare_row(const ObNewRow &lrow, const ObNewRow &rrow, int &cmp)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(lrow.is_invalid()) || OB_UNLIKELY(rrow.is_invalid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lrow or rrow is invalid", K(lrow.is_invalid()), K(rrow.is_invalid()));
  } else {
    int64_t cmp_cnt = lrow.get_count() >= rrow.get_count() ? lrow.get_count() : rrow.get_count();
    int64_t min_cnt = lrow.get_count() < rrow.get_count() ? lrow.get_count() : rrow.get_count();
    for (int64_t i = 0; 0 == cmp && i < cmp_cnt; ++i) {
      if (i < min_cnt) {
        ret = compare_key_value(lrow.get_cell(i), rrow.get_cell(i), cmp);
        if (ret != OB_SUCCESS) { break; }
      } else if (i < lrow.get_count()) {
        cmp = lrow.get_cell(i).is_min_value() ? 0 : 1;
      } else {
        //i < rrow.get_count() && i >= lrow.get_count()
        cmp = rrow.get_cell(i).is_min_value() ? 0 : -1;
      }
    }
  }
  return ret;
}
} // end namespace common
} // end namespace oceanbase
