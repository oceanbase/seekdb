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
#include "logservice/ob_log_allocator.h"
#include "logservice/ob_log_allocator_mgr.h"
#include "lib/alloc/alloc_func.h"

namespace oceanbase
{
using namespace common;

namespace unittest
{

TEST(TestLogAllocator, test_managed_allocator)
{
  PALF_LOG(INFO, "test_log_allocator begin", "allocator size", sizeof(ObLogAllocator));
  const int init_ret = LOG_ALLOCATOR_MGR_INSTANCE.init();
  ASSERT_TRUE(OB_SUCCESS == init_ret || OB_INIT_TWICE == init_ret);

  ObILogAllocator *allocator = nullptr;
  ObILogAllocator *same_allocator = nullptr;
  ASSERT_EQ(OB_SUCCESS, LOG_ALLOCATOR_MGR_INSTANCE.get_log_allocator(allocator));
  ASSERT_EQ(OB_SUCCESS, LOG_ALLOCATOR_MGR_INSTANCE.get_log_allocator(same_allocator));
  ASSERT_NE(nullptr, allocator);
  EXPECT_EQ(allocator, same_allocator);

  void *buffer = allocator->alloc(1024);
  ASSERT_NE(nullptr, buffer);
  allocator->free(buffer);
  EXPECT_EQ(OB_SUCCESS, LOG_ALLOCATOR_MGR_INSTANCE.delete_log_allocator());
}

} // END of unittest
} // end of oceanbase

int main(int argc, char **argv)
{
  system("rm -f ./test_log_allocator.log*");
  OB_LOGGER.set_file_name("test_log_allocator.log", true);
  OB_LOGGER.set_log_level("TRACE");
  PALF_LOG(INFO, "begin unittest::test_log_allocator");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
