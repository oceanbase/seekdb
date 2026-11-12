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

#pragma once

#include "lib/alloc/ob_iallocator.h" // ObIAllocator
#include "lib/ob_errno.h"            // OB_SUCCESS, OB_ERR_UNEXPECTED, ...
#include "lib/string/ob_string.h"    // ObString
#include "lib/utility/ob_macro_utils.h" // OB_ISNULL, OB_UNLIKELY

namespace oceanbase
{
namespace share
{
// Escape backticks in an identifier before it is embedded in quoted DDL SQL.
// Share owns this helper so shared DDL code does not need SQL utilities from
// an upper module.
inline int generate_escaped_identifier_name(
    common::ObIAllocator &allocator,
    const common::ObString &identifier,
    common::ObString &escaped_identifier)
{
  int ret = OB_SUCCESS;
  const common::ObString::obstr_size_t identifier_len = identifier.length();
  common::ObString::obstr_size_t escaped_len = identifier_len;
  const char escape_character = '`';
  char *buffer = nullptr;
  if (OB_ISNULL(identifier.ptr()) || OB_UNLIKELY(0 >= identifier_len)) {
    escaped_identifier.assign(nullptr, 0);
  } else if (nullptr == identifier.find(escape_character)) {
    escaped_identifier = identifier;
  } else {
    for (common::ObString::obstr_size_t i = 0; i < identifier_len; ++i) {
      if (identifier[i] == escape_character) {
        ++escaped_len;
      }
    }
    if (OB_ISNULL(buffer = static_cast<char *>(allocator.alloc(escaped_len + 1)))) {
      escaped_identifier.assign(nullptr, 0);
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      int64_t escaped_pos = 0;
      for (common::ObString::obstr_size_t i = 0; i < identifier_len; ++i) {
        if (identifier[i] == escape_character) {
          buffer[escaped_pos++] = escape_character;
        }
        buffer[escaped_pos++] = identifier[i];
      }
      if (OB_UNLIKELY(escaped_pos != escaped_len)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        buffer[escaped_len] = '\0';
        escaped_identifier.assign_ptr(buffer, escaped_len);
      }
    }
  }
  return ret;
}
} // namespace share
} // namespace oceanbase
