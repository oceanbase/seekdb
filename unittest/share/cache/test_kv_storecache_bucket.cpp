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

} // namespace common
} // namespace oceanbase
