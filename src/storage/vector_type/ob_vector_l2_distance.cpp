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

#include "ob_vector_l2_distance.h"
#include <cfloat>
#include <cstring>
#include "rust/vector_kernels.h"
namespace oceanbase
{
namespace common
{
template<>
int ObVectorL2Distance<float>::l2_square_func(const float *a, const float *b, const int64_t len,
                                              double &square)
{
  int ret = OB_SUCCESS;
  // One portable SIMD implementation (Rust `simsimd`, runtime ISA dispatch)
  // replaces the per-ISA hand-written float32 lane kernels. Its float32 lane
  // accumulation has the old range defect: a square/lane sum outside the
  // normal float32 range becomes inf or loses all precision. Detect that on
  // the result and redo the rare unsafe vector with the deterministic binary64
  // reference (ob_vector_l2_distance_reference.h), which is in range for the
  // whole finite-float32 domain.
  square = l2_square_f32(rust::Slice<const float>(a, len),
                         rust::Slice<const float>(b, len));
  if (OB_UNLIKELY(!std::isfinite(square)
                  || (square > 0 && square < FLT_MIN)
                  || (0 == square && 0 != std::memcmp(a, b, len * sizeof(float))))) {
    square = l2_square_float_reference(a, b, len);
  }
  if (OB_UNLIKELY(0 != ::isinf(square))) {
    ret = OB_NUMERIC_OVERFLOW;
    LIB_LOG(WARN, "value is overflow", K(ret), K(len), K(square));
  }
  return ret;
}
template <>
int ObVectorL2Distance<uint8_t>::l2_square_func(const uint8_t *a, const uint8_t *b, const int64_t len, double &square)
{
  int ret = OB_SUCCESS;
  double sum = 0;
  double diff = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < len; ++i) {
    diff = a[i] - b[i];
    sum += (diff * diff);
    if (OB_UNLIKELY(0 != ::isinf(sum))) {
      ret = OB_NUMERIC_OVERFLOW;
      LIB_LOG(WARN, "value is overflow", K(ret), K(diff), K(sum));
    }
  }
  if (OB_SUCC(ret)) {
    square = sum;
  }
  return ret;
}

template <>
float ObVectorL2Distance<float>::l2_square_flt_func(const float *a, const float *b, const int64_t len)
{
  float square = l2_square_flt_f32(rust::Slice<const float>(a, len),
                                   rust::Slice<const float>(b, len));
  // Same float32 range defect as the old per-ISA kernels, but internal callers
  // only need a bounded value: redo the rare unsafe vector in binary64 and
  // saturate instead of returning inf/zero, which would poison k-means or the
  // IVF candidate heaps.
  if (OB_UNLIKELY(!std::isfinite(square)
                  || (square > 0 && square < FLT_MIN)
                  || (0 == square && 0 != std::memcmp(a, b, len * sizeof(float))))) {
    const double reference = l2_square_float_reference(a, b, len);
    square = static_cast<float>(reference > static_cast<double>(FLT_MAX) ? FLT_MAX : reference);
  }
  return square;
}

template <>
float ObVectorL2Distance<float>::l2_norm_square(const float *a, const int64_t len)
{
  return l2_norm_square_f32(rust::Slice<const float>(a, len));
}

template<>
void ObVectorL2Distance<float>::fvec_madd(size_t n, const float* a, float bf, const float* b, float* c)
{
  fvec_madd_f32(rust::Slice<const float>(a, n), bf,
                rust::Slice<const float>(b, n), rust::Slice<float>(c, n));
}

template<>
void ObVectorL2Distance<float>::fvec_madd_in_place(size_t n, const float* a, float bf, float* b)
{
  fvec_madd_in_place_f32(rust::Slice<const float>(a, n), bf, rust::Slice<float>(b, n));
}

template<>
void ObVectorL2Distance<float>::distance_four_codes(
    const size_t M, const size_t nbits, const float* sim_table,
    const uint8_t* __restrict code0, const uint8_t* __restrict code1,
    const uint8_t* __restrict code2, const uint8_t* __restrict code3,
    float &result0, float &result1, float &result2, float &result3)
{
#if OB_USE_MULTITARGET_CODE
  if (common::is_arch_supported(ObTargetArch::AVX2)) {
    return common::specific::avx2::distance_four_codes(M, nbits, sim_table, code0, code1, code2, code3, result0, result1, result2, result3);
  } else {
    return common::specific::normal::distance_four_codes(M, nbits, sim_table, code0, code1, code2, code3, result0, result1, result2, result3);
  }
#else
  return common::specific::normal::distance_four_codes(M, nbits, sim_table, code0, code1, code2, code3, result0, result1, result2, result3);
#endif
}

template<>
float ObVectorL2Distance<float>::distance_one_code(const size_t M, const size_t nbits, const float* sim_table, const uint8_t* code)
{
#if OB_USE_MULTITARGET_CODE
  if (common::is_arch_supported(ObTargetArch::AVX2)) {
    return common::specific::avx2::distance_one_code(M, nbits, sim_table, code);
  } else {
    return common::specific::normal::distance_one_code(M, nbits, sim_table, code);
  }
#else
  return common::specific::normal::distance_one_code(M, nbits, sim_table, code);
#endif
}

}
}
