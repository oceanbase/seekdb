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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace oceanbase
{
namespace common
{

inline int ob_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
#if defined(__EMSCRIPTEN__) && __SIZEOF_LONG__ == 4
  size_t extra = 0;
  bool conversion = false;
  for (const char *src = fmt; nullptr != src && '\0' != *src; ++src) {
    if (!conversion) {
      conversion = '%' == *src;
    } else if ('l' == *src && 'l' != src[-1] && '\0' != src[1]
        && nullptr != strchr("diouxX", src[1])) {
      ++extra;
    } else if (nullptr != strchr("diouxXfFeEgGaAcCsSpnm%", *src)) {
      conversion = false;
    }
  }
  if (0 == extra) {
    return vsnprintf(buf, size, fmt, args);
  }
  const size_t length = strlen(fmt);
  if (extra >= static_cast<size_t>(-1) - length) {
    errno = EOVERFLOW;
    return -1;
  }
  char local_fmt[4096];
  const size_t capacity = length + extra + 1;
  char *actual_fmt = capacity <= sizeof(local_fmt)
      ? local_fmt : static_cast<char *>(malloc(capacity));
  if (nullptr == actual_fmt) {
    errno = ENOMEM;
    return -1;
  }
  char *dst = actual_fmt;
  conversion = false;
  for (const char *src = fmt; '\0' != *src; ++src) {
    if (!conversion) {
      conversion = '%' == *src;
    } else if ('l' == *src && 'l' != src[-1] && '\0' != src[1]
        && nullptr != strchr("diouxX", src[1])) {
      *dst++ = 'l';
    } else if (nullptr != strchr("diouxXfFeEgGaAcCsSpnm%", *src)) {
      conversion = false;
    }
    *dst++ = *src;
  }
  *dst = '\0';
  const int result = vsnprintf(buf, size, actual_fmt, args);
  if (actual_fmt != local_fmt) {
    free(actual_fmt);
  }
  return result;
#else
  return vsnprintf(buf, size, fmt, args);
#endif
}

}
}
