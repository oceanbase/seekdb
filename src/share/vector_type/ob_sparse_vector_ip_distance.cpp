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

#include "ob_sparse_vector_ip_distance.h"
namespace oceanbase
{
namespace common
{

int ObSparseVectorIpDistance::spiv_ip_distance_func(const ObMapType *a, const ObMapType *b, double &distance)
{
  int ret = OB_SUCCESS;
  double square = 0;
  distance = 0;
  if (OB_ISNULL(a) || OB_ISNULL(b)) {
    ret = OB_ERR_NULL_VALUE;
    LIB_LOG(WARN, "invalid null pointer", K(ret), KP(a), KP(b));
  } else {
    uint32_t len_a = a->cardinality();
    uint32_t len_b = b->cardinality();
    uint32_t *keys_a = reinterpret_cast<uint32_t *>(a->get_key_array()->get_data());
    uint32_t *keys_b = reinterpret_cast<uint32_t *>(b->get_key_array()->get_data());
    float *values_a = reinterpret_cast<float *>(a->get_value_array()->get_data());
    float *values_b = reinterpret_cast<float *>(b->get_value_array()->get_data());
    for (int64_t i = 0, j = 0; OB_SUCC(ret) && i < len_a && j < len_b;) {
      if (keys_a[i] == keys_b[j]) {
        distance += values_a[i] * values_b[j];
        i++;
        j++;
      } else if (keys_a[i] < keys_b[j]) {
        i++;
      } else {
        j++;
      }
      if (OB_UNLIKELY(0 != ::isinf(distance))) {
        ret = OB_NUMERIC_OVERFLOW;
        LIB_LOG(WARN, "value is overflow", K(ret), K(distance));
      }
    }
  }
  return ret;
}

}
}
