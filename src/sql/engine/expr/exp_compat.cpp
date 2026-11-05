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

#include "sql/engine/expr/exp_compat.h"

#ifdef _WIN32
#include <cfenv>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
#include <x86intrin.h>

// Preserve the upstream error bounds and explicit FMA operations in this TU.
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
#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
namespace
{
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559
              && std::numeric_limits<double>::digits == 53);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

// Unmodified CORE-MATH exp.c, commit
// 44bb1678a7b0f0d023380d4745e9bad7b32cf261, with its MIT license,
// approximation tables, error bounds, and accurate fallback retained verbatim.
namespace exp_backend
{
#include "exp_core_math.ipp"
#undef roundeven_finite
}

bool use_nearest_backend()
{
  // Read the caller's environment without changing control or sticky bits.
  const unsigned csr = _mm_getcsr();
  return (csr & 0xe040U) == 0 && (csr & 0x1f80U) == 0x1f80U
      && std::fegetround() == FE_TONEAREST;
}
} // namespace
#endif

double exp(double x)
{
#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
  uint64_t bits;
  std::memcpy(&bits, &x, sizeof(bits));
  const uint64_t ax = bits & UINT64_C(0x7fffffffffffffff);
  // Use the upstream algorithm's own tiny, normal/subnormal, and overflow
  // boundaries. CRT retains signed zero, tiny inputs, nonfinite operands,
  // subnormal results, overflow, errno, and their existing exception behavior.
  const uint64_t upper = (bits >> 63) != 0
      ? UINT64_C(0x4086232bdd7abcd2) : UINT64_C(0x40862e42fefa39f0);
  if (ax > UINT64_C(0x3c90000000000000) && ax < upper && use_nearest_backend()) {
    return exp_backend::cr_exp(x);
  }
#endif
  return ::exp(x);
}
} // namespace math_compat
} // namespace sql
} // namespace oceanbase
#endif // _WIN32
