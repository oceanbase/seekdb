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
 
#ifndef DEPS_OBLIB_SRC_COMMON_META_PROGRAMMING_OB_META_COMPARE_H
#define DEPS_OBLIB_SRC_COMMON_META_PROGRAMMING_OB_META_COMPARE_H

#include "lib/ob_define.h"
#include "ob_type_traits.h"

namespace oceanbase
{
namespace common
{
namespace meta
{

template <typename T, typename std::enable_if<OB_TRAIT_IS_ORIGIN_COMPAREABLE(T), bool>::type = true>
int compare(const T &lhs, const T&rhs, int &result)
{
  int ret = OB_SUCCESS;
  result = 0;
  if (lhs < rhs) {
    result = -1;
  } else if (lhs == rhs) {
    result = 0;
  } else {
    result = 1;
  }
  return ret;
}

template <typename T,
          typename std::enable_if<!OB_TRAIT_IS_ORIGIN_COMPAREABLE(T) &&\
                                   OB_TRAIT_IS_METHOD_COMPAREABLE(T), bool>::type = true>
int compare(const T &lhs, const T&rhs, int &result)
{
  return lhs.compare(rhs, result);
}

template <typename T,
          typename std::enable_if<!OB_TRAIT_IS_ORIGIN_COMPAREABLE(T) &&\
                                  !OB_TRAIT_IS_METHOD_COMPAREABLE(T), bool>::type = true>
int compare(const T &lhs, const T&rhs, int &result)
{
  static_assert(!(!OB_TRAIT_IS_ORIGIN_COMPAREABLE(T) &&
                !OB_TRAIT_IS_METHOD_COMPAREABLE(T)),
                "your type NEITHER has opertor< and operator== "
                "NOR has int T::comapre(cosnt T&)");
  return OB_NOT_SUPPORTED;
}

}
}
}
#endif
