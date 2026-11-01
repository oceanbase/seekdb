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

#include <cmath>

namespace oceanbase
{
namespace math_compat
{
// Single-owner numerical compatibility layer for sin/asin/cos.
//
// On Windows with Clang 18+ on x86-64 the CORE-MATH backends are used when the
// FP environment is exactly round-to-nearest with no FTZ/DAZ and every FP
// exception masked (i.e. the default MSVC/clang-cl state, which the supported
// backend assumes); every other case (nonfinite/tiny/out-of-range inputs, other
// rounding modes, unmasked exceptions, non-Clang toolchains) keeps the system
// libm behavior. Outside Windows the system libm is used unchanged.
#ifdef _WIN32
double sin(double x);
double asin(double x);
double cos(double x);
#else
inline double sin(double x) { return ::sin(x); }
inline double asin(double x) { return ::asin(x); }
inline double cos(double x) { return ::cos(x); }
#endif
} // namespace math_compat
} // namespace oceanbase
