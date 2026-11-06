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

#include <cmath>
#include <cstdint>

namespace oceanbase
{
namespace common
{

// Deterministic binary64 reference for the float L2 square kernel, i.e. for
// sum((a[i] - b[i])^2) over the whole vector.
//
// ObVectorL2Distance<float>::l2_square_func() normally uses the portable
// `simsimd` float32 SIMD kernel (Rust bridge, runtime ISA dispatch). This
// binary64 function is the fallback for inputs whose float32 lane arithmetic
// overflows or underflows (inf, zero, subnormal result), where it produces the
// finite, correctly ranged value. It is also the reference for tests. Three
// properties make its result independent of the platform:
//
//   * both operands are converted to binary64 before the subtraction (the
//     conversion is exact, and a difference of two binary32 values is exactly
//     representable in binary64), and every intermediate is binary64;
//   * the terms are added in increasing index order, with one rounding point
//     per term - no reassociation, no tree/multi-accumulator reduction;
//   * floating-point contraction is disabled around this function. A compiler
//     may otherwise fuse `square += diff * diff` into an FMA, which rounds once
//     instead of twice; x86-64 has no baseline FMA while AArch64 does, so the
//     fused value would be target dependent.
//
// The formula is also in range over the whole finite-float32 input domain: with
// binary64 intermediates the worst case (len up to INT64_MAX, |a[i]-b[i]| up to
// 2 * FLT_MAX) stays below 4.3e96, far under DBL_MAX, and the smallest non-zero
// square, (FLT_TRUE_MIN)^2 = 1.96e-90, is a normal binary64. A float32 lane
// accumulator cannot do this: a square below FLT_MIN loses mantissa bits,
// squares below FLT_TRUE_MIN flush to zero and a lane sum above FLT_MAX
// saturates to infinity.
//
// The result is not clamped and can be +inf or nan only when the input vector
// contains a non-finite value; the caller reports OB_NUMERIC_OVERFLOW for an
// infinite square.
//
// Precision note: the returned value is the correctly rounded binary64 sum of
// correctly rounded per-term squares in index order. It is not the exact
// mathematical sum and not necessarily the same value as an extended-precision
// (e.g. 80-bit long double) summation would give; it is a reproducible value,
// which is the contract this function exists to provide.
//
// Clang (including clang-cl) supports the standard contract pragma inside a
// function body. GCC only has its own function-scoped optimize pragma, so the
// definition is wrapped in push_options/optimize/pop_options there; the repo
// toolchains are clang on macOS/Linux/Windows and never take that branch. The
// two pragma forms are the ones the vendored S2 copy uses (s2/_fp_contract_off.h),
// which is the in-tree precedent for suppressing contraction portably.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif
inline double l2_square_float_reference(const float *a, const float *b, const int64_t len)
{
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
  double square = 0;
  for (int64_t i = 0; i < len; ++i) {
    const double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
    square += diff * diff;
  }
  return square;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif

}  // namespace common
}  // namespace oceanbase
