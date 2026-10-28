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

#include <stdarg.h>
#include <stdio.h>

namespace oceanbase
{
namespace common
{

// Format strings in SeekDB carry 64-bit values through the <inttypes.h> PRI
// macros (PRId64/PRIu64/...), so %ld always means the native long everywhere.
// This used to rewrite %ld to %lld on LLP64 targets; that transitional shim is
// gone now that every 64-bit call site is portable.
inline int ob_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
    __attribute__((format(printf, 3, 0)));

inline int ob_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
  return vsnprintf(buf, size, fmt, args);
}

}
}
