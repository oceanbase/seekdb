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
#include "ob_px_bloom_filter.h"
#if defined(__x86_64__)
#include <immintrin.h>
#endif

using namespace oceanbase;
using namespace common;
using namespace sql;

#define LOG_HASH_COUNT 2

int ObPxBloomFilter::might_contain_simd(uint64_t hash, bool &is_match)
{
  int ret = OB_SUCCESS;
#if defined(__x86_64__)
  specific::avx512::inline_might_contain_simd(bits_array_, block_mask_, hash, is_match);
#else
  ret = might_contain_nonsimd(hash, is_match);
#endif
  return ret;
}

