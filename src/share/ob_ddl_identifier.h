/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdint.h>
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace share
{

// SqlBuffer follows ObSqlString's append interface. Keep identifier formatting
// in Share, without depending on the SQL resolver or its allocator helpers.
template <typename SqlBuffer>
int append_ddl_column_identifier(SqlBuffer &sql, const char *name,
                                 const int64_t length, const bool shadow = false)
{
  int ret = common::OB_SUCCESS;
  if (length < 0 || (length > 0 && nullptr == name)) {
    ret = common::OB_INVALID_ARGUMENT;
  } else {
    ret = sql.append(shadow ? "`__SHADOW_" : "`");
    int64_t start = 0;
    for (int64_t i = 0; common::OB_SUCCESS == ret && i < length; ++i) {
      if ('`' == name[i]) {
        if (i > start) {
          ret = sql.append(name + start, i - start);
        }
        if (common::OB_SUCCESS == ret) {
          ret = sql.append("``");
        }
        start = i + 1;
      }
    }
    if (common::OB_SUCCESS == ret && start < length) {
      ret = sql.append(name + start, length - start);
    }
    if (common::OB_SUCCESS == ret) {
      ret = sql.append("`");
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
