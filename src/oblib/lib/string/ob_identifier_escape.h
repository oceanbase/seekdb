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

#include <cstdint>
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace common
{

// Escape the contents of a MySQL backtick-quoted identifier. The caller adds
// the surrounding backticks. buffer_size includes the terminating NUL.
// An exact in-place conversion is supported; other overlapping ranges are not.
inline int escape_mysql_identifier(const char *name,
                                   const int64_t name_length,
                                   char *buffer,
                                   const int64_t buffer_size,
                                   int64_t &escaped_length)
{
  escaped_length = 0;
  if (name_length < 0 || (nullptr == name && name_length != 0)
      || nullptr == buffer || buffer_size <= 0) {
    return OB_INVALID_ARGUMENT;
  }
  int64_t required = 0;
  for (int64_t i = 0; i < name_length; ++i) {
    const int64_t width = '`' == name[i] ? 2 : 1;
    if (width > buffer_size - 1 - required) {
      return OB_BUF_NOT_ENOUGH;
    }
    required += width;
  }
  // Write backwards so that expanding an identifier in place is safe.
  int64_t pos = required;
  buffer[pos] = '\0';
  for (int64_t i = name_length; i > 0; --i) {
    const char ch = name[i - 1];
    buffer[--pos] = ch;
    if ('`' == ch) {
      buffer[--pos] = ch;
    }
  }
  escaped_length = required;
  return OB_SUCCESS;
}

} // namespace common
} // namespace oceanbase
