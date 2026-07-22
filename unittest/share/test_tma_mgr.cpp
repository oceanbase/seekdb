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
#include "logservice/ob_log_allocator_mgr.h"
#include "logservice/ob_log_allocator.h"

namespace oceanbase
{
using namespace common;

namespace unittest
{

TEST(TestTMAMgr, test_tma_mgr)
{
  PALF_LOG(INFO, "test_tma_mgr begin", "log allocator size", sizeof(ObLogAllocator));
  ObMallocAllocator *malloc_allocator = ObMallocAllocator::get_instance();
  ASSERT_EQ(OB_SUCCESS, LOG_ALLOCATOR_MGR_INSTANCE.init());

  // single-tenant: create the log allocator (idempotent)
  ObILogAllocator *log_allocator = NULL;
  EXPECT_EQ(OB_SUCCESS, LOG_ALLOCATOR_MGR_INSTANCE.get_log_allocator(log_allocator));
  EXPECT_TRUE(NULL != log_allocator);
  ObILogAllocator *log_allocator2 = NULL;
  EXPECT_EQ(OB_SUCCESS, LOG_ALLOCATOR_MGR_INSTANCE.get_log_allocator(log_allocator2));
  EXPECT_EQ(log_allocator, log_allocator2);
  PALF_LOG(INFO, "after create TMA", "log allocator size", sizeof(ObLogAllocator), "allocator hold", \
      malloc_allocator->get_total_hold());

  // delete then re-create
  EXPECT_EQ(OB_SUCCESS, LOG_ALLOCATOR_MGR_INSTANCE.delete_log_allocator());
  log_allocator = NULL;
  EXPECT_EQ(OB_SUCCESS, LOG_ALLOCATOR_MGR_INSTANCE.get_log_allocator(log_allocator));
  EXPECT_TRUE(NULL != log_allocator);
  PALF_LOG(INFO, "after delete TMA", "log allocator size", sizeof(ObLogAllocator), "allocator hold", \
      malloc_allocator->get_total_hold());
  PALF_LOG(INFO, "test_tma_mgr end");
}

} // END of unittest
} // end of oceanbase

int main(int argc, char **argv)
{
  system("rm -f ./test_tma_mgr.log*");
  OB_LOGGER.set_file_name("test_tma_mgr.log", true);
  OB_LOGGER.set_log_level("TRACE");
  PALF_LOG(INFO, "begin unittest::test_tma_mgr");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
