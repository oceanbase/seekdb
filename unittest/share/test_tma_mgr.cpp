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
#include "logservice/ob_tenant_mutil_allocator_mgr.h"
#include "logservice/ob_tenant_mutil_allocator.h"

namespace oceanbase
{
using namespace common;

namespace unittest
{

TEST(TestTMAMgr, test_tma_mgr)
{
  PALF_LOG(INFO, "test_tma_mgr begin", "TMA size", sizeof(ObTenantMutilAllocator));
  ObMallocAllocator *malloc_allocator = ObMallocAllocator::get_instance();
  ASSERT_EQ(OB_SUCCESS, TMA_MGR_INSTANCE.init());

  // single-tenant: create the log allocator (idempotent)
  ObILogAllocator *tenant_allocator = NULL;
  EXPECT_EQ(OB_SUCCESS, TMA_MGR_INSTANCE.get_tenant_log_allocator(tenant_allocator));
  EXPECT_TRUE(NULL != tenant_allocator);
  ObILogAllocator *tenant_allocator2 = NULL;
  EXPECT_EQ(OB_SUCCESS, TMA_MGR_INSTANCE.get_tenant_log_allocator(tenant_allocator2));
  EXPECT_EQ(tenant_allocator, tenant_allocator2);
  PALF_LOG(INFO, "after create TMA", "TMA size", sizeof(ObTenantMutilAllocator), "tenant hold", \
      malloc_allocator->get_tenant_hold());

  // delete then re-create
  EXPECT_EQ(OB_SUCCESS, TMA_MGR_INSTANCE.delete_tenant_log_allocator());
  tenant_allocator = NULL;
  EXPECT_EQ(OB_SUCCESS, TMA_MGR_INSTANCE.get_tenant_log_allocator(tenant_allocator));
  EXPECT_TRUE(NULL != tenant_allocator);
  PALF_LOG(INFO, "after delete TMA", "TMA size", sizeof(ObTenantMutilAllocator), "tenant hold", \
      malloc_allocator->get_tenant_hold());
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
