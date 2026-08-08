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

#include "share/vector/ob_vector_index_mode.h"

#include "lib/charset/ob_charset.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{

bool is_vector_index_sync_mode_async(
    const common::ObString &index_params,
    const bool is_hnsw_heap_table)
{
  bool is_async = false;
  const common::ObCollationType calc_cs_type = common::CS_TYPE_UTF8MB4_GENERAL_CI;
  const uint32_t immediate_pos = common::ObCharset::locate(
      calc_cs_type,
      index_params.ptr(),
      index_params.length(),
      "SYNC_MODE=IMMEDIATE",
      18,
      1);
  if (immediate_pos > 0) {
    is_async = false;
  } else {
    const uint32_t async_pos = common::ObCharset::locate(
        calc_cs_type,
        index_params.ptr(),
        index_params.length(),
        "SYNC_MODE=ASYNC",
        14,
        1);
    if (async_pos > 0) {
      is_async = true;
    } else if (is_hnsw_heap_table) {
      // HNSW heap tables default to asynchronous maintenance.
      is_async = true;
    }
  }
  return is_async;
}

} // namespace share
} // namespace oceanbase
