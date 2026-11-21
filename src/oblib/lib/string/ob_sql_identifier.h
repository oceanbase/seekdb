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
#include <limits>
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace common
{

// Escape the contents of a backtick-quoted MySQL identifier, without adding
// delimiters. Unchanged names borrow src; changed names belong to allocator.
// Accept any allocator exposing alloc(int64_t), as the ObString copy helpers do.
template <typename Allocator>
int escape_mysql_identifier(Allocator &allocator,
                            const char *src,
                            const int32_t src_len,
                            const char *&dst,
                            int32_t &dst_len)
{
  int ret = OB_SUCCESS;
  dst = nullptr;
  dst_len = 0;
  if (nullptr != src && src_len > 0) {
    int64_t escaped_len = src_len;
    for (int32_t i = 0; i < src_len; ++i) {
      escaped_len += ('`' == src[i]);
    }
    if (escaped_len > (std::numeric_limits<int32_t>::max)()) {
      ret = OB_SIZE_OVERFLOW;
    } else if (escaped_len == src_len) {
      dst = src;
      dst_len = src_len;
    } else {
      char *buffer = static_cast<char *>(allocator.alloc(escaped_len + 1));
      if (nullptr == buffer) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        int32_t pos = 0;
        for (int32_t i = 0; i < src_len; ++i) {
          buffer[pos++] = src[i];
          if ('`' == src[i]) {
            buffer[pos++] = '`';
          }
        }
        buffer[pos] = '\0';
        dst = buffer;
        dst_len = pos;
      }
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
