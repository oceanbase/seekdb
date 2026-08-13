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

#ifndef OCEANBASE_DATA_PLANE_API_ENCODING_OB_CPU_FEATURES_H_
#define OCEANBASE_DATA_PLANE_API_ENCODING_OB_CPU_FEATURES_H_

namespace oceanbase
{
namespace data_plane
{

inline bool is_avx512_supported()
{
#if defined(__x86_64__)
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;
  __asm("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0) : );
  return (b & (1 << 31)) != 0             // AVX512VL
      && (b & 0x40020000) == 0x40020000;  // AVX512BW/AVX512DQ
#else
  return false;
#endif
}

inline bool is_avx2_supported()
{
#if defined(__x86_64__)
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;
  __asm("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(7), "c"(0) : );
  return (b & (1 << 5)) != 0;
#else
  return false;
#endif
}

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_ENCODING_OB_CPU_FEATURES_H_
