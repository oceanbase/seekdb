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
#ifndef CHARSET_LATIN1_TAB_H_
#define CHARSET_LATIN1_TAB_H_

#include "lib/charset/ob_ctype.h"
extern unsigned char ctype_latin1[];
extern unsigned char to_lower_latin1[];
extern unsigned char to_upper_latin1[];
extern unsigned char sort_order_latin1[];
extern unsigned char sort_order_latin1_german1_ci[];
extern unsigned char sort_order_latin1_danish_ci[];
extern unsigned char sort_order_latin1_general_ci[];
extern unsigned char sort_order_latin1_general_cs[];
extern unsigned char sort_order_latin1_spanish_ci[];
extern unsigned char sort_order_latin1_de[];
extern const unsigned char combo1map[];
extern const unsigned char combo2map[];
extern unsigned short cs_to_uni[];
extern unsigned char *uni_to_cs[];

#endif  // CHARSET_LATIN1_TAB_H_
