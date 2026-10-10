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

#include "gtest/gtest.h"
#include "share/cache/ob_kvcache_store.h"

TEST(TestKVCacheMemoryQuota, fixed_block_admission)
{
  EXPECT_EQ(0,
            oceanbase::common::ObKVCacheStore::compute_fixed_cache_limit(63,
                                                                          64));
  EXPECT_EQ(192,
            oceanbase::common::ObKVCacheStore::compute_fixed_cache_limit(255,
                                                                          64));
  EXPECT_TRUE(
      oceanbase::common::ObKVCacheStore::can_reserve_cache_size(128, 64, 192));
  EXPECT_FALSE(
      oceanbase::common::ObKVCacheStore::can_reserve_cache_size(192, 64, 192));
}
