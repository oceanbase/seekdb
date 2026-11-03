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

#include <stdint.h>

namespace oceanbase
{
namespace common
{

inline void format_i64(const int64_t value, int16_t &check_sum)
{
  int i = 0;
  while (i < 4) {
    check_sum = static_cast<int16_t>(check_sum ^ ((value >> i * 16) & 0xFFFF));
    ++i;
  }
}

inline void format_i32(const int32_t value, int16_t &check_sum)
{
  int i = 0;
  while (i < 2) {
    check_sum = static_cast<int16_t>(check_sum ^ ((value >> i * 16) & 0xFFFF));
    ++i;
  }
}

}
}
