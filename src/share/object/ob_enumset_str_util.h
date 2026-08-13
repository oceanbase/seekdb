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
#ifndef OCEANBASE_SHARE_OBJECT_OB_ENUMSET_STR_UTIL_H_
#define OCEANBASE_SHARE_OBJECT_OB_ENUMSET_STR_UTIL_H_
// Pure enum/set internal-value to string conversion.
// (enum/set internal values -> string),depends only on lib;sql-side original methods forward here。
#include "lib/string/ob_string.h"
#include "lib/container/ob_iarray.h"
#include "lib/charset/ob_charset.h"
namespace oceanbase {
namespace common {
class ObTextStringResult;
namespace enumset_str {
int enum_to_str(const uint64_t enum_val, const ObIArray<ObString> &str_values,
                ObTextStringResult &text_result);
int set_to_str(const ObCollationType cs_type, const uint64_t set_val,
               const ObIArray<ObString> &str_values, ObTextStringResult &text_result);
}  // namespace enumset_str
}  // namespace common
}  // namespace oceanbase
#endif
