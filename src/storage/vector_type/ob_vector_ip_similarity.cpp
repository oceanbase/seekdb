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

#include "ob_vector_ip_similarity.h"
namespace oceanbase
{
namespace common
{
template <>
int ObVectorIPSimilarity<float>::ip_similarity_func(const float *a, const float *b, const int64_t len, double &similarity)
{
  int ret = OB_SUCCESS;
  double distance = 0;
  bool is_norm = true;
  if (OB_FAIL(ObVectorIpDistance<float>::ip_distance_func(a, b, len, distance))) {
    if (OB_ERR_NULL_VALUE != ret) {
      LIB_LOG(WARN, "failed to cal ip distance", K(ret));
    }
  } else {
    similarity = get_ip_similarity(distance);
  }
  return ret;
}

template <>
int ObVectorIPSimilarity<uint8_t>::ip_similarity_func(const uint8_t *a, const uint8_t *b, const int64_t len, double &similarity)
{
  int ret = OB_SUCCESS;
  double distance = 0;
  if (OB_FAIL(ObVectorIpDistance<uint8_t>::ip_distance_func(a, b, len, distance))) {
    if (OB_ERR_NULL_VALUE != ret) {
      LIB_LOG(WARN, "failed to cal ip distance", K(ret));
    }
  } else {
    similarity = get_ip_similarity(distance);
  }
  return ret;
}

}
}
