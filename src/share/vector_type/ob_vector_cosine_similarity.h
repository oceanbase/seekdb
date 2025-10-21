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

#ifndef OCEANBASE_LIB_OB_VECTOR_COSINE_SIMILARITY_H_
#define OCEANBASE_LIB_OB_VECTOR_COSINE_SIMILARITY_H_

#include "ob_vector_ip_distance.h"

namespace oceanbase
{
namespace common
{
template <typename T>
struct ObVectorCosineSimilarity
{
  static int cosine_similarity_func(const T *a, const T *b, const int64_t len, double &similarity);
  static double get_cosine_similarity(double distance);
};

template <typename T>
OB_INLINE double ObVectorCosineSimilarity<T>::get_cosine_similarity(double distance)
{
  if (distance > 1.0) {
    distance = 1.0;
  } else if (distance < -1.0) {
    distance = -1.0;
  }
  return (1 + distance) / 2;
}

} // common
} // oceanbase
#endif
