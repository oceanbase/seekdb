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
#include "storage/blocksstable/ob_storage_cache_suite.h"
#include "share/ob_simple_mem_limit_getter.h"

namespace oceanbase
{
using namespace common;
static ObSimpleMemLimitGetter getter;

namespace blocksstable
{

class TestStorageCacheSuite : public ::testing::Test
{
public:
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestStorageCacheSuite, test_cache_suite)
{
  ASSERT_EQ(OB_NOT_INIT, OB_STORE_CACHE.init(1,1,2,3,4, 5, 10));
  ASSERT_EQ(OB_NOT_INIT, OB_STORE_CACHE.reset_priority(6,6,5,4,3,1));
  const int64_t bucket_num = 1024;
  const int64_t max_cache_size = 1024 * 1024 * 512;
  const int64_t block_size = common::OB_MALLOC_BIG_BLOCK_SIZE;
  ObKVGlobalCache::get_instance().init(&getter, bucket_num, max_cache_size, block_size);
  ASSERT_EQ(OB_SUCCESS, OB_STORE_CACHE.init(1,2,3,4,5, 10, 10));
  ASSERT_EQ(OB_SUCCESS, OB_STORE_CACHE.reset_priority(6,5,4,3,2));
  ASSERT_EQ(OB_INIT_TWICE, OB_STORE_CACHE.init(1,2,3,4,5, 10, 10));
  OB_STORE_CACHE.destroy();
  ObKVGlobalCache::get_instance().destroy();
  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::get_instance().init(&getter, bucket_num, max_cache_size, block_size));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.init(-1,2,3,4,1, 10, 10));
  OB_STORE_CACHE.destroy();
  ObKVGlobalCache::get_instance().destroy();
  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::get_instance().init(&getter, bucket_num, max_cache_size, block_size));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.init(-1,2,3,4,1, 10, 10));
  OB_STORE_CACHE.destroy();
  ObKVGlobalCache::get_instance().destroy();
  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::get_instance().init(&getter, bucket_num, max_cache_size, block_size));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.init(1,-2,3,4,1, 10, 10));
  OB_STORE_CACHE.destroy();
  ObKVGlobalCache::get_instance().destroy();
  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::get_instance().init(&getter, bucket_num, max_cache_size, block_size));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.init(1,2,-3,4,1, 10, 10));
  OB_STORE_CACHE.destroy();
  ObKVGlobalCache::get_instance().destroy();
  ASSERT_EQ(OB_SUCCESS, ObKVGlobalCache::get_instance().init(&getter, bucket_num, max_cache_size, block_size));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.init(1,2,3,-4,1, 10, 10));
  ObKVGlobalCache::get_instance().destroy();
  OB_STORE_CACHE.destroy();
  ObKVGlobalCache::get_instance().init(&getter, bucket_num, max_cache_size, block_size);
  ASSERT_EQ(OB_SUCCESS, OB_STORE_CACHE.init(1,2,3,4, 5, 10, 10));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.reset_priority(-6,6,5,4,3,1));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.reset_priority(6,-6,5,4,3,1));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.reset_priority(6,6,-5,4,3,1));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.reset_priority(6,6,5,-4,3,1));
  ASSERT_EQ(OB_INVALID_ARGUMENT, OB_STORE_CACHE.reset_priority(6,6,5,4,-3,1));
  ObKVGlobalCache::get_instance().destroy();
}

}//namespace blocksstable
}//namespace oceanbase

int main(int argc, char** argv)
{
  OB_LOGGER.set_log_level("WARN");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
