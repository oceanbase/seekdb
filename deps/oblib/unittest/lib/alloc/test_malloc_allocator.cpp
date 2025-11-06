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
#include "lib/allocator/ob_malloc.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;

TEST(TestMallocAllocator, idle)
{
  ObMallocAllocator *malloc_allocator = ObMallocAllocator::get_instance();
  const uint64_t tenant_id = OB_SYS_TENANT_ID;
  const uint64_t ctx_id = 1;
  auto ta = malloc_allocator->get_tenant_ctx_allocator(tenant_id, ctx_id);
  ASSERT_TRUE(NULL != ta);
  ASSERT_EQ(OB_SUCCESS, malloc_allocator->set_tenant_limit(tenant_id, 1024 * 1024 * 1024));

  ASSERT_EQ(OB_SUCCESS, malloc_allocator->set_tenant_ctx_idle(tenant_id, ctx_id,
                                                              OB_MALLOC_BIG_BLOCK_SIZE));
}

TEST(TestMallocAllocator, ob_malloc_align)
{
  void *ptr = ob_malloc_align(1, 4, "test");
  ASSERT_TRUE(ptr != NULL);
  ASSERT_EQ(0, (int64_t)ptr % 16);

  ptr = ob_malloc_align(4096, 4, "test");
  ASSERT_TRUE(ptr != NULL);
  ASSERT_EQ(0, (int64_t)ptr % 4096);
}

int main(int argc, char *argv[])
{
  signal(49, SIG_IGN);
  OB_LOGGER.set_file_name("t.log", true, true);
  OB_LOGGER.set_log_level("INFO");

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
