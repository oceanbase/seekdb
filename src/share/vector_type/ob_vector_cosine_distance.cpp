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

#include "ob_vector_cosine_distance.h"
namespace oceanbase
{
namespace common
{

template <>
int ObVectorCosineDistance<float>::cosine_distance_func(const float *a, const float *b, const int64_t len, double &distance) {
  int ret = OB_SUCCESS;
  double similarity = 0;
  if (OB_FAIL(cosine_similarity_func(a, b, len, similarity))) {
    if (OB_ERR_NULL_VALUE != ret) {
      LIB_LOG(WARN, "failed to cal cosine similaity", K(ret));
    }
  } else {
    distance = get_cosine_distance(similarity);
  }
  return ret;
}

template <>
int ObVectorCosineDistance<uint8_t>::cosine_distance_func(const uint8_t *a, const uint8_t *b, const int64_t len, double &distance) {
  int ret = OB_SUCCESS;
  double similarity = 0;
  if (OB_FAIL(cosine_similarity_func(a, b, len, similarity))) {
    if (OB_ERR_NULL_VALUE != ret) {
      LIB_LOG(WARN, "failed to cal cosine similaity", K(ret));
    }
  } else {
    distance = get_cosine_distance(similarity);
  }
  return ret;
}

template <>
int ObVectorCosineDistance<float>::cosine_similarity_func(const float *a, const float *b, const int64_t len, double &similarity)
{
  int ret = OB_SUCCESS;
  #if OB_USE_MULTITARGET_CODE
    if (common::is_arch_supported(ObTargetArch::AVX512)) {
      ret = common::specific::avx512::cosine_similarity(a, b, len, similarity);
    } else if (common::is_arch_supported(ObTargetArch::AVX2)) {
      ret = common::specific::avx2::cosine_similarity(a, b, len, similarity);
    } else {
      ret = common::specific::normal::cosine_similarity(a, b, len, similarity);
    }
  #else
    ret = common::specific::normal::cosine_similarity(a, b, len, similarity);
  #endif
  return ret;
}

template <>
int ObVectorCosineDistance<uint8_t>::cosine_similarity_func(const uint8_t *a, const uint8_t *b, const int64_t len, double &similarity)
{
  return cosine_similarity_normal(a, b, len, similarity);
}

}
}
