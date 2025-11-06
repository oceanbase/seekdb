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

#ifndef OCEANBASE_LIB_OB_VECTOR_IP_SIMILARITY_H_
#define OCEANBASE_LIB_OB_VECTOR_IP_SIMILARITY_H_

#include "ob_vector_ip_distance.h"

namespace oceanbase
{
namespace common
{
template <typename T>
struct ObVectorIPSimilarity
{
  // vector a and vector b should be normalized before calling ip_similarity_func
  static int ip_similarity_func(const T *a, const T *b, const int64_t len, double &similarity);
  static double get_ip_similarity(double distance);
};

template <typename T>
OB_INLINE double ObVectorIPSimilarity<T>::get_ip_similarity(double distance)
{
  return (1 + distance) / 2;
}

} // common
} // oceanbase
#endif
