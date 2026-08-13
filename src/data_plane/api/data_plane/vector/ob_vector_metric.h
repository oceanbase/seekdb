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

#ifndef OCEANBASE_DATA_PLANE_API_OB_VECTOR_METRIC_H_
#define OCEANBASE_DATA_PLANE_API_OB_VECTOR_METRIC_H_

#include "data_plane/vector/ob_vector_cosine_distance.h"
#include "data_plane/vector/ob_vector_ip_distance.h"
#include "data_plane/vector/ob_vector_l1_distance.h"
#include "data_plane/vector/ob_vector_l2_distance.h"

namespace oceanbase
{
namespace share
{

enum class ObVectorDistanceType
{
  COSINE = 0,
  DOT,
  EUCLIDEAN,
  MANHATTAN,
  EUCLIDEAN_SQUARED,
  HAMMING,
  MAX_TYPE,
};

template <typename T = float>
struct ObVectorDistanceDispatch
{
  using FuncPtrType = int (*)(const T *a, const T *b, const int64_t len, double &distance);
  static FuncPtrType distance_funcs[];
};

template <typename T>
typename ObVectorDistanceDispatch<T>::FuncPtrType ObVectorDistanceDispatch<T>::distance_funcs[] =
{
  common::ObVectorCosineDistance<T>::cosine_distance_func,
  common::ObVectorIpDistance<T>::ip_distance_func,
  common::ObVectorL2Distance<T>::l2_distance_func,
  common::ObVectorL1Distance<T>::l1_distance_func,
  common::ObVectorL2Distance<T>::l2_square_func,
  nullptr,
};

inline int vector_similarity_from_distance(const ObVectorDistanceType type,
                                           const float distance,
                                           float &similarity)
{
  int ret = common::OB_SUCCESS;
  switch (type) {
    case ObVectorDistanceType::EUCLIDEAN:
      similarity = 1 / (1 + distance * distance);
      break;
    case ObVectorDistanceType::DOT:
      similarity = (1 + distance) / 2;
      break;
    case ObVectorDistanceType::COSINE:
      similarity = (2 - distance) / 2;
      break;
    default:
      ret = common::OB_NOT_SUPPORTED;
      break;
  }
  return ret;
}

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_VECTOR_METRIC_H_
