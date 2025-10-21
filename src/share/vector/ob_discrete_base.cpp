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
#include "share/vector/ob_discrete_base.h"
#include "sql/engine/basic/ob_compact_row.h"
#include "share/vector/ob_discrete_format.h"
#include "sql/engine/expr/ob_array_expr_utils.h"

namespace oceanbase
{
namespace common
{
int ObDiscreteBase::to_rows(const sql::RowMeta &row_meta, sql::ObCompactRow **stored_rows,
                            const uint16_t selector[], const int64_t size,
                            const int64_t col_idx) const
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(!is_collection_expr())) {
    for (int64_t i = 0; i < size; i++) {
      int64_t row_idx = selector[i];
      if (nulls_->at(row_idx)) {
        stored_rows[i]->set_null(row_meta, col_idx);
      } else {
        stored_rows[i]->set_cell_payload(row_meta, col_idx, ptrs_[row_idx], lens_[row_idx]);
      }
    }
  } else {
    ret = sql::ObCollectionExprUtil::write_collections_to_rows(
      static_cast<const ObDiscreteFormat *>(this), row_meta, stored_rows, selector, size, col_idx);
  }
  return ret;
}

int ObDiscreteBase::to_rows(const sql::RowMeta &row_meta, sql::ObCompactRow **stored_rows,
                             const int64_t size, const int64_t col_idx) const
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(!is_collection_expr())) {
    for (int64_t row_idx = 0; row_idx < size; row_idx++) {
      if (nulls_->at(row_idx)) {
        stored_rows[row_idx]->set_null(row_meta, col_idx);
      } else {
        stored_rows[row_idx]->set_cell_payload(row_meta, col_idx, ptrs_[row_idx], lens_[row_idx]);
      }
    }
  } else {
    ret = sql::ObCollectionExprUtil::write_collections_to_rows(
      static_cast<const ObDiscreteFormat *>(this), row_meta, stored_rows, size, col_idx);
  }
  return ret;
}

  DEF_TO_STRING(ObDiscreteBase)
  {
    int64_t pos = 0;
    J_OBJ_START();
    J_KV(K_(has_null), K_(is_batch_ascii), K_(max_row_cnt));
    J_OBJ_END();
    return pos;
  }
}
}
