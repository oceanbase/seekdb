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
#ifndef CHARSET_HKSCS31_TAB_H_
#define CHARSET_HKSCS31_TAB_H_

#include "lib/charset/ob_ctype.h"
#include <unordered_map>

using std::unordered_map;
using std::pair;

extern const int size_of_hkscs31_to_uni_map_array;
extern const int size_of_uni_to_hkscs31_map_array;

extern const std::pair<uint16_t,int> hkscs31_to_uni_map_array[];
extern unordered_map<uint16_t, int> hkscs31_to_uni_map;
extern const std::pair<int,uint16_t>  uni_to_hkscs31_map_array[];
extern unordered_map<int,uint16_t> uni_to_hkscs31_map;

int func_hkscs31_uni_onechar(int code);
int func_uni_hkscs31_onechar(int code);


#endif  // CHARSET_HKSCS31_TAB_H_
