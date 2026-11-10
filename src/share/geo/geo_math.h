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

#include <cfloat>
#include <cmath>
#include <type_traits>

namespace oceanbase
{
namespace common
{
namespace geometry_math
{

template <typename Y, typename X>
inline auto atan2(const Y &y, const X &x)
{
#ifdef __EMSCRIPTEN__
  static_assert(LDBL_MANT_DIG >= 113);
  if constexpr (std::is_same_v<Y, double> && std::is_same_v<X, double>) {
    return static_cast<double>(::atan2l(static_cast<long double>(y), static_cast<long double>(x)));
  } else
#endif
  {
    using std::atan2;
    return atan2(y, x);
  }
}

template <typename T>
inline auto cos(const T &x)
{
#ifdef __EMSCRIPTEN__
  static_assert(LDBL_MANT_DIG >= 113);
  if constexpr (std::is_same_v<T, double>) {
    return static_cast<double>(::cosl(static_cast<long double>(x)));
  } else
#endif
  {
    using std::cos;
    return cos(x);
  }
}

}
}
}
