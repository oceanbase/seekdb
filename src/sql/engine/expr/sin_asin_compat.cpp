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

#include "sql/engine/expr/sin_asin_compat.h"

#ifdef _WIN32
#include <cfenv>
#include <cstdint>
#include <cstring>
#include <limits>

#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
#include <x86intrin.h>

// Keep the upstream error bounds and explicit FMA semantics in one isolated TU.
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

// Unmodified CORE-MATH sources at commit
// 44bb1678a7b0f0d023380d4745e9bad7b32cf261. Each file retains its MIT license,
// approximation tables, error bounds, and accurate fallback verbatim.
namespace sin_backend
{
#include "sin_core_math.ipp"
static_assert(sizeof(u128) == 16);
#undef U128
#undef roundeven_finite
}
namespace asin_backend
{
#include "asin_core_math.ipp"
#undef MAGIC
#undef roundeven_finite
}
namespace cos_backend
{
#include "cos_core_math.ipp"
#undef U128
#undef roundeven_finite
}

uint64_t magnitude_bits(double x)
{
  uint64_t bits;
  std::memcpy(&bits, &x, sizeof(bits));
  return bits & UINT64_C(0x7fffffffffffffff);
}

bool use_nearest_backend()
{
  // Do not alter the caller's rounding, exception masks, or FTZ/DAZ settings.
  // The supported backend needs nearest rounding and gradual underflow.
  const unsigned csr = _mm_getcsr();
  return (csr & 0xe040U) == 0 && (csr & 0x1f80U) == 0x1f80U
      && std::fegetround() == FE_TONEAREST;
}
} // namespace
#endif

double sin(double x)
{
#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
  const uint64_t ax = magnitude_bits(x);
  // Below 2^-26, the nearest SIN/ASIN value is x. Retain CRT handling of
  // signed zero, tiny-value exception flags, and nonfinite operands.
  if (ax >= UINT64_C(0x3e50000000000000)
      && ax < UINT64_C(0x7ff0000000000000) && use_nearest_backend()) {
    return sin_backend::cr_sin(x);
  }
#endif
  return ::sin(x);
}

double asin(double x)
{
#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
  const uint64_t ax = magnitude_bits(x);
  // Keep CRT endpoint, domain-error, NaN, zero, and tiny-value behavior.
  // SQL's existing out-of-domain-to-NULL check stays at the call site.
  if (ax >= UINT64_C(0x3e50000000000000)
      && ax < UINT64_C(0x3ff0000000000000) && use_nearest_backend()) {
    return asin_backend::cr_asin(x);
  }
#endif
  return ::asin(x);
}

double cos(double x)
{
#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
  const uint64_t ax = magnitude_bits(x);
  // Below 2^-27, cos(x) rounds to 1.0 exactly; keep the CRT handling of signed
  // zero, tiny-value exception flags, and nonfinite operands.
  if (ax >= UINT64_C(0x3e40000000000000)
      && ax < UINT64_C(0x7ff0000000000000) && use_nearest_backend()) {
    return cos_backend::cr_cos(x);
  }
#endif
  return ::cos(x);
}
} // namespace math_compat
} // namespace sql
} // namespace oceanbase
#endif // _WIN32
