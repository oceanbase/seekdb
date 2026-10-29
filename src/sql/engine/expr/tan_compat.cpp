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

#include "sql/engine/expr/tan_compat.h"

#ifdef _WIN32
#include <cfenv>
#include <cstdint>
#include <cinttypes>
#include <cerrno>
#include <limits>

#if defined(__clang__) && __clang_major__ >= 18
// CORE-MATH uses explicit FMA error-free transforms and dynamic rounding.
// Do not contract other expressions or reassociate its error bounds.
#pragma STDC FENV_ACCESS ON
#pragma clang fp contract(off)
#pragma clang fp reassociate(off)
#endif

namespace oceanbase
{
namespace sql
{
namespace math_compat
{
#if defined(__clang__) && __clang_major__ >= 18
namespace
{
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559
              && std::numeric_limits<double>::digits == 53);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

// Unmodified CORE-MATH source at commit
// 44bb1678a7b0f0d023380d4745e9bad7b32cf261, src/binary64/tan/tan.c.
// Its MIT license, tables, explicit FMA operations and accurate fallback are
// retained verbatim. The C union/designator extensions and _BitInt(128) are
// consumed only by the supported Windows Clang frontend in this isolated TU.
#include "tan_core_math.ipp"
static_assert(sizeof(u128) == 16);
#undef CH
#undef CL
} // namespace
#endif

double tan(double x)
{
#if defined(__clang__) && __clang_major__ >= 18
  // Preserve the existing CRT path for directed rounding. Do not change the
  // caller's floating-point environment. Other Windows frontends retain CRT.
  return std::fegetround() == FE_TONEAREST ? cr_tan(x) : ::tan(x);
#else
  return ::tan(x);
#endif
}
} // namespace math_compat
} // namespace sql
} // namespace oceanbase
#endif // _WIN32
