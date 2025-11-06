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

#ifndef OCEANBASE_LIB_OB_VECTOR_L2_SIMILARITY_H_
#define OCEANBASE_LIB_OB_VECTOR_L2_SIMILARITY_H_

#include "ob_vector_l2_distance.h"

namespace oceanbase
{
namespace common
{
template <typename T>
struct ObVectorL2Similarity
{
  // vector a and vector b should be normalized before calling l2_similarity_func
  static int l2_similarity_func(const T *a, const T *b, const int64_t len, double &similarity);
  static double get_l2_similarity(double distance);
};

template <typename T>
OB_INLINE double ObVectorL2Similarity<T>::get_l2_similarity(double distance)
{
  return 1 / (1 + distance);
}

} // common
} // oceanbase
#endif
