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

#ifndef CHARSET_GB18030_TAB_H_
#define CHARSET_GB18030_TAB_H_

#include "lib/charset/ob_ctype.h"

extern unsigned char ctype_gb18030[257];
extern unsigned char to_lower_gb18030[];
extern unsigned char to_upper_gb18030[];
extern unsigned char sort_order_gb18030[];

extern unsigned char sort_order_gb18030_ci[];
const extern ObUnicaseInfoChar *ob_caseinfo_pages_gb18030[256];
extern ObUnicaseInfo ob_caseinfo_gb18030;
extern const uint16_t tab_gb18030_2_uni[24192];
extern const uint16_t tab_gb18030_4_uni[10922];
extern const uint16_t tab_uni_gb18030_p1[40742];
extern const uint16_t tab_uni_gb18030_p2[3897];
extern const uint16_t gb18030_2_weight_py[];
extern const uint16_t gb18030_4_weight_py_p1[];
extern const uint16_t gb18030_4_weight_py_p2[];

/*[GB+82358F33, GB+82359134] or [U+9FA6, U+9FBB]*/
const static int GB_2022_CNT_PART_1 = 0x9FBB - 0x9FA6 + 1;
/*[GB+84318236, GB+84318537]*/
const static int GB_2022_CNT_PART_2 = (0x85 - 0x82) * 10 + (0x37 - 0x36) + 1;

extern uint16 tab_gb18030_2022_2_uni[];
extern uint16 tab_gb18030_2022_4_uni[];
extern uint16 tab_uni_gb18030_2022_p1[];
extern uint16 tab_uni_gb18030_2022_p2[];

#endif  // CHARSET_GB18030_TAB_H_
