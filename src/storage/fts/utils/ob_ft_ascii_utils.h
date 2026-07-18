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

#ifndef _OCEANBASE_STORAGE_FTS_UTILS_OB_FT_ASCII_UTILS_H_
#define _OCEANBASE_STORAGE_FTS_UTILS_OB_FT_ASCII_UTILS_H_

#include "lib/utility/ob_target_specific.h"

#if OB_USE_MULTITARGET_CODE
#include <immintrin.h>
#endif

namespace oceanbase
{
namespace storage
{
namespace ascii
{

enum class CharType : uint8_t
{
  USELESS = 0,
  ARABIC = 1,
  ENGLISH = 2,
};

static constexpr uint64_t ASCII_HIGH_BIT_MASK64 = 0x8080808080808080ULL;

inline bool is_ascii_upper_byte(const uint8_t c)
{
  return c >= 'A' && c <= 'Z';
}

inline bool is_ascii_lower_byte(const uint8_t c)
{
  return c >= 'a' && c <= 'z';
}

inline bool is_ascii_alpha_byte(const uint8_t c)
{
  return is_ascii_upper_byte(c) || is_ascii_lower_byte(c);
}

inline bool is_ascii_digit_byte(const uint8_t c)
{
  return c >= '0' && c <= '9';
}

inline bool is_ascii_alnum_byte(const uint8_t c)
{
  return is_ascii_alpha_byte(c) || is_ascii_digit_byte(c);
}

inline char to_ascii_lower_char(const char c)
{
  const uint8_t byte = static_cast<uint8_t>(c);
  return is_ascii_upper_byte(byte) ? static_cast<char>(byte + ('a' - 'A')) : c;
}

inline CharType classify_ascii_byte(const uint8_t c)
{
  CharType type = CharType::USELESS;
  if (is_ascii_alpha_byte(c)) {
    type = CharType::ENGLISH;
  } else if (is_ascii_digit_byte(c)) {
    type = CharType::ARABIC;
  }
  return type;
}

inline bool is_ascii_letter_connector_byte(const uint8_t c)
{
  bool is_connector = false;
  switch (c) {
    case '#':
    case '&':
    case '+':
    case '-':
    case '.':
    case '@':
    case '_':
      is_connector = true;
      break;
    default:
      break;
  }
  return is_connector;
}

inline bool is_ascii_num_connector_byte(const uint8_t c)
{
  return c == ',' || c == '.';
}

inline int64_t count_ascii_prefix_scalar(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos < len && 0 == (static_cast<uint8_t>(str[pos]) & 0x80); ++pos) {
  }
  return pos;
}

inline int64_t count_leading_delimiter_scalar(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos < len && !is_ascii_alnum_byte(static_cast<uint8_t>(str[pos])); ++pos) {
  }
  return pos;
}

inline int64_t count_leading_token_scalar(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos < len && is_ascii_alnum_byte(static_cast<uint8_t>(str[pos])); ++pos) {
  }
  return pos;
}

inline bool has_ascii_upper_scalar(const char *str, const int64_t len)
{
  bool found = false;
  for (int64_t i = 0; !found && i < len; ++i) {
    found = is_ascii_upper_byte(static_cast<uint8_t>(str[i]));
  }
  return found;
}

inline void lowercase_ascii_copy_scalar(const char *src, const int64_t len, char *dst)
{
  for (int64_t i = 0; i < len; ++i) {
    dst[i] = to_ascii_lower_char(src[i]);
  }
}

inline bool is_ascii_string_scalar(const char *str, const int64_t len)
{
  bool is_ascii = true;
  int64_t pos = 0;
  for (; is_ascii && pos + static_cast<int64_t>(sizeof(uint64_t)) <= len; pos += sizeof(uint64_t)) {
    uint64_t chunk = 0;
    MEMCPY(&chunk, str + pos, sizeof(chunk));
    is_ascii = 0 == (chunk & ASCII_HIGH_BIT_MASK64);
  }
  for (; is_ascii && pos < len; ++pos) {
    is_ascii = 0 == (static_cast<uint8_t>(str[pos]) & 0x80);
  }
  return is_ascii;
}

OB_DECLARE_AVX2_SPECIFIC_CODE(

static constexpr int64_t AVX2_BYTES = sizeof(__m256i);

inline uint32_t get_ascii_alnum_mask(const char *str)
{
  const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(str));
  const __m256i zero = _mm256_set1_epi8('0' - 1);
  const __m256i nine = _mm256_set1_epi8('9' + 1);
  const __m256i upper_a = _mm256_set1_epi8('A' - 1);
  const __m256i upper_z = _mm256_set1_epi8('Z' + 1);
  const __m256i lower_a = _mm256_set1_epi8('a' - 1);
  const __m256i lower_z = _mm256_set1_epi8('z' + 1);
  const __m256i digits = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, zero), _mm256_cmpgt_epi8(nine, bytes));
  const __m256i uppers = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, upper_a), _mm256_cmpgt_epi8(upper_z, bytes));
  const __m256i lowers = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, lower_a), _mm256_cmpgt_epi8(lower_z, bytes));
  return static_cast<uint32_t>(
      _mm256_movemask_epi8(_mm256_or_si256(_mm256_or_si256(digits, uppers), lowers)));
}

inline uint32_t get_ascii_upper_mask(const char *str)
{
  const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(str));
  const __m256i upper_a = _mm256_set1_epi8('A' - 1);
  const __m256i upper_z = _mm256_set1_epi8('Z' + 1);
  const __m256i uppers = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, upper_a), _mm256_cmpgt_epi8(upper_z, bytes));
  return static_cast<uint32_t>(_mm256_movemask_epi8(uppers));
}

inline int64_t count_ascii_prefix_avx2(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(str + pos));
    const uint32_t mask = static_cast<uint32_t>(_mm256_movemask_epi8(bytes));
    if (0 != mask) {
      return pos + __builtin_ctz(mask);
    }
  }
  return pos + oceanbase::storage::ascii::count_ascii_prefix_scalar(str + pos, len - pos);
}

inline int64_t count_leading_delimiter_avx2(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const uint32_t mask = get_ascii_alnum_mask(str + pos);
    if (0 != mask) {
      return pos + __builtin_ctz(mask);
    }
  }
  return pos + oceanbase::storage::ascii::count_leading_delimiter_scalar(str + pos, len - pos);
}

inline int64_t count_leading_token_avx2(const char *str, const int64_t len)
{
  int64_t pos = 0;
  for (; pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const uint32_t mask = get_ascii_alnum_mask(str + pos);
    if (UINT32_MAX != mask) {
      return pos + __builtin_ctz(~mask);
    }
  }
  return pos + oceanbase::storage::ascii::count_leading_token_scalar(str + pos, len - pos);
}

inline bool has_ascii_upper_avx2(const char *str, const int64_t len)
{
  bool found = false;
  int64_t pos = 0;
  for (; !found && pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    found = 0 != get_ascii_upper_mask(str + pos);
  }
  return found || oceanbase::storage::ascii::has_ascii_upper_scalar(str + pos, len - pos);
}

inline void lowercase_ascii_copy_avx2(const char *src, const int64_t len, char *dst)
{
  int64_t pos = 0;
  const __m256i upper_a = _mm256_set1_epi8('A' - 1);
  const __m256i upper_z = _mm256_set1_epi8('Z' + 1);
  const __m256i flip_mask = _mm256_set1_epi8(32);
  for (; pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + pos));
    const __m256i uppers = _mm256_and_si256(_mm256_cmpgt_epi8(bytes, upper_a), _mm256_cmpgt_epi8(upper_z, bytes));
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + pos),
                        _mm256_xor_si256(bytes, _mm256_and_si256(uppers, flip_mask)));
  }
  oceanbase::storage::ascii::lowercase_ascii_copy_scalar(src + pos, len - pos, dst + pos);
}

inline bool is_ascii_string_avx2(const char *str, const int64_t len)
{
  bool is_ascii = true;
  int64_t pos = 0;
  for (; is_ascii && pos + AVX2_BYTES <= len; pos += AVX2_BYTES) {
    const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(str + pos));
    is_ascii = 0 == _mm256_movemask_epi8(bytes);
  }
  return is_ascii && oceanbase::storage::ascii::is_ascii_string_scalar(str + pos, len - pos);
}

)

inline int64_t count_ascii_prefix(const char *str, const int64_t len)
{
  int64_t prefix_len = 0;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && common::is_arch_supported(ObTargetArch::AVX2)) {
    prefix_len = specific::avx2::count_ascii_prefix_avx2(str, len);
  } else
#endif
  {
    prefix_len = count_ascii_prefix_scalar(str, len);
  }
  return prefix_len;
}

inline int64_t count_leading_delimiter(const char *str, const int64_t len)
{
  int64_t skip = 0;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && common::is_arch_supported(ObTargetArch::AVX2)) {
    skip = specific::avx2::count_leading_delimiter_avx2(str, len);
  } else
#endif
  {
    skip = count_leading_delimiter_scalar(str, len);
  }
  return skip;
}

inline int64_t count_leading_token(const char *str, const int64_t len)
{
  int64_t token_len = 0;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && common::is_arch_supported(ObTargetArch::AVX2)) {
    token_len = specific::avx2::count_leading_token_avx2(str, len);
  } else
#endif
  {
    token_len = count_leading_token_scalar(str, len);
  }
  return token_len;
}

inline bool has_ascii_upper(const char *str, const int64_t len)
{
  bool found = false;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && common::is_arch_supported(ObTargetArch::AVX2)) {
    found = specific::avx2::has_ascii_upper_avx2(str, len);
  } else
#endif
  {
    found = has_ascii_upper_scalar(str, len);
  }
  return found;
}

inline void lowercase_ascii_copy(const char *src, const int64_t len, char *dst)
{
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && common::is_arch_supported(ObTargetArch::AVX2)) {
    specific::avx2::lowercase_ascii_copy_avx2(src, len, dst);
  } else
#endif
  {
    lowercase_ascii_copy_scalar(src, len, dst);
  }
}

inline bool is_ascii_string(const char *str, const int64_t len)
{
  bool is_ascii = true;
#if OB_USE_MULTITARGET_CODE
  if (len >= specific::avx2::AVX2_BYTES && common::is_arch_supported(ObTargetArch::AVX2)) {
    is_ascii = specific::avx2::is_ascii_string_avx2(str, len);
  } else
#endif
  {
    is_ascii = is_ascii_string_scalar(str, len);
  }
  return is_ascii;
}

} // namespace ascii
} // namespace storage
} // namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_UTILS_OB_FT_ASCII_UTILS_H_
