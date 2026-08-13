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

#ifndef OCEANBASE_QUERY_API_ENGINE_EXPR_OB_ARITHMETIC_OVERFLOW_H_
#define OCEANBASE_QUERY_API_ENGINE_EXPR_OB_ARITHMETIC_OVERFLOW_H_

#include <stdint.h>

namespace oceanbase
{
namespace query
{

struct ObArithmeticOverflow
{
  static constexpr int64_t SHIFT_OFFSET = 63;

  static inline bool is_int_add_out_of_range(
      const int64_t lhs, const int64_t rhs, const int64_t result)
  {
    return (lhs >> SHIFT_OFFSET) != (result >> SHIFT_OFFSET)
        && (rhs >> SHIFT_OFFSET) != (result >> SHIFT_OFFSET);
  }

  static inline bool is_uint_add_out_of_range(
      const uint64_t lhs, const uint64_t rhs, const uint64_t result)
  {
    return (lhs >> SHIFT_OFFSET) + (rhs >> SHIFT_OFFSET)
        > (result >> SHIFT_OFFSET);
  }
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_EXPR_OB_ARITHMETIC_OVERFLOW_H_
