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

