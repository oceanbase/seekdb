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

#define USING_LOG_PREFIX SQL_EXE
#include <gtest/gtest.h>

#include "sql/ob_sql_init.h"
#include "sql/engine/px/ob_px_util.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::storage;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

class ObSplitSqcTaskTest : public ::testing::Test
{
public:

  const static int64_t TEST_PARTITION_COUNT = 5;
  const static int64_t TEST_SPLIT_TASK_COUNT = 8;

  ObSplitSqcTaskTest() = default;
  virtual ~ObSplitSqcTaskTest() = default;
  virtual void SetUp() {};
  virtual void TearDown() {};

private:
  // disallow copy
  ObSplitSqcTaskTest(const ObSplitSqcTaskTest &other);
  ObSplitSqcTaskTest& operator=(const ObSplitSqcTaskTest &other);
};

TEST_F(ObSplitSqcTaskTest, split_task_test) {
  int ret = OB_SUCCESS;
  {
    int64_t parallel = 12;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(33);
    sqc_part_count.push_back(44);
    sqc_part_count.push_back(100-33-44);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(3, results.count());
    ASSERT_EQ(4, results[0]);
    ASSERT_EQ(5, results[1]);
    ASSERT_EQ(3, results[2]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    int64_t parallel = 12;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(1);
    sqc_part_count.push_back(2);
    sqc_part_count.push_back(1);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(3, results.count());
    ASSERT_EQ(3, results[0]);
    ASSERT_EQ(6, results[1]);
    ASSERT_EQ(3, results[2]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    int64_t parallel = 15;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(1);
    sqc_part_count.push_back(1);
    sqc_part_count.push_back(11);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(3, results.count());
    ASSERT_EQ(2, results[0]);
    ASSERT_EQ(1, results[1]);
    ASSERT_EQ(12, results[2]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    int64_t parallel = 15;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(4);
    sqc_part_count.push_back(5);
    sqc_part_count.push_back(5);
    sqc_part_count.push_back(6);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(4, results.count());
    ASSERT_EQ(3, results[0]);
    ASSERT_EQ(4, results[1]);
    ASSERT_EQ(4, results[2]);
    ASSERT_EQ(4, results[3]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    int64_t parallel = 12;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(2);
    sqc_part_count.push_back(3);
    sqc_part_count.push_back(4);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(3, results.count());
    ASSERT_EQ(3, results[0]);
    ASSERT_EQ(4, results[1]);
    ASSERT_EQ(5, results[2]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    // When partition skew is extreme, can the sqc with fewer partitions be assigned to threads.
    int64_t parallel = 15;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(100);
    sqc_part_count.push_back(100);
    sqc_part_count.push_back(1);
    sqc_part_count.push_back(1);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(4, results.count());
    ASSERT_EQ(7, results[0]);
    ASSERT_EQ(6, results[1]);
    ASSERT_EQ(1, results[2]);
    ASSERT_EQ(1, results[3]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    // When partition skew is extreme, can the sqc with fewer partitions be assigned to threads.
    int64_t parallel = 203;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(100);
    sqc_part_count.push_back(100);
    sqc_part_count.push_back(1);
    sqc_part_count.push_back(1);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(4, results.count());
    ASSERT_EQ(101, results[0]);
    ASSERT_EQ(100, results[1]);
    ASSERT_EQ(1, results[2]);
    ASSERT_EQ(1, results[3]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    // When partition skew is extreme, can the sqc with fewer partitions be assigned to threads.
    int64_t parallel = 4;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(10000);
    sqc_part_count.push_back(1);
    sqc_part_count.push_back(1);
    sqc_part_count.push_back(1);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(4, results.count());
    ASSERT_EQ(1, results[0]);
    ASSERT_EQ(1, results[1]);
    ASSERT_EQ(1, results[2]);
    ASSERT_EQ(1, results[3]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    // When partition skew is extreme, can the sqc with fewer partitions be assigned to threads.
    int64_t parallel = 4;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(10000);
    sqc_part_count.push_back(10000);
    sqc_part_count.push_back(10000);
    sqc_part_count.push_back(1);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(4, results.count());
    ASSERT_EQ(1, results[0]);
    ASSERT_EQ(1, results[1]);
    ASSERT_EQ(1, results[2]);
    ASSERT_EQ(1, results[3]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, parallel);
  }

  {
    // when parallel < sqc_count, can it be one thread per sqc.
    int64_t parallel = 1;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    int64_t total_thread = 0;
    sqc_part_count.push_back(2);
    sqc_part_count.push_back(3);
    sqc_part_count.push_back(4);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_SUCCESS);
    ASSERT_EQ(3, results.count());
    ASSERT_EQ(1, results[0]);
    ASSERT_EQ(1, results[1]);
    ASSERT_EQ(1, results[2]);
    ARRAY_FOREACH(results, idx) {
      total_thread += results[idx];
    }
    ASSERT_EQ(total_thread, sqc_part_count.count());
  }

  {
    // See if illegal input throws an error as expected
    int64_t parallel = 1;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    sqc_part_count.push_back(2);
    sqc_part_count.push_back(-1);
    sqc_part_count.push_back(4);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_ERR_UNEXPECTED);
  }

  {
    // See if illegal input throws an error as expected
    int64_t parallel = 0;
    ObArray<int64_t> sqc_part_count;
    ObArray<int64_t> results;
    sqc_part_count.push_back(2);
    sqc_part_count.push_back(1);
    sqc_part_count.push_back(4);
    ret = ObPXServerAddrUtil::split_parallel_into_task(parallel, sqc_part_count, results);
    ASSERT_TRUE(ret == OB_INVALID_ARGUMENT);
  }

}

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("TRACE");
  //oceanbase::common::ObLogger::get_logger().set_log_level("TRACE");
  init_sql_factories();
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
