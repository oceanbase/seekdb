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

#include <gtest/gtest.h>

#define private public
#include "share/cache/ob_kv_storecache.h"
#undef private

namespace oceanbase
{
namespace common
{

TEST(TestKVStorecacheBucket, low_memory_levels)
{
  ASSERT_EQ(196613, ObKVGlobalCache::bucket_num_array_[0]);
  ASSERT_EQ(393241, ObKVGlobalCache::bucket_num_array_[1]);
  ASSERT_EQ(786433, ObKVGlobalCache::bucket_num_array_[2]);
}

TEST(TestKVStorecacheBucket, selects_bucket_count_for_cache_capacity)
{
  const int64_t cache_memory_limit = (12LL << 30) / 5; // 2.4 GiB
  int64_t bucket_num = -1;

  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::calculate_suitable_bucket_num(
      cache_memory_limit, bucket_num));
  ASSERT_EQ(1572869, bucket_num);
}

TEST(TestKVStorecacheBucket, selects_bucket_level_at_capacity_boundary)
{
  const int64_t cache_memory_limit =
      ObKVGlobalCache::bucket_num_array_[2]
      * ObKVGlobalCache::KVCACHE_BYTES_PER_BUCKET;
  int64_t bucket_num = -1;

  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::calculate_suitable_bucket_num(
      cache_memory_limit, bucket_num));
  ASSERT_EQ(786433, bucket_num);
  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::calculate_suitable_bucket_num(
      cache_memory_limit + 1, bucket_num));
  ASSERT_EQ(1572869, bucket_num);
}

TEST(TestKVStorecacheBucket, handles_tiny_and_invalid_limits)
{
  int64_t bucket_num = -1;

  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::calculate_suitable_bucket_num(
      1, bucket_num));
  ASSERT_EQ(196613, bucket_num);
  ASSERT_EQ(OB_ERR_UNEXPECTED, ObKVGlobalCache::calculate_suitable_bucket_num(
      0, bucket_num));
  ASSERT_EQ(-1, bucket_num);
}

} // namespace common
} // namespace oceanbase
