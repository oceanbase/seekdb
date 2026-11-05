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

#ifndef OCEANBASE_DATA_PLANE_API_OB_VECTOR_L2_DISTANCE_H_
#define OCEANBASE_DATA_PLANE_API_OB_VECTOR_L2_DISTANCE_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log.h"
#include "lib/ob_define.h"
#include "common/object/ob_obj_compare.h"
#include "ob_vector_op_common.h"
#include "ob_vector_l2_distance_reference.h"

namespace oceanbase
{
namespace common
{
template<typename T>
struct ObVectorL2Distance
{
  static int l2_square_func(const T *a, const T *b, const int64_t len, double &square);
  static int l2_distance_func(const T *a, const T *b, const int64_t len, double &distance);
  // float interface
  static float l2_square_flt_func(const T *a, const T *b, const int64_t len);
  static float l2_norm_square(const T *a, const int64_t len);
  static void fvec_madd(size_t n, const T* a, T bf, const T* b, T* c);
  static void distance_four_codes(const size_t M, const size_t nbits, const float* sim_table,
                                  const uint8_t* __restrict code0, const uint8_t* __restrict code1,
                                  const uint8_t* __restrict code2, const uint8_t* __restrict code3,
                                  float &result0, float &result1, float &result2, float &result3);
  static float distance_one_code(const size_t M, const size_t nbits, const float* sim_table, const uint8_t* code);
};

template<>
int ObVectorL2Distance<float>::l2_square_func(const float *a, const float *b, const int64_t len, double &square);
template <>
int ObVectorL2Distance<uint8_t>::l2_square_func(const uint8_t *a, const uint8_t *b, const int64_t len, double &square);
template <>
float ObVectorL2Distance<float>::l2_square_flt_func(const float *a, const float *b, const int64_t len);
template<>
float ObVectorL2Distance<float>::l2_norm_square(const float *a, const int64_t len);
template<>
void ObVectorL2Distance<float>::fvec_madd(size_t n, const float* a, float bf, const float* b, float* c);

template<typename T>
int ObVectorL2Distance<T>::l2_distance_func(const T *a, const T *b, const int64_t len, double &distance)
{
  int ret = OB_SUCCESS;
  double square = 0;
  distance = 0;
  if (OB_ISNULL(a) || OB_ISNULL(b)) {
    ret = OB_ERR_NULL_VALUE;
    LIB_LOG(WARN, "invalid null pointer", K(ret), KP(a), KP(b));
  } else if (OB_FAIL(l2_square_func(a, b, len, square))) {
  } else {
    distance = sqrt(square);
  }
  return ret;
}

// l2 square flt interface
OB_DECLARE_DEFAULT_CODE(
  inline static float l2_square_flt(const float *a, const float *b, const int64_t len) {
    float result = 0.0f;
    for (uint64_t i = 0; i < len; ++i) {
      float val = a[i] - b[i];
      result += val * val;
    }
    return result;
  }
  inline static float l2_norm_square(const float *a, size_t len) {
    size_t i;
    float res = 0.0f;
    for (i = 0; i < len; i++) {
        res += a[i] * a[i];
    }
    return res;
  }
  inline static void fvec_madd(size_t n, const float* a, float bf, const float* b, float* c) {
    for (size_t i = 0; i < n; i++) {
        c[i] = a[i] + bf * b[i];
    }
  }
  inline static void distance_four_codes(
    const size_t M, const size_t nbits, const float* sim_table,
    const uint8_t* __restrict code0, const uint8_t* __restrict code1,
    const uint8_t* __restrict code2, const uint8_t* __restrict code3,
    float &result0, float &result1, float &result2, float &result3)
  {
    PQDecoderGeneric decoder0(code0, nbits);
    PQDecoderGeneric decoder1(code1, nbits);
    PQDecoderGeneric decoder2(code2, nbits);
    PQDecoderGeneric decoder3(code3, nbits);
    const size_t ksub  = 1 << nbits;
    const float* tab = sim_table;
    result0 = 0;
    result1 = 0;
    result2 = 0;
    result3 = 0;
    for (size_t i = 0; i < M; i++) {
      result0 += tab[decoder0.decode()];
      result1 += tab[decoder1.decode()];
      result2 += tab[decoder2.decode()];
      result3 += tab[decoder3.decode()];
      tab += ksub;
    }
  }

  inline static float distance_one_code(const size_t M, const size_t nbits, const float* sim_table, const uint8_t* code)
  {
    PQDecoderGeneric decoder(code, nbits);
    const size_t ksub  = 1 << nbits;
    const float* tab = sim_table;
    float result = 0;
    for (size_t i = 0; i < M; i++) {
      result += tab[decoder.decode()];
      tab += ksub;
    }
    return result;
  }
)

OB_DECLARE_SSE42_SPECIFIC_CODE(
  inline static float l2_square_flt(const float *a, const float *b, const int64_t len) {
    float dis;
    if (common::is_arch_supported(ObTargetArch::SSE42)) {
      const uint64_t n = len / 4;
      if (n == 0) {
        dis = common::specific::normal::l2_square_flt(a, b, len);
      } else {
        __m128 sum = _mm_setzero_ps();
        for (int i = 0; i < n; ++i) {
          __m128 a0 = _mm_loadu_ps(a + i * 4);
          __m128 b0 = _mm_loadu_ps(b + i * 4);
          __m128 diff = _mm_sub_ps(a0, b0);
          sum = _mm_add_ps(sum, _mm_mul_ps(diff, diff));
        }
        alignas(16) float result[4];
        _mm_store_ps(result, sum);
        dis = result[0] + result[1] + result[2] + result[3];
        dis += common::specific::normal::l2_square_flt(a + n * 4, b + n * 4, len - n * 4);
      }
    } else {
      dis = common::specific::normal::l2_square_flt(a, b, len);
    }
    return dis;
  }

  inline static float l2_norm_square(const float *a, size_t len) {
    __m128 mx;
    __m128 msum1 = _mm_setzero_ps();

    while (len >= 4) {
      mx = _mm_loadu_ps(a);
      a += 4;
      msum1 = _mm_add_ps(msum1, _mm_mul_ps(mx, mx));
      len -= 4;
    }

    mx = masked_read(len, a);
    msum1 = _mm_add_ps(msum1, _mm_mul_ps(mx, mx));

    msum1 = _mm_hadd_ps(msum1, msum1);
    msum1 = _mm_hadd_ps(msum1, msum1);
    return _mm_cvtss_f32(msum1);
  }

  inline static void fvec_madd(size_t n, const float* a, float bf, const float* b, float* c) {
    size_t i = 0;
    for (; i + 3 < n; i += 4) {
      __m128 va = _mm_loadu_ps(a + i);
      __m128 vb = _mm_loadu_ps(b + i);

      __m128 vbf = _mm_set1_ps(bf);
      __m128 vb_scaled = _mm_mul_ps(vb, vbf);

      __m128 vresult = _mm_add_ps(va, vb_scaled);

      _mm_storeu_ps(c + i, vresult);
    }

    for (; i < n; ++i) {
        c[i] = a[i] + bf * b[i];
    }
  }
)

OB_DECLARE_AVX_SPECIFIC_CODE(
  inline static float l2_square_flt(const float *a, const float *b, const int64_t len) {
    float dis;
    if (common::is_arch_supported(ObTargetArch::AVX)) {
      const int n = len / 8;
      if (n == 0) {
        dis = common::specific::sse42::l2_square_flt(a, b, len);
      } else {
        // process 8 floats at a time
        __m256 sum = _mm256_setzero_ps();  // initialize to 0
        for (int i = 0; i < n; ++i) {
          __m256 a0 = _mm256_loadu_ps(a + i * 8);            // load 8 floats from memory
          __m256 b0 = _mm256_loadu_ps(b + i * 8);            // load 8 floats from memory
          __m256 diff = _mm256_sub_ps(a0, b0);                    // calculate the difference
          sum = _mm256_add_ps(sum, _mm256_mul_ps(diff, diff));  // accumulate the squared difference
        }
        alignas(32) float result[8];
        _mm256_store_ps(result, sum);  // store the accumulated result into an array
        dis = result[0] + result[1] + result[2] + result[3] + result[4] + result[5] + result[6] +
                  result[7];  // calculate the sum of the accumulated results
        dis += common::specific::sse42::l2_square_flt(a + n * 8, b + n * 8, len - n * 8);
      }
    } else {
      dis = common::specific::sse42::l2_square_flt(a, b, len);
    }
    return dis;
  }
)

OB_DECLARE_AVX2_SPECIFIC_CODE(
  inline static float l2_square_flt(const float *a, const float *b, const int64_t len) {
    float dis;
    if (common::is_arch_supported(ObTargetArch::AVX2)) {
      const int n = len / 8;
      if (n == 0) {
        dis = common::specific::avx::l2_square_flt(a, b, len);
      } else {
        // process 8 floats at a time
        __m256 sum = _mm256_setzero_ps();  // initialize to 0
        for (int i = 0; i < n; ++i) {
          __m256 a0 = _mm256_loadu_ps(a + i * 8);  // load 8 floats from memory
          __m256 b0 = _mm256_loadu_ps(b + i * 8);  // load 8 floats from memory
          __m256 diff = _mm256_sub_ps(a0, b0);          // calculate the difference
          sum = _mm256_fmadd_ps(diff, diff, sum);     // accumulate the squared difference
        }
        alignas(32) float result[8];
        _mm256_store_ps(result, sum);  // store the accumulated result into an array
        dis = result[0] + result[1] + result[2] + result[3] + result[4] + result[5] + result[6] +
            result[7];  // calculate the sum of the accumulated results
        dis += common::specific::avx::l2_square_flt(a + n * 8, b + n * 8, len - n * 8);
      }
    } else {
      dis = common::specific::avx::l2_square_flt(a, b, len);
    }
    return dis;
  }

  inline static __m128 masked_read(int len, const float* a) {
      __attribute__((aligned(16))) float buf[4] = {0, 0, 0, 0};
      switch (len) {
        case 3:
          buf[2] = a[2];
        case 2:
          buf[1] = a[1];
        case 1:
          buf[0] = a[0];
      }
      return _mm_load_ps(buf);
  }

  inline static float _mm256_reduce_add_ps(const __m256 res) {
    const __m128 sum = _mm_add_ps(_mm256_castps256_ps128(res), _mm256_extractf128_ps(res, 1));
    const __m128 v0 = _mm_shuffle_ps(sum, sum, _MM_SHUFFLE(0, 0, 3, 2));
    const __m128 v1 = _mm_add_ps(sum, v0);
    __m128 v2 = _mm_shuffle_ps(v1, v1, _MM_SHUFFLE(0, 0, 0, 1));
    const __m128 v3 = _mm_add_ps(v1, v2);
    return _mm_cvtss_f32(v3);
  }

  inline static float l2_norm_square(const float *a, size_t len) {
    __m256 msum_0 = _mm256_setzero_ps();
    while (len >= 16) {
        auto mx_0 = _mm256_loadu_ps(a);
        auto mx_1 = _mm256_loadu_ps(a + 8);
        msum_0 = _mm256_fmadd_ps(mx_0, mx_0, msum_0);
        auto msum_1 = _mm256_mul_ps(mx_1, mx_1);
        msum_0 = msum_0 + msum_1;
        a += 16;
        len -= 16;
    }
    if (len >= 8) {
        auto mx = _mm256_loadu_ps(a);
        msum_0 = _mm256_fmadd_ps(mx, mx, msum_0);
        a += 8;
        len -= 8;
    }
    if (len > 0) {
        __m128 rest_0 = _mm_setzero_ps();
        __m128 rest_1 = _mm_setzero_ps();
        if (len >= 4) {
            rest_0 = _mm_loadu_ps(a);
            a += 4;
            len -= 4;
        }
        if (len >= 0) {
            rest_1 = masked_read(len, a);
        }
        auto mx = _mm256_set_m128(rest_0, rest_1);
        msum_0 = _mm256_fmadd_ps(mx, mx, msum_0);
    }
    auto res = _mm256_reduce_add_ps(msum_0);
    return res;
  }

  inline static void fvec_madd(size_t n, const float* a, float bf, const float* b, float* c) {
    const __m256 vbf = _mm256_set1_ps(bf);
    size_t i = 0;
    for (; i + 15 < n; i += 16) {
        __m256 va0 = _mm256_loadu_ps(a + i);
        __m256 va1 = _mm256_loadu_ps(a + i + 8);

        __m256 vb0 = _mm256_loadu_ps(b + i);
        __m256 vb1 = _mm256_loadu_ps(b + i + 8);

        __m256 tmp0 = _mm256_mul_ps(vb0, vbf);
        __m256 tmp1 = _mm256_mul_ps(vb1, vbf);

        __m256 vres0 = _mm256_add_ps(va0, tmp0);
        __m256 vres1 = _mm256_add_ps(va1, tmp1);

        _mm256_storeu_ps(c + i, vres0);
        _mm256_storeu_ps(c + i + 8, vres1);
    }

    for (; i < n; ++i) {
      c[i] = a[i] + bf * b[i];
    }

  }

#if defined(__GNUC__) && __GNUC__ < 9
#define _mm_loadu_si64(x) (_mm_loadl_epi64((__m128i_u*)x))
#endif

  inline float horizontal_sum(const __m128 v) {
    const __m128 v0 = _mm_shuffle_ps(v, v, _MM_SHUFFLE(0, 0, 3, 2));
    const __m128 v1 = _mm_add_ps(v, v0);
    __m128 v2 = _mm_shuffle_ps(v1, v1, _MM_SHUFFLE(0, 0, 0, 1));
    const __m128 v3 = _mm_add_ps(v1, v2);
    return _mm_cvtss_f32(v3);
  }

  // Computes a horizontal sum over an __m256 register
  inline float horizontal_sum(const __m256 v) {
    const __m128 v0 =
      _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1));
    return horizontal_sum(v0);
  }

// processes a single code for M=4, ksub=256, nbits=8
  inline float distance_single_code_avx2_pqdecoder8_m4(
    // precomputed distances, layout (4, 256)
    const float* sim_table,
    const uint8_t* code) {
    float result = 0;

    const float* tab = sim_table;
    constexpr size_t ksub = 1 << 8;

    const __m128i vksub = _mm_set1_epi32(ksub);
    __m128i offsets_0 = _mm_setr_epi32(0, 1, 2, 3);
    offsets_0 = _mm_mullo_epi32(offsets_0, vksub);

    // accumulators of partial sums
    __m128 partialSum;

    // load 4 uint8 values
    const __m128i mm1 = _mm_cvtsi32_si128(*((const int32_t*)code));
    {
      // convert uint8 values (low part of __m128i) to int32
      // values
      const __m128i idx1 = _mm_cvtepu8_epi32(mm1);

      // add offsets
      const __m128i indices_to_read_from = _mm_add_epi32(idx1, offsets_0);

      // gather 8 values, similar to 8 operations of tab[idx]
      __m128 collected =
              _mm_i32gather_ps(tab, indices_to_read_from, sizeof(float));

      // collect partial sums
      partialSum = collected;
    }

    // horizontal sum for partialSum
    result = horizontal_sum(partialSum);
    return result;
  }

  // processes a single code for M=8, ksub=256, nbits=8
  inline float distance_single_code_avx2_pqdecoder8_m8(
    // precomputed distances, layout (8, 256)
    const float* sim_table,
    const uint8_t* code) {
    float result = 0;

    const float* tab = sim_table;
    constexpr size_t ksub = 1 << 8;

    const __m256i vksub = _mm256_set1_epi32(ksub);
    __m256i offsets_0 = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    offsets_0 = _mm256_mullo_epi32(offsets_0, vksub);

    // accumulators of partial sums
    __m256 partialSum;

    // load 8 uint8 values
    const __m128i mm1 = _mm_loadu_si64((const __m128i_u*)code);
    {
      // convert uint8 values (low part of __m128i) to int32
      // values
      const __m256i idx1 = _mm256_cvtepu8_epi32(mm1);

      // add offsets
      const __m256i indices_to_read_from = _mm256_add_epi32(idx1, offsets_0);

      // gather 8 values, similar to 8 operations of tab[idx]
      __m256 collected =
              _mm256_i32gather_ps(tab, indices_to_read_from, sizeof(float));

      // collect partial sums
      partialSum = collected;
    }

    // horizontal sum for partialSum
    result = horizontal_sum(partialSum);
    return result;
  }

  // processes four codes for M=4, ksub=256, nbits=8
  inline void distance_four_codes_avx2_pqdecoder8_m4(
    // precomputed distances, layout (4, 256)
    const float* sim_table,
    // codes
    const uint8_t* __restrict code0,
    const uint8_t* __restrict code1,
    const uint8_t* __restrict code2,
    const uint8_t* __restrict code3,
    // computed distances
    float& result0,
    float& result1,
    float& result2,
    float& result3) {
    constexpr intptr_t N = 4;

    const float* tab = sim_table;
    constexpr size_t ksub = 1 << 8;

    // process 8 values
    const __m128i vksub = _mm_set1_epi32(ksub);
    __m128i offsets_0 = _mm_setr_epi32(0, 1, 2, 3);
    offsets_0 = _mm_mullo_epi32(offsets_0, vksub);

    // accumulators of partial sums
    __m128 partialSums[N];

    // load 4 uint8 values
    __m128i mm1[N];
    mm1[0] = _mm_cvtsi32_si128(*((const int32_t*)code0));
    mm1[1] = _mm_cvtsi32_si128(*((const int32_t*)code1));
    mm1[2] = _mm_cvtsi32_si128(*((const int32_t*)code2));
    mm1[3] = _mm_cvtsi32_si128(*((const int32_t*)code3));

    for (intptr_t j = 0; j < N; j++) {
      // convert uint8 values (low part of __m128i) to int32
      // values
      const __m128i idx1 = _mm_cvtepu8_epi32(mm1[j]);

      // add offsets
      const __m128i indices_to_read_from = _mm_add_epi32(idx1, offsets_0);

      // gather 4 values, similar to 4 operations of tab[idx]
      __m128 collected =
              _mm_i32gather_ps(tab, indices_to_read_from, sizeof(float));

      // collect partial sums
      partialSums[j] = collected;
    }

    // horizontal sum for partialSum
    result0 = horizontal_sum(partialSums[0]);
    result1 = horizontal_sum(partialSums[1]);
    result2 = horizontal_sum(partialSums[2]);
    result3 = horizontal_sum(partialSums[3]);
  }

  // processes four codes for M=8, ksub=256, nbits=8
  inline void distance_four_codes_avx2_pqdecoder8_m8(
    // precomputed distances, layout (8, 256)
    const float* sim_table,
    // codes
    const uint8_t* __restrict code0,
    const uint8_t* __restrict code1,
    const uint8_t* __restrict code2,
    const uint8_t* __restrict code3,
    // computed distances
    float& result0,
    float& result1,
    float& result2,
    float& result3) {
    constexpr intptr_t N = 4;

    const float* tab = sim_table;
    constexpr size_t ksub = 1 << 8;

    // process 8 values
    const __m256i vksub = _mm256_set1_epi32(ksub);
    __m256i offsets_0 = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    offsets_0 = _mm256_mullo_epi32(offsets_0, vksub);

    // accumulators of partial sums
    __m256 partialSums[N];

    // load 8 uint8 values
    __m128i mm1[N];
    mm1[0] = _mm_loadu_si64((const __m128i_u*)code0);
    mm1[1] = _mm_loadu_si64((const __m128i_u*)code1);
    mm1[2] = _mm_loadu_si64((const __m128i_u*)code2);
    mm1[3] = _mm_loadu_si64((const __m128i_u*)code3);

    for (intptr_t j = 0; j < N; j++) {
      // convert uint8 values (low part of __m128i) to int32
      // values
      const __m256i idx1 = _mm256_cvtepu8_epi32(mm1[j]);

      // add offsets
      const __m256i indices_to_read_from = _mm256_add_epi32(idx1, offsets_0);

      // gather 8 values, similar to 8 operations of tab[idx]
      __m256 collected =
              _mm256_i32gather_ps(tab, indices_to_read_from, sizeof(float));

      // collect partial sums
      partialSums[j] = collected;
    }

    // horizontal sum for partialSum
    result0 = horizontal_sum(partialSums[0]);
    result1 = horizontal_sum(partialSums[1]);
    result2 = horizontal_sum(partialSums[2]);
    result3 = horizontal_sum(partialSums[3]);
  }

  inline static float distance_one_code(
    // number of subquantizers
    const size_t M,
    // number of bits per quantization index
    const size_t nbits,
    // precomputed distances, layout (M, ksub)
    const float* sim_table,
    const uint8_t* code) {
    if (M == 4) {
        return distance_single_code_avx2_pqdecoder8_m4(sim_table, code);
    }
    if (M == 8) {
      return distance_single_code_avx2_pqdecoder8_m8(sim_table, code);
    }

    float result = 0;
    size_t ksub = 1 << nbits;

    size_t m = 0;
    const size_t pqM16 = M / 16;

    const float* tab = sim_table;

    if (pqM16 > 0) {
      // process 16 values per loop
      const __m256i vksub = _mm256_set1_epi32(ksub);
      __m256i offsets_0 = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
      offsets_0 = _mm256_mullo_epi32(offsets_0, vksub);

      // accumulators of partial sums
      __m256 partialSum = _mm256_setzero_ps();

      // loop
      for (m = 0; m < pqM16 * 16; m += 16) {
        // load 16 uint8 values
        const __m128i mm1 = _mm_loadu_si128((const __m128i_u*)(code + m));
        {
          // convert uint8 values (low part of __m128i) to int32
          // values
          const __m256i idx1 = _mm256_cvtepu8_epi32(mm1);

          // add offsets
          const __m256i indices_to_read_from =
                  _mm256_add_epi32(idx1, offsets_0);

          // gather 8 values, similar to 8 operations of tab[idx]
          __m256 collected = _mm256_i32gather_ps(
                  tab, indices_to_read_from, sizeof(float));
          tab += ksub * 8;

          // collect partial sums
          partialSum = _mm256_add_ps(partialSum, collected);
        }

        // move high 8 uint8 to low ones
        const __m128i mm2 = _mm_unpackhi_epi64(mm1, _mm_setzero_si128());
        {
          // convert uint8 values (low part of __m128i) to int32
          // values
          const __m256i idx1 = _mm256_cvtepu8_epi32(mm2);

          // add offsets
          const __m256i indices_to_read_from =
                  _mm256_add_epi32(idx1, offsets_0);

          // gather 8 values, similar to 8 operations of tab[idx]
          __m256 collected = _mm256_i32gather_ps(
                  tab, indices_to_read_from, sizeof(float));
          tab += ksub * 8;

          // collect partial sums
          partialSum = _mm256_add_ps(partialSum, collected);
        }
      }

      result += horizontal_sum(partialSum);
    }

    if (m < M) {
      // process leftovers
      PQDecoder<uint8_t> decoder(code + m);

      for (; m < M; m++) {
        result += tab[decoder.decode()];
        tab += ksub;
      }
    }
    return result;
  }

  // Combines 4 operations of distance_single_code()
  inline static void distance_four_codes(
    // number of subquantizers
    const size_t M,
    // number of bits per quantization index
    const size_t nbits,
    // precomputed distances, layout (M, ksub)
    const float* sim_table,
    // codes
    const uint8_t* __restrict code0,
    const uint8_t* __restrict code1,
    const uint8_t* __restrict code2,
    const uint8_t* __restrict code3,
    // computed distances
    float& result0,
    float& result1,
    float& result2,
    float& result3)
  {
    bool is_decode8 = (nbits <= 8);
    if (!is_decode8) {
      common::specific::normal::distance_four_codes(M, nbits, sim_table, code0, code1, code2, code3, result0, result1, result2, result3);
    } else {
      if (M == 4) {
        distance_four_codes_avx2_pqdecoder8_m4(
                sim_table,
                code0,
                code1,
                code2,
                code3,
                result0,
                result1,
                result2,
                result3);
        return;
      }
      if (M == 8) {
        distance_four_codes_avx2_pqdecoder8_m8(
                sim_table,
                code0,
                code1,
                code2,
                code3,
                result0,
                result1,
                result2,
                result3);
        return;
      }

      result0 = 0;
      result1 = 0;
      result2 = 0;
      result3 = 0;
      size_t ksub = 1 << nbits;

      size_t m = 0;
      const size_t pqM16 = M / 16;

      constexpr intptr_t N = 4;

      const float* tab = sim_table;

      if (pqM16 > 0) {
        // process 16 values per loop
        const __m256i vksub = _mm256_set1_epi32(ksub);
        __m256i offsets_0 = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
        offsets_0 = _mm256_mullo_epi32(offsets_0, vksub);

        // accumulators of partial sums
        __m256 partialSums[N];
        for (intptr_t j = 0; j < N; j++) {
          partialSums[j] = _mm256_setzero_ps();
        }

        // loop
        for (m = 0; m < pqM16 * 16; m += 16) {
          // load 16 uint8 values
          __m128i mm1[N];
          mm1[0] = _mm_loadu_si128((const __m128i_u*)(code0 + m));
          mm1[1] = _mm_loadu_si128((const __m128i_u*)(code1 + m));
          mm1[2] = _mm_loadu_si128((const __m128i_u*)(code2 + m));
          mm1[3] = _mm_loadu_si128((const __m128i_u*)(code3 + m));

          // process first 8 codes
          for (intptr_t j = 0; j < N; j++) {
            // convert uint8 values (low part of __m128i) to int32
            // values
            const __m256i idx1 = _mm256_cvtepu8_epi32(mm1[j]);

            // add offsets
            const __m256i indices_to_read_from =
                    _mm256_add_epi32(idx1, offsets_0);

            // gather 8 values, similar to 8 operations of tab[idx]
            __m256 collected = _mm256_i32gather_ps(
                    tab, indices_to_read_from, sizeof(float));

            // collect partial sums
            partialSums[j] = _mm256_add_ps(partialSums[j], collected);
          }
          tab += ksub * 8;

          // process next 8 codes
          for (intptr_t j = 0; j < N; j++) {
            // move high 8 uint8 to low ones
            const __m128i mm2 =
                    _mm_unpackhi_epi64(mm1[j], _mm_setzero_si128());

            // convert uint8 values (low part of __m128i) to int32
            // values
            const __m256i idx1 = _mm256_cvtepu8_epi32(mm2);

            // add offsets
            const __m256i indices_to_read_from =
                    _mm256_add_epi32(idx1, offsets_0);

            // gather 8 values, similar to 8 operations of tab[idx]
            __m256 collected = _mm256_i32gather_ps(
                    tab, indices_to_read_from, sizeof(float));

            // collect partial sums
            partialSums[j] = _mm256_add_ps(partialSums[j], collected);
          }

          tab += ksub * 8;
        }

        // horizontal sum for partialSum
        result0 += horizontal_sum(partialSums[0]);
        result1 += horizontal_sum(partialSums[1]);
        result2 += horizontal_sum(partialSums[2]);
        result3 += horizontal_sum(partialSums[3]);
      }

      if (m < M) {
        // process leftovers
        PQDecoder<uint8_t> decoder0(code0 + m);
        PQDecoder<uint8_t> decoder1(code1 + m);
        PQDecoder<uint8_t> decoder2(code2 + m);
        PQDecoder<uint8_t> decoder3(code3 + m);
        for (; m < M; m++) {
          result0 += tab[decoder0.decode()];
          result1 += tab[decoder1.decode()];
          result2 += tab[decoder2.decode()];
          result3 += tab[decoder3.decode()];
          tab += ksub;
        }
      }
    }
  }

)

OB_DECLARE_AVX512_SPECIFIC_CODE(
  OB_INLINE static float l2_square_flt(const float *a, const float *b, const int64_t len) {
    float dis;
    if (common::is_arch_supported(ObTargetArch::AVX512)) {
      if (len < 16) {
        dis = common::specific::avx2::l2_square_flt(a, b, len);
      } else {
        __m512 sum0 = _mm512_setzero_ps();
        __m512 sum1 = _mm512_setzero_ps();
        __m512 sum2 = _mm512_setzero_ps();
        __m512 sum3 = _mm512_setzero_ps();

        uint64_t i = 0;
        for (; i + 63 < len; i += 64) {
          __m512 a0 = _mm512_loadu_ps(a + i);
          __m512 b0 = _mm512_loadu_ps(b + i);

          __m512 a1 = _mm512_loadu_ps(a + i + 16);
          __m512 b1 = _mm512_loadu_ps(b + i + 16);

          __m512 a2 = _mm512_loadu_ps(a + i + 32);
          __m512 b2 = _mm512_loadu_ps(b + i + 32);

          __m512 a3 = _mm512_loadu_ps(a + i + 48);
          __m512 b3 = _mm512_loadu_ps(b + i + 48);

          __m512 diff0 = _mm512_sub_ps(a0, b0);
          __m512 diff1 = _mm512_sub_ps(a1, b1);
          __m512 diff2 = _mm512_sub_ps(a2, b2);
          __m512 diff3 = _mm512_sub_ps(a3, b3);

          sum0 = _mm512_fmadd_ps(diff0, diff0, sum0);
          sum1 = _mm512_fmadd_ps(diff1, diff1, sum1);
          sum2 = _mm512_fmadd_ps(diff2, diff2, sum2);
          sum3 = _mm512_fmadd_ps(diff3, diff3, sum3);
        }

        __m512 sum = _mm512_add_ps(sum0, sum1);
        sum = _mm512_add_ps(sum, sum2);
        sum = _mm512_add_ps(sum, sum3);

        for (; i + 15 < len; i += 16) {
          __m512 a0 = _mm512_loadu_ps(a + i);
          __m512 b0 = _mm512_loadu_ps(b + i);
          __m512 diff = _mm512_sub_ps(a0, b0);
          sum = _mm512_fmadd_ps(diff, diff, sum);
        }

        dis = _mm512_reduce_add_ps(sum);
        if (len - i > 0) {
          dis += common::specific::avx2::l2_square_flt(a + i, b + i, len - i);
        }
      }
    } else {
      dis = common::specific::avx2::l2_square_flt(a, b, len);
    }
    return dis;
  }

  inline static float l2_norm_square(const float *a, size_t len) {
    __m512 m512_res = _mm512_setzero_ps();
    __m512 m512_res_0 = _mm512_setzero_ps();
    while (len >= 32) {
      auto mx_0 = _mm512_loadu_ps(a);
      auto mx_1 = _mm512_loadu_ps(a + 16);
      m512_res = _mm512_fmadd_ps(mx_0, mx_0, m512_res);
      m512_res_0 = _mm512_fmadd_ps(mx_1, mx_1, m512_res_0);
      a += 32;
      len -= 32;
    }
    m512_res = m512_res + m512_res_0;
    if (len >= 16) {
      auto mx = _mm512_loadu_ps(a);
      m512_res = _mm512_fmadd_ps(mx, mx, m512_res);
      a += 16;
      len -= 16;
    }
    if (len > 0) {
      const __mmask16 mask = (1U << len) - 1U;
      auto mx = _mm512_maskz_loadu_ps(mask, a);
      m512_res = _mm512_fmadd_ps(mx, mx, m512_res);
    }
    return _mm512_reduce_add_ps(m512_res);
  }

  inline static void fvec_madd(size_t n, const float* a, float bf, const float* b, float* c) {
    const size_t n16 = n / 16;
    const size_t n_for_masking = n % 16;
    const __m512 bfmm = _mm512_set1_ps(bf);

    size_t idx = 0;
    for (idx = 0; idx < n16 * 16; idx += 16) {
        const __m512 ax = _mm512_loadu_ps(a + idx);
        const __m512 bx = _mm512_loadu_ps(b + idx);
        const __m512 abmul = _mm512_fmadd_ps(bfmm, bx, ax);
        _mm512_storeu_ps(c + idx, abmul);
    }
    if (n_for_masking > 0) {
        const __mmask16 mask = (1 << n_for_masking) - 1;

        const __m512 ax = _mm512_maskz_loadu_ps(mask, a + idx);
        const __m512 bx = _mm512_maskz_loadu_ps(mask, b + idx);
        const __m512 abmul = _mm512_fmadd_ps(bfmm, bx, ax);
        _mm512_mask_storeu_ps(c + idx, mask, abmul);
    }
  }
)

}  // namespace common
}  // namespace oceanbase
#endif
