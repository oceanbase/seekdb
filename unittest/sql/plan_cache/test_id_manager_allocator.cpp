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

#include "gtest/gtest.h"
#include "sql/plan_cache/ob_id_manager_allocator.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

class ObIdManagerAllocatorTest : public ::testing::Test
{
public:
  ObIdManagerAllocatorTest();
  virtual ~ObIdManagerAllocatorTest();
  void SetUp();
  void TearDown();
private:
  DISALLOW_COPY_AND_ASSIGN(ObIdManagerAllocatorTest);
};

ObIdManagerAllocatorTest::ObIdManagerAllocatorTest()
{
}

ObIdManagerAllocatorTest::~ObIdManagerAllocatorTest()
{
};

void ObIdManagerAllocatorTest::SetUp()
{
}

void ObIdManagerAllocatorTest::TearDown()
{
}

TEST_F(ObIdManagerAllocatorTest, basic_test)
{
  const int64_t size_cnt = 5;
  const int64_t loop_cnt = 1024;
  int64_t sizes[size_cnt] = { 1, 128, 1024, 1024 * 6, 1024 * 1024 };
  void *ptr[loop_cnt];

  for(int64_t i = 0; i < 3; ++i) {
    ObIdManagerAllocator alloc_impl;
    alloc_impl.init(sizes[i], 0, OB_SYS_TENANT_ID);
    for (int64_t idx = 0; idx < size_cnt; ++idx) {
      for (int64_t loop = 0; loop < loop_cnt; ++loop) {
        ptr[loop] = NULL;
        ptr[loop] = alloc_impl.alloc(sizes[idx]);
        ASSERT_TRUE(NULL != ptr[loop]);
      }
      for(int64_t loop = 0; loop <loop_cnt; ++loop) {
        alloc_impl.free(ptr[loop]);
        ptr[loop] = NULL;
      }
    }
  }
}

TEST_F(ObIdManagerAllocatorTest, failure_test)
{
  const int64_t size_cnt = 5;
  const int64_t loop_cnt = 1024;
  int64_t sizes[size_cnt] = { 1, 128, 1024, 1024 * 6, 1024 * 1024 };
  void *ptr[loop_cnt];

  for(int64_t i = 0; i < size_cnt; ++i) {
    ObIdManagerAllocator alloc_impl;
    for (int64_t idx = 0; idx < size_cnt; ++idx) {
      for (int64_t loop = 0; loop < loop_cnt; ++loop) {
        ptr[loop] = NULL;
        ptr[loop] = alloc_impl.alloc(sizes[idx]);
        ASSERT_TRUE(NULL == ptr[loop]);
      }
      for(int64_t loop = 0; loop <loop_cnt; ++loop) {
        alloc_impl.free(ptr[loop]);
        ptr[loop] = NULL;
      }
    }
  }
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
