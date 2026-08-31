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

#ifndef OCEANBASE_LIB_CHARSET_STR_UCA_TYPE_H_
#define OCEANBASE_LIB_CHARSET_STR_UCA_TYPE_H_

// seekdb currently exposes only utf8mb4_unicode_ci. It uses the fixed UCA 4.0
// table and has no tailoring, contractions, or locale-specific reordering.
struct ObUCAInfo {
  ob_wc_t maxchar;
  uchar *lengths;
  uint16 **weights;
};

#endif // OCEANBASE_LIB_CHARSET_STR_UCA_TYPE_H_
