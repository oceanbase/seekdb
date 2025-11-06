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

#ifndef CHARSET_EUCKR_TAB_H_
#define CHARSET_EUCKR_TAB_H_

#include "lib/charset/ob_ctype.h"
extern unsigned char ctype_euc_kr[257];
extern unsigned char to_lower_euc_kr[];
extern  unsigned char to_upper_euc_kr[];
extern  unsigned char sort_order_euc_kr[];
extern ObUnicaseInfo ob_caseinfo_euckr;
extern int func_ksc5601_uni_onechar(int code);
extern int func_uni_ksc5601_onechar(int code);

#define iseuc_kr_head(c) ((0x81 <= (uint8_t)(c) && (uint8_t)(c) <= 0xfe))
#define iseuc_kr_tail1(c) ((uint8_t)(c) >= 0x41 && (uint8_t)(c) <= 0x5A)
#define iseuc_kr_tail2(c) ((uint8_t)(c) >= 0x61 && (uint8_t)(c) <= 0x7A)
#define iseuc_kr_tail3(c) ((uint8_t)(c) >= 0x81 && (uint8_t)(c) <= 0xFE)
#define iseuc_kr_tail(c) \
  (iseuc_kr_tail1(c) || iseuc_kr_tail2(c) || iseuc_kr_tail3(c))

#endif  // CHARSET_EUCKR_TAB_H_
