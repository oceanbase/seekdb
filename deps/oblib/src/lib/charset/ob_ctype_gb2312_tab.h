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

#ifndef CHARSET_GB2312_TAB_H_
#define CHARSET_GB2312_TAB_H_

#include "lib/charset/ob_ctype.h"
extern unsigned char ctype_gb2312[257];
// clang-format off
extern unsigned char to_lower_gb2312[];
extern unsigned char to_upper_gb2312[];
extern unsigned char sort_order_gb2312[];
// clang-format on

#define isgb2312head(c) (0xa1 <= (unsigned char)(c) && (unsigned char)(c) <= 0xf7)
#define isgb2312tail(c) (0xa1 <= (unsigned char)(c) && (unsigned char)(c) <= 0xfe)
extern ObUnicaseInfo ob_caseinfo_gb2312;
extern int func_gb2312_uni_onechar(int code);
extern int func_uni_gb2312_onechar(int code);

#endif  // CHARSET_GB2312_TAB_H_
