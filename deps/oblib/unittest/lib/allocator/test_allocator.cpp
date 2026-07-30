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
#include "lib/allocator/ob_allocator_v2.h"
#undef private

using namespace std;
using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;

const uint64_t ctx_id = 2;
const int64_t limit = 1 << 30;
const lib::ObLabel &label = "1";

static bool has_unfree = false;
void has_unfree_callback(char *)
{
  has_unfree = true;
}

class TestAllocator : public ::testing::Test
{
public:
  void SetUp() override
  {
    ObMallocAllocator *ma = ObMallocAllocator::get_instance();
    ASSERT_EQ(OB_SUCCESS, ma->set_allocator_limit(limit));
    auto ta = ma->get_ctx_allocator(ctx_id);
    ASSERT_TRUE(NULL != ta);
  }
};

// ObAllocator has no state and no logic, only basic functions are tested here.
TEST_F(TestAllocator, basic)
{
  ObMemAttr attr(label, ctx_id);
  ObAllocator allocator(nullptr, attr);
  int64_t size = 1L << 4;

  void *ptrs[128] = {};
  int64_t count = 1L << 18;
  while (count--) {
    int i = 0;
    for (int j = 0; j < 16; ++j) {
      ptrs[i++] = allocator.alloc(size);
    }
    ASSERT_GT(allocator.used(), 0);
    while (i--) {
      allocator.free(ptrs[i]);
    }
    size = ((size | reinterpret_cast<size_t>(ptrs[0])) & ((1 << 13) - 1));
  }

  // Test alloc_align/free_align.
  for (int i = 0; i < 10; ++i) {
    const int64_t align = 8 << i;
    void *ptr = allocator.alloc_align(100, align);
    ASSERT_EQ(0, reinterpret_cast<int64_t>(ptr) & (align - 1));
    ASSERT_GT(allocator.used(), 0);
    allocator.free_align(ptr);
    ASSERT_EQ(allocator.used(), 0);
  }
}

TEST_F(TestAllocator, reveal_unfree)
{
  ObMemAttr attr(label, ctx_id);
  has_unfree = false;

  // No unfreed allocation.
  {
    ObAllocator allocator(nullptr, attr);
    const int64_t hold = allocator.used();
    void *ptr = allocator.alloc(100);
    ASSERT_NE(ptr, nullptr);
    ASSERT_GT(allocator.used(), hold);
    allocator.free(ptr);
    allocator.~ObAllocator();
    ASSERT_FALSE(has_unfree);
    ASSERT_EQ(allocator.used(), hold);
  }

  // One unfreed allocation.
  {
    ObAllocator allocator(nullptr, attr);
    const int64_t hold = allocator.used();
    void *ptr = allocator.alloc(100);
    ASSERT_NE(ptr, nullptr);
    ASSERT_GT(allocator.used(), hold);
    allocator.~ObAllocator();
    ASSERT_TRUE(has_unfree);
    ASSERT_EQ(allocator.used(), hold);
  }
}

TEST_F(TestAllocator, reset)
{
  ObMemAttr attr(label, ctx_id);
  const int64_t hold = 0;
  ObAllocator allocator(nullptr, attr);
  void *ptr = allocator.alloc(100);
  ASSERT_NE(ptr, nullptr);
  ASSERT_GT(allocator.used(), hold);
  // reset
  allocator.reset();
  ASSERT_EQ(allocator.used(), hold);
  // alloc after reset
  ptr = allocator.alloc(100);
  ASSERT_NE(ptr, nullptr);
  ASSERT_GT(allocator.used(), hold);
  allocator.~ObAllocator();
  ASSERT_EQ(allocator.used(), hold);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
