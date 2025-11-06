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
#ifndef CHARSET_MACROMAN_TAB_H_
#define CHARSET_MACROMAN_TAB_H_

#include "lib/charset/ob_ctype.h"

extern unsigned char ctype_macroman_general_ci[];
extern unsigned char to_lower_macroman_general_ci[];
extern unsigned char to_upper_macroman_general_ci[];
extern unsigned char sort_order_macroman_general_ci[];
extern uint16 to_uni_macroman_general_ci[];
extern unsigned char ctype_macroman_bin[];
extern unsigned char to_lower_macroman_bin[];
extern unsigned char to_upper_macroman_bin[];
extern uint16 to_uni_macroman_bin[];

#endif  // CHARSET_MACROMAN_TAB_H_
