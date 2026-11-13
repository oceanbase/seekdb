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

#include "rust/math.h"

namespace oceanbase
{
namespace math_compat
{
// The single platform-independent binary64 math layer.
//
// Every platform links the same pinned pure-Rust `libm` implementation, so an
// SQL/geo result computed here is bit-identical on Linux, macOS and Windows.
// Call sites must use this layer instead of the C library whenever the result
// is observable; the platform libm must not leak into SQL or geo output.
//
// The backend is deterministic, but it is not a correctly-rounded
// implementation: results may differ from the platform libm by up to about one
// ulp.  Cross-platform equality, not last-bit optimality, is the contract.
inline double sin(double x) { return oceanbase::math::sin(x); }
inline double cos(double x) { return oceanbase::math::cos(x); }
inline double tan(double x) { return oceanbase::math::tan(x); }
inline double asin(double x) { return oceanbase::math::asin(x); }
inline double acos(double x) { return oceanbase::math::acos(x); }
inline double atan(double x) { return oceanbase::math::atan(x); }
inline double atan2(double y, double x) { return oceanbase::math::atan2(y, x); }
inline double atanh(double x) { return oceanbase::math::atanh(x); }
inline double asinh(double x) { return oceanbase::math::asinh(x); }
inline double acosh(double x) { return oceanbase::math::acosh(x); }
inline double sinh(double x) { return oceanbase::math::sinh(x); }
inline double cosh(double x) { return oceanbase::math::cosh(x); }
inline double tanh(double x) { return oceanbase::math::tanh(x); }
inline double exp(double x) { return oceanbase::math::exp(x); }
inline double exp2(double x) { return oceanbase::math::exp2(x); }
inline double expm1(double x) { return oceanbase::math::expm1(x); }
inline double log(double x) { return oceanbase::math::log(x); }
inline double log2(double x) { return oceanbase::math::log2(x); }
inline double log10(double x) { return oceanbase::math::log10(x); }
inline double log1p(double x) { return oceanbase::math::log1p(x); }
inline double pow(double x, double y) { return oceanbase::math::pow(x, y); }
inline double cbrt(double x) { return oceanbase::math::cbrt(x); }
inline double hypot(double x, double y) { return oceanbase::math::hypot(x, y); }
} // namespace math_compat
} // namespace oceanbase
