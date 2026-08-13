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

#ifndef OCEANBASE_DATA_PLANE_API_ENCODING_OB_ASCII_UTIL_H_
#define OCEANBASE_DATA_PLANE_API_ENCODING_OB_ASCII_UTIL_H_

#include "lib/charset/ob_charset.h"

namespace oceanbase
{
namespace storage
{

OB_INLINE bool can_do_ascii_optimize(common::ObCollationType cs_type)
{
  return common::CS_TYPE_UTF8MB4_GENERAL_CI == cs_type
      || common::CS_TYPE_UTF8MB4_BIN == cs_type
      || common::CS_TYPE_UTF8MB4_UNICODE_CI == cs_type
      || common::CS_TYPE_GBK_CHINESE_CI == cs_type
      || common::CS_TYPE_GBK_BIN == cs_type;
}

OB_INLINE bool is_ascii_less_8(const char *str, int64_t len)
{
  bool is_not_ascii = true;
  const uint8_t *val = reinterpret_cast<const uint8_t *>(str);
  switch (len) {
    case 0:
      is_not_ascii = false;
      break;
    case 1:
      is_not_ascii = (0x80 & val[0]);
      break;
    case 2:
      is_not_ascii = 0x8080 & *reinterpret_cast<const uint16_t *>(val);
      break;
    case 3:
      is_not_ascii = (0x8080 & *reinterpret_cast<const uint16_t *>(val)) | (0x80 & val[2]);
      break;
    case 4:
      is_not_ascii = (0x80808080U & *reinterpret_cast<const uint32_t *>(val));
      break;
    case 5:
      is_not_ascii = (0x80808080U & *reinterpret_cast<const uint32_t *>(val)) | (0x80 & val[4]);
      break;
    case 6:
      is_not_ascii = (0x80808080U & *reinterpret_cast<const uint32_t *>(val))
          | (0x8080 & *reinterpret_cast<const uint16_t *>(val + 4));
      break;
    case 7:
      is_not_ascii = (0x80808080U & *reinterpret_cast<const uint32_t *>(val))
          | (0x80808080U & *reinterpret_cast<const uint32_t *>(val + 3));
      break;
    default:
      break;
  }
  return !is_not_ascii;
}

OB_INLINE bool is_ascii_str(const char *str, const int64_t len)
{
  bool is_ascii = true;
  if (len >= 8) {
    const int64_t length = len / 8;
    const uint64_t *vals = reinterpret_cast<const uint64_t *>(str);
    for (int64_t i = 0; is_ascii && i < length; ++i) {
      if (vals[i] & 0x8080808080808080UL) {
        is_ascii = false;
      }
    }
    is_ascii = is_ascii && is_ascii_less_8(str + len / 8 * 8, len % 8);
  } else {
    is_ascii = is_ascii_less_8(str, len);
  }
  return is_ascii;
}

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_ENCODING_OB_ASCII_UTIL_H_
