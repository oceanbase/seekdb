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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_load_inner_table_schema.h"

namespace oceanbase
{
namespace share
{
int ObLoadInnerTableSchemaInfo::get_row(const int64_t idx, const char *&row, uint64_t &table_id) const
{
  int ret = OB_SUCCESS;
  if (idx < 0 || idx >= row_count_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("idx is out of range", KR(ret), K(idx), K(row_count_));
  } else {
    row = inner_table_rows_[idx];
    table_id = inner_table_table_ids_[idx];
  }
  return ret;
}

}
}

