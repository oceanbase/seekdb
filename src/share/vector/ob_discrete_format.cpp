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

#include "ob_discrete_format.h"
#include "sql/engine/expr/ob_array_expr_utils.h"

namespace oceanbase
{
namespace common
{
using namespace sql;
using ATTR0_FMT = ObFixedLengthFormat<RTCType<VEC_TC_INTEGER>>;

void ObDiscreteFormat::set_collection_payload_shallow(const int64_t idx, const void *payload, const ObLength length)
{
  if (ObCollectionExprUtil::is_compact_fmt_cell(payload)) {
    set_has_compact_collection();
  }
}

int ObDiscreteFormat::write_collection_to_row(const sql::RowMeta &row_meta,
                                              sql::ObCompactRow *stored_row, const uint64_t row_idx,
                                              const int64_t col_idx) const
{
  return ObCollectionExprUtil::write_collection_to_row(this, row_meta, stored_row, row_idx, col_idx);
}

int ObDiscreteFormat::write_collection_to_row(const sql::RowMeta &row_meta,
                                              sql::ObCompactRow *stored_row, const uint64_t row_idx,
                                              const int64_t col_idx, const int64_t remain_size,
                                              const bool is_fixed_length_data, int64_t &row_size) const
{
  return ObCollectionExprUtil::write_collection_to_row(this, row_meta, stored_row, row_idx, col_idx,
                                                       remain_size, is_fixed_length_data, row_size);
}
} // end common
} // end oceanbase
