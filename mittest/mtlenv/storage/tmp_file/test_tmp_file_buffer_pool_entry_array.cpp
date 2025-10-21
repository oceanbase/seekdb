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
#define USING_LOG_PREFIX STORAGE
#define protected public
#define private public
#include "lib/random/ob_random.h"
#include "storage/tmp_file/ob_tmp_file_write_buffer_pool_entry_array.h"

namespace oceanbase
{
using namespace common;
using namespace tmp_file;

class TestBufferPooEntryArray : public ::testing::Test
{
public:
  TestBufferPooEntryArray() {}
  virtual ~TestBufferPooEntryArray() = default;
};

TEST_F(TestBufferPooEntryArray, test_basic)
{
  ObPageEntry entry;
  ObTmpWriteBufferPoolEntryArray fat;
  ASSERT_EQ(OB_NOT_INIT, fat.push_back(entry));

  ASSERT_EQ(OB_SUCCESS, fat.init());

  const int64_t MAX_ENTRY_COUNT = 131072 * 100; // 100GB buffer pool / 8192KB page_size
  for (int32_t i = 0; i < MAX_ENTRY_COUNT; ++i) {
    entry.page_key_.virtual_page_id_ = i;
    ASSERT_EQ(OB_SUCCESS, fat.push_back(entry));
  }
  ASSERT_EQ(MAX_ENTRY_COUNT, fat.size());
  ASSERT_EQ(MAX_ENTRY_COUNT, fat.count());

  const int32_t RANDOM_CHECK_NUM = 1000;
  for (int32_t i = 0; i < RANDOM_CHECK_NUM; ++i) {
    int64_t idx = ObRandom::rand(0, MAX_ENTRY_COUNT);
    ASSERT_EQ(idx, fat[idx].page_key_.virtual_page_id_);
  }

  for (int32_t i = 0; i < MAX_ENTRY_COUNT; ++i) {
    fat.pop_back();
  }
  ASSERT_EQ(0, fat.size());
  ASSERT_EQ(0, fat.count());

  fat.pop_back();
  ASSERT_EQ(0, fat.size());
  ASSERT_EQ(0, fat.buckets_.size());
}

TEST_F(TestBufferPooEntryArray, test_random_push_and_pop)
{
  ObPageEntry entry;
  ObTmpWriteBufferPoolEntryArray fat;
  ASSERT_EQ(OB_SUCCESS, fat.init());

  int64_t pop_cnt = 0;
  const int64_t MAX_ENTRY_COUNT = 131072 * 10;
  for (int32_t i = 0; i < MAX_ENTRY_COUNT; ++i) {
    entry.page_key_.virtual_page_id_ = i;
    ASSERT_EQ(OB_SUCCESS, fat.push_back(entry));

    if (ObRandom::rand(0, 100) < 50) {
      fat.pop_back();
      pop_cnt += 1;
    }
  }
  ASSERT_EQ(MAX_ENTRY_COUNT - pop_cnt, fat.size());
  ASSERT_EQ(MAX_ENTRY_COUNT - pop_cnt, fat.count());

  for (int32_t i = 1; i < fat.size(); ++i) {
    ASSERT_LT(fat[i - 1].page_key_.virtual_page_id_, fat[i].page_key_.virtual_page_id_);
  }
}

TEST_F(TestBufferPooEntryArray, test_array_shrinking)
{
  ObPageEntry entry;
  ObTmpWriteBufferPoolEntryArray fat;
  ASSERT_EQ(OB_SUCCESS, fat.init());

  const int64_t MAX_ENTRY_COUNT = 131072;
  for (int32_t i = 0; i < MAX_ENTRY_COUNT / 2; ++i) {
    entry.page_key_.virtual_page_id_ = i;
    ASSERT_EQ(OB_SUCCESS, fat.push_back(entry));
  }
  ASSERT_EQ(MAX_ENTRY_COUNT / 2, fat.size());
  ASSERT_EQ(7, fat.buckets_.size());

  for (int32_t i = MAX_ENTRY_COUNT / 2; i < MAX_ENTRY_COUNT; ++i) {
    entry.page_key_.virtual_page_id_ = i;
    ASSERT_EQ(OB_SUCCESS, fat.push_back(entry));
  }
  ASSERT_EQ(13, fat.buckets_.size());

  int64_t bucket_capacity = fat.MAX_BUCKET_CAPACITY;
  for (int32_t i = 0; i < MAX_ENTRY_COUNT; ++i) {
    fat.pop_back();
    int64_t bucket_num = (fat.size() + bucket_capacity - 1) / bucket_capacity;
    ASSERT_EQ(bucket_num, fat.buckets_.size());
  }
}

} // namespace oceanbase

int main(int argc, char **argv)
{
  int ret = 0;
  system("rm -f ./test_tmp_file_buffer_pool_entry_array.log*");
  OB_LOGGER.set_file_name("test_tmp_file_buffer_pool_entry_array.log", true);
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
