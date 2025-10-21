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

#ifndef CHARSET_CP932_TAB_H_
#define CHARSET_CP932_TAB_H_

#include "lib/charset/ob_ctype.h"
extern unsigned char ctype_cp932[257];
extern unsigned char to_lower_cp932[];
extern unsigned char to_upper_cp932[];
extern unsigned char sort_order_cp932[];
extern ObUnicaseInfo ob_caseinfo_cp932;
extern  uint16_t cp932_to_unicode[65536];
extern  uint16_t unicode_to_cp932[65536];

#define iscp932head(c) \
  ((0x81 <= (c) && (c) <= 0x9f) || ((0xe0 <= (c)) && (c) <= 0xfc))
#define iscp932tail(c) \
  ((0x40 <= (c) && (c) <= 0x7e) || (0x80 <= (c) && (c) <= 0xfc))

#endif  // CHARSET_CP932_TAB_H_ 
