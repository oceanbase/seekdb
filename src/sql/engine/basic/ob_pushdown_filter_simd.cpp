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

#define USING_LOG_PREFIX SQL_ENG

#if defined(__GNUC__) && defined(__x86_64__) && !defined(_MSC_VER)
#include <immintrin.h>
#endif

#include "ob_pushdown_filter.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace sql
{

// Native values inline scalar payloads; filter truth is independent of an
// external res_buf and of the old 12-byte descriptor's packed NULL word.
void mark_filtered_datums_simd(const common::Datum *datums, const uint64_t *,
                                const int64_t size, ObBitVector &bit_vec)
{
  bit_vec.reset(size);
  for (int64_t i = 0; i < size; ++i) {
    if (is_row_filtered(datums[i])) { bit_vec.set(i); }
  }
}

} // end namespace sql
} // end namespace oceanbase
