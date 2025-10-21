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

#ifndef FNV_HASH_H_
#define FNV_HASH_H_

#include <cstdint>

namespace oceanbase
{
namespace common
{
namespace hash
{
#define fnv_offset_basis 0x811C9DC5 // 2166136261
#define fnv_prime        0x01000193 // 16777619

/*
fnv1_32(high 32bit) && fnv1a_32(low 32bit)
*/
constexpr static inline uint64_t
fnv1_32_and_fnv1a_32_compile_time_hash(char const* const str,
                                       const uint32_t fnv1 = fnv_offset_basis,
                                       const uint32_t fnv1a = fnv_offset_basis)
{
  return (str[0] == '\0') ? ((uint64_t)fnv1 << 32 | fnv1a) : fnv1_32_and_fnv1a_32_compile_time_hash(&str[1],
    (fnv1 * fnv_prime) ^ uint32_t(str[0]),
    (fnv1a ^ uint32_t(str[0])) * fnv_prime);
}

constexpr static inline uint64_t
fnv_hash_for_logger(char const* const str,
                    const int idx,
                    const uint32_t fnv1 = fnv_offset_basis,
                    const uint32_t fnv1a = fnv_offset_basis)
{
  return (idx < 0 || str[idx] == '/') ?
    ((uint64_t)fnv1 << 32 | fnv1a) :
    fnv_hash_for_logger(str, idx - 1, (fnv1 * fnv_prime) ^ uint32_t(str[idx]),
    (fnv1a ^ uint32_t(str[idx])) * fnv_prime);
}

template<int N>
constexpr static inline uint64_t
fnv_hash_for_logger(const char (&str)[N], const int FROM = N - 2)
{
  return fnv_hash_for_logger(str, FROM);
}

} // end of namespace hash
} // end of namespace common
} // end of namespace oceanbase

#endif // FNV_HASH_H_
