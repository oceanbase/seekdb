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
 
#ifndef CHARSET_UJIS_TAB_H_
#define CHARSET_UJIS_TAB_H_

#include "lib/charset/ob_ctype.h"

extern unsigned char ctype_ujis[257];
extern unsigned char to_lower_ujis[];
extern unsigned char to_upper_ujis[];
extern ObUnicaseInfo ob_caseinfo_ujis;
extern unsigned char sort_order_ujis[];
extern uint16_t jisx0208_eucjp_to_unicode[65536];
extern uint16_t jisx0212_eucjp_to_unicode[65536];
extern uint16_t unicode_to_jisx0208_eucjp[65536];
extern uint16_t unicode_to_jisx0212_eucjp[65536];

#define isujis(c) ((0xa1 <= ((c)&0xff) && ((c)&0xff) <= 0xfe))
#define iskata(c) ((0xa1 <= ((c)&0xff) && ((c)&0xff) <= 0xdf))
#define isujis_ss2(c) (((c)&0xff) == 0x8e)
#define isujis_ss3(c) (((c)&0xff) == 0x8f)

#endif  // CHARSET_UJIS_TAB_H_
