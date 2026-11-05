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

#pragma once

#include <cstdint>
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace share
{

// SqlBuffer supplies append(const char *, int64_t), as ObSqlString does.
// Keep DDL identifier quoting in Share rather than depending on SQL utilities.
template <typename SqlBuffer>
int append_ddl_identifier(SqlBuffer &sql, const char *name, const int64_t length,
                          const bool is_shadow = false)
{
  int ret = common::OB_SUCCESS;
  if (length < 0 || (length > 0 && nullptr == name)) {
    ret = common::OB_INVALID_ARGUMENT;
  } else {
    static constexpr char SHADOW_PREFIX[] = "`__SHADOW_";
    ret = is_shadow ? sql.append(SHADOW_PREFIX, sizeof(SHADOW_PREFIX) - 1)
                    : sql.append("`", 1);
    int64_t begin = 0;
    for (int64_t i = 0; common::OB_SUCCESS == ret && i < length; ++i) {
      if ('`' == name[i]) {
        ret = sql.append(name + begin, i - begin + 1);
        if (common::OB_SUCCESS == ret) {
          ret = sql.append("`", 1);
        }
        begin = i + 1;
      }
    }
    if (common::OB_SUCCESS == ret && begin < length) {
      ret = sql.append(name + begin, length - begin);
    }
    if (common::OB_SUCCESS == ret) {
      ret = sql.append("`", 1);
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
