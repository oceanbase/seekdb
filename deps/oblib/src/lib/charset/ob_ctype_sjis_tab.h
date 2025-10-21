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

#ifndef CHARSET_SJIS_TAB_H_
#define CHARSET_SJIS_TAB_H_

#include "lib/charset/ob_ctype.h"

extern unsigned char ctype_sjis[];
extern unsigned char to_lower_sjis[];
extern unsigned char to_upper_sjis[];
extern unsigned char sort_order_sjis[];
extern const ObUnicaseInfoChar *ob_caseinfo_pages_sjis[];
extern const uint16 sjis_to_unicode[];
extern const uint16 unicode_to_sjis[];

#define issjishead(c) \
  ((0x81 <= (c) && (c) <= 0x9f) || ((0xe0 <= (c)) && (c) <= 0xfc))
#define issjistail(c) \
  ((0x40 <= (c) && (c) <= 0x7e) || (0x80 <= (c) && (c) <= 0xfc))
#define sjiscode(c, d) ((((uint)(uchar)(c)) << 8) | (uint)(uchar)(d))

#endif  // CHARSET_SJIS_TAB_H_
