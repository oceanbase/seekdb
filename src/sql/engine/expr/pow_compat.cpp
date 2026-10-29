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

#include "pow_compat.h"

#ifdef _WIN32
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
#include <x86intrin.h>
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
namespace pow_backend
{
#include "core_math_pow/pow.ipp"
static_assert(POW_ITERATION == 15, "All upstream power iterations are required");
}
uint64_t encoding(double x)
{
  uint64_t bits;
  std::memcpy(&bits, &x, sizeof(bits));
  return bits;
}
bool positive_normal(double x)
{
  const uint64_t bits = encoding(x);
  const uint64_t exponent = (bits >> 52) & UINT64_C(0x7ff);
  return (bits >> 63) == 0 && exponent > 0 && exponent < 0x7ff;
}
bool finite(double x)
{
  return ((encoding(x) >> 52) & UINT64_C(0x7ff)) != 0x7ff;
}
bool nearest_environment()
{
  const unsigned csr = _mm_getcsr();
  return (csr & 0xe040U) == 0 && (csr & 0x1f80U) == 0x1f80U
      && std::fegetround() == FE_TONEAREST;
}
} // namespace
#endif

double pow(double x, double y, bool &environment_restored)
{
  environment_restored = true;
#if defined(__clang__) && __clang_major__ >= 18 && defined(_M_X64)
  const int caller_errno = errno;
  if (!positive_normal(x) || !finite(y) || !nearest_environment()) {
    errno = caller_errno;
    return ::pow(x, y);
  }
  errno = caller_errno;
  // Evaluate CRT exactly once for its existing exception and errno behavior.
  // Its bits never choose between implementations by an observed ULP delta.
  const double baseline = ::pow(x, y);
  const int baseline_errno = errno;
  if (!positive_normal(baseline)) {
    return baseline;
  }
  std::fenv_t baseline_environment;
  const int saved = std::fegetenv(&baseline_environment);
  errno = baseline_errno;
  if (saved != 0 || !nearest_environment()) {
    errno = baseline_errno;
    return baseline;
  }
  // The algorithm reads the rounding mode, not the caller's sticky flags.
  // Keep all CRT flags during evaluation, and remove only added backend flags.
  bool resolved = true;
  const double value = pow_backend::cr_pow(x, y, resolved);
  const int restored = std::fesetenv(&baseline_environment);
  errno = baseline_errno;
  if (restored != 0) {
    environment_restored = false;
    return baseline;
  }
  return resolved && positive_normal(value) ? value : baseline;
#else
  return ::pow(x, y);
#endif
}
} // namespace math_compat
} // namespace sql
} // namespace oceanbase
#endif
