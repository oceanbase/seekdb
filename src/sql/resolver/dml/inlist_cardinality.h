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

namespace oceanbase
{
namespace sql
{

// A bounded, allocation-free check for duplicate-heavy scalar integer lists.
// Returning false only declines this optimization; the normal rewrite remains
// available for unsupported values or lists with many distinct integers.
template <typename IntegerReader>
bool has_small_integer_inlist(const int64_t row_count,
                             const int64_t rewrite_threshold,
                             IntegerReader read_integer)
{
  constexpr int64_t MAX_DISTINCT_VALUES = 64;
  int64_t values[MAX_DISTINCT_VALUES];
  int64_t distinct_count = 0;
  if (rewrite_threshold <= 1 || row_count < rewrite_threshold) {
    return false;
  }
  for (int64_t i = 0; i < row_count; ++i) {
    int64_t value = 0;
    if (!read_integer(i, value)) {
      return false;
    }
    int64_t j = 0;
    while (j < distinct_count && values[j] != value) {
      ++j;
    }
    if (j == distinct_count) {
      if (distinct_count == MAX_DISTINCT_VALUES || distinct_count + 1 >= rewrite_threshold) {
        return false;
      }
      values[distinct_count++] = value;
    }
  }
  return distinct_count > 0 && distinct_count < row_count;
}

}  // namespace sql
}  // namespace oceanbase
