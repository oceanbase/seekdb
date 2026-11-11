/*
 * Copyright (c) 2026 OceanBase.
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
#ifndef OCEANBASE_SHARE_DDL_QUOTED_IDENTIFIER_H_
#define OCEANBASE_SHARE_DDL_QUOTED_IDENTIFIER_H_

#include <cstddef>

namespace oceanbase
{
namespace share
{
// Format a MySQL backtick-quoted identifier without depending on the SQL layer.
// Embedded backticks are doubled; the input is a byte span, not a C string.
// append(data, len) returns 0 on success, or an error code on failure.
template <typename Append>
int append_ddl_quoted_identifier(const char *name, size_t length, bool shadow, Append append)
{
  int ret = append("`", 1);
  if (0 == ret && shadow) {
    ret = append("__SHADOW_", sizeof("__SHADOW_") - 1);
  }

  size_t begin = 0;
  for (size_t i = 0; 0 == ret && i < length; ++i) {
    if ('`' == name[i]) {
      if (i > begin) {
        ret = append(name + begin, i - begin);
      }
      if (0 == ret) {
        ret = append("``", 2);
      }
      begin = i + 1;
    }
  }
  if (0 == ret && begin < length) {
    ret = append(name + begin, length - begin);
  }
  if (0 == ret) {
    ret = append("`", 1);
  }
  return ret;
}
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_DDL_QUOTED_IDENTIFIER_H_
