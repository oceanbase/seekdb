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

#ifndef _OCEANBASE_STORAGE_FTS_UTILS_UNICODE_UTILS_H_
#define _OCEANBASE_STORAGE_FTS_UTILS_UNICODE_UTILS_H_

#include "lib/charset/ob_ctype.h"

namespace oceanbase
{
namespace storage
{
class ObUnicodeBlockUtils final
{
public:
  static bool is_unicode_cn_number(const ob_wc_t unicode);

  static bool is_alpha(const ob_wc_t unicode);

  static bool is_arabic(const ob_wc_t unicode);

  static bool check_high_surrogate(const ob_wc_t unicode);

  static bool check_high_private_use_surrogate(const ob_wc_t unicode);

  static bool check_low_surrogate(const ob_wc_t unicode);

  static bool check_letter_connector(const ob_wc_t unicode);

  static bool check_num_connector(const ob_wc_t unicode);

  static bool check_ignore_as_single(const ob_wc_t unicode);

  static bool is_chinese(const ob_wc_t unicode);

  static bool is_other_cjk(const ob_wc_t unicode);

  static bool is_surrogate(const ob_wc_t unicode);

  static bool is_convertable_fullwidth(const ob_wc_t unicode);

private:
  // Do compare with u8 char
  // only for static number
  static ob_wc_t get_unicode_from_u8(const char *input, const uint8_t char_len);
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_UTILS_UNICODE_UTILS_H_
