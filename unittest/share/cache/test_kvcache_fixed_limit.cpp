/*
 * Copyright (c) 2026 OceanBase.
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

#include <gtest/gtest.h>

#include "share/cache/ob_kvcache_store.h"

namespace oceanbase
{
namespace common
{

TEST(TestKVCacheFixedLimit, aligns_thirty_percent_to_block_size)
{
  const int64_t mib = 1L << 20;
  const int64_t memory_budget = 300L * mib;
  const int64_t block_size = 2L * mib;

  EXPECT_EQ(90L * mib,
            ObKVCacheStore::compute_fixed_cache_limit(memory_budget, block_size));
  EXPECT_EQ(88L * mib,
            ObKVCacheStore::compute_fixed_cache_limit(299L * mib, block_size));

  const int64_t expected_achunk_limit =
      (memory_budget * 30 / 100) / lib::ACHUNK_SIZE * lib::ACHUNK_SIZE;
  EXPECT_EQ(expected_achunk_limit,
            ObKVCacheStore::compute_fixed_cache_limit(memory_budget, lib::ACHUNK_SIZE));
  EXPECT_EQ(0, ObKVCacheStore::compute_fixed_cache_limit(0, block_size));
  EXPECT_EQ(0, ObKVCacheStore::compute_fixed_cache_limit(memory_budget, 0));

  const int64_t max_limit = INT64_MAX / 100 * 30 + INT64_MAX % 100 * 30 / 100;
  EXPECT_EQ(max_limit / block_size * block_size,
            ObKVCacheStore::compute_fixed_cache_limit(INT64_MAX, block_size));
}

TEST(TestKVCacheFixedLimit, computes_store_block_excess)
{
  const int64_t mib = 1L << 20;
  const int64_t memory_budget = 300L * mib;
  const int64_t block_size = 2L * mib;

  EXPECT_EQ(0, ObKVCacheStore::compute_fixed_wash_size(88L * mib,
                                                       memory_budget,
                                                       block_size));
  EXPECT_EQ(0, ObKVCacheStore::compute_fixed_wash_size(90L * mib,
                                                       memory_budget,
                                                       block_size));
  EXPECT_EQ(2L * mib, ObKVCacheStore::compute_fixed_wash_size(92L * mib,
                                                              memory_budget,
                                                              block_size));
  EXPECT_EQ(20L * mib, ObKVCacheStore::compute_fixed_wash_size(110L * mib,
                                                               memory_budget,
                                                               block_size));
}

} // namespace common
} // namespace oceanbase
