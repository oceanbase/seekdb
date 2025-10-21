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

#define USING_LOG_PREFIX SHARE
#include "share/vector/ob_uniform_base.h"
#include "sql/engine/basic/ob_compact_row.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "share/vector/ob_uniform_format.h"

namespace oceanbase
{
namespace common
{
  int ObUniformBase::to_rows(const sql::RowMeta &row_meta,
                              sql::ObCompactRow **stored_rows,
                              const uint16_t selector[],
                              const int64_t size,
                              const int64_t col_idx) const
  {
    int ret = OB_SUCCESS;
    if (get_format() == VEC_UNIFORM) {
      for (int64_t i = 0; i < size; i++) {
        int64_t row_idx = selector[i];
        if (datums_[row_idx].is_null()) {
          stored_rows[i]->set_null(row_meta, col_idx);
        } else {
          stored_rows[i]->set_cell_payload(row_meta, col_idx,
                                          datums_[row_idx].ptr_,
                                          datums_[row_idx].len_);
        }
      }
    } else {
      int64_t row_idx = 0;
      for (int64_t i = 0; i < size; i++) {
        if (datums_[row_idx].is_null()) {
          stored_rows[i]->set_null(row_meta, col_idx);
        } else {
          stored_rows[i]->set_cell_payload(row_meta, col_idx,
                                          datums_[row_idx].ptr_,
                                          datums_[row_idx].len_);
        }
      }
    }
    return ret;
  }

  int ObUniformBase::to_rows(const sql::RowMeta &row_meta, sql::ObCompactRow **stored_rows,
                              const int64_t size, const int64_t col_idx) const
  {
    int ret = OB_SUCCESS;
    if (get_format() == VEC_UNIFORM) {
      for (int64_t row_idx = 0; row_idx < size; row_idx++) {
        if (datums_[row_idx].is_null()) {
          stored_rows[row_idx]->set_null(row_meta, col_idx);
        } else {
          stored_rows[row_idx]->set_cell_payload(row_meta, col_idx, datums_[row_idx].ptr_,
                                                 datums_[row_idx].len_);
        }
      }
    } else {
      for (int64_t i = 0, row_idx = 0; i < size; i++) {
        if (datums_[row_idx].is_null()) {
          stored_rows[i]->set_null(row_meta, col_idx);
        } else {
          stored_rows[i]->set_cell_payload(row_meta, col_idx, datums_[row_idx].ptr_,
                                           datums_[row_idx].len_);
        }
      }
    }
    return ret;
  }

  DEF_TO_STRING(ObUniformBase)
  {
    int64_t pos = 0;
    J_OBJ_START();
    J_KV(K_(max_row_cnt));
    BUF_PRINTF(", eval_info: ");
    pos += eval_info_->to_string(buf + pos, buf_len - pos);
    J_OBJ_END();
    return pos;
  }

}
}
