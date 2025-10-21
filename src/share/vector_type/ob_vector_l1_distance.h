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

#ifndef OCEANBASE_LIB_OB_VECTOR_L1_DISTANCE_H_
#define OCEANBASE_LIB_OB_VECTOR_L1_DISTANCE_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log.h"
#include "lib/ob_define.h"
#include "common/object/ob_obj_compare.h"

namespace oceanbase
{
namespace common
{

template <typename T>
struct ObVectorL1Distance
{
  static int l1_distance_func(const T *a, const T *b, const int64_t len, double &distance);

  // normal func
  OB_INLINE static int l1_distance_normal(const T *a, const T *b, const int64_t len, double &distance);
  // TODO(@jingshui) add simd func
};

template <typename T>
int ObVectorL1Distance<T>::l1_distance_func(const T *a, const T *b, const int64_t len, double &distance)
{
return l1_distance_normal(a, b, len, distance);
}

template <typename T>
OB_INLINE int ObVectorL1Distance<T>::l1_distance_normal(const T *a, const T *b, const int64_t len, double &distance)
{
  int ret = OB_SUCCESS;
  double sum = 0;
  double diff = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < len; ++i) {
    sum += fabs(a[i] - b[i]);
    if (OB_UNLIKELY(0 != ::isinf(sum))) {
      ret = OB_NUMERIC_OVERFLOW;
      LIB_LOG(WARN, "value is overflow", K(ret), K(diff), K(sum));
    }
  }
  if (OB_SUCC(ret)) {
    distance = sum;
  }
  return ret;
}
} // common
} // oceanbase
#endif
