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
#include <string_view>

namespace oceanbase { namespace share {
// Append a MySQL identifier without depending on SQL-layer utilities. The sink
// returns zero on success; propagate its first error without further writes.
template <typename Append>
int append_quoted_identifier(std::string_view name, std::string_view prefix, Append append)
{
  int ret = append("`", 1);
  if (ret == 0 && !prefix.empty()) { ret = append(prefix.data(), prefix.size()); }
  size_t begin = 0;
  for (size_t i = 0; ret == 0 && i < name.size(); ++i) {
    if (name[i] == '`') {
      if (i > begin) { ret = append(name.data() + begin, i - begin); }
      if (ret == 0) { ret = append("``", 2); }
      begin = i + 1;
    }
  }
  if (ret == 0 && begin < name.size()) { ret = append(name.data() + begin, name.size() - begin); }
  if (ret == 0) { ret = append("`", 1); }
  return ret;
}
} }
