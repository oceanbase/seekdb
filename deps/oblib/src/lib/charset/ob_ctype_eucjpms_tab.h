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
#ifndef CHARSET_EUCJPMS_TAB_H_
#define CHARSET_EUCJPMS_TAB_H_

#include "lib/charset/ob_ctype.h"
extern unsigned char ctype_eucjpms[257];
extern unsigned char to_lower_eucjpms[];
extern unsigned char to_upper_eucjpms[];
extern unsigned char sort_order_eucjpms[];
extern ObUnicaseInfo my_caseinfo_eucjpms;
extern const uint16_t jisx0208_eucjpms_to_unicode[65536];
extern const uint16_t unicode_to_jisx0208_eucjpms[65536];
extern const uint16_t jisx0212_eucjpms_to_unicode[65536];
extern const uint16_t unicode_to_jisx0212_eucjpms[65536];

#define iseucjpms(c) ((0xa1 <= ((c)&0xff) && ((c)&0xff) <= 0xfe))
#define iskata(c) ((0xa1 <= ((c)&0xff) && ((c)&0xff) <= 0xdf))
#define iseucjpms_ss2(c) (((c)&0xff) == 0x8e)
#define iseucjpms_ss3(c) (((c)&0xff) == 0x8f)

#endif  // CHARSET_EUCJPMS_TAB_H_
