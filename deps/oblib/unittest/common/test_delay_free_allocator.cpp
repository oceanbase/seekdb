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
#include "lib/allocator/ob_delay_free_allocator.h"

namespace oceanbase
{
namespace common
{
TEST(ObDelayFreeAllocator, test_delay_free_allocator)
{
  int ret = OB_SUCCESS;
  ObDelayFreeAllocator allocator;
  const int64_t local_array_size = 100000;
  void *data = NULL;
  void *array[local_array_size];
  int64_t i = 0;
  int64_t size = 1000;

  // test invalid init
  ret = allocator.init("DelayFreeAlloc", false, -1);
  EXPECT_NE(OB_SUCCESS, ret);

  // test invalid alloc and free
  data = allocator.alloc(size);
  EXPECT_EQ(NULL, data);
  allocator.free(data);

  // test init
  ret = allocator.init("DelayFreeAlloc", false, 0);
  EXPECT_EQ(OB_SUCCESS, ret);

  // test repeatly init
  ret = allocator.init("DelayFreeAlloc", false, 0);
  EXPECT_NE(OB_SUCCESS, ret);

  // test destroy
  allocator.destroy();

  // test init
  ret = allocator.init("DelayFreeAlloc", true, 0);
  EXPECT_EQ(OB_SUCCESS, ret);

  // test normal alloc
  data = allocator.alloc(size);
  EXPECT_TRUE(data != NULL);

  // test free
  allocator.free(data);

  // test invalid free
  allocator.free(NULL);

  // test alloc and free
  for (i = 0; i < local_array_size; i++) {
    if (i >= 10000) {
      allocator.free(array[i - 10000]);
      array[i - 10000] = NULL;
    }
    array[i] = allocator.alloc(size);
    EXPECT_TRUE(array[i] != NULL);
  }

  for (i = local_array_size - 1; i >= 90000; i--) {
    allocator.free(array[i]);
    array[i] = 0;
  }

  for (i = 0; i < local_array_size; i++) {
    array[i] = allocator.alloc(size);
    EXPECT_TRUE(array[i] != NULL);
  }

  for (i = 0; i < local_array_size; i++) {
    allocator.free(array[i]);
    array[i] = NULL;
  }

  // test alloc large size
  array[0] = allocator.alloc(1024 * 1024 * 4);
  EXPECT_TRUE(array[0] != NULL);

  // test reset
  allocator.reset();
  EXPECT_EQ(0, allocator.get_total_size());
  EXPECT_EQ(0, allocator.get_memory_fragment_size());

}

}
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
