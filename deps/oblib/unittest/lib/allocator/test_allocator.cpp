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
  virtual void SetUp()
  {
    ObMallocAllocator *ma = ObMallocAllocator::get_instance();
    ASSERT_EQ(OB_SUCCESS, ma->set_allocator_limit(limit));
    auto ta = ma->get_ctx_allocator(ctx_id);
    ASSERT_TRUE(NULL != ta);
  }
  //virtual void TearDown();
};

// ObAllocator has no state and no logic, only basic functions are tested here
TEST_F(TestAllocator, basic)
{
  ObMallocAllocator *ma = ObMallocAllocator::get_instance();
  auto ta = ma->get_ctx_allocator(ctx_id);
  ObMemAttr attr(label, ctx_id);
  ObAllocator a(nullptr, attr);
  int64_t sz = 100;

  void *p[128] = {};
  int64_t cnt = 1L << 18;
  sz = 1L << 4;

  while (cnt--) {
    int i = 0;
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    p[i++] = a.alloc(sz);
    int64_t hold = a.used();
    ASSERT_GT(hold, 0);
    while (i--) {
      a.free(p[i]);
    }
    sz = ((sz | reinterpret_cast<size_t>(p[0])) & ((1<<13) - 1));
  }

  // test alloc_align/free_align
  for (int i = 0; i < 10; ++i) {
    int64_t align = 8<<i;
    void *ptr = a.alloc_align(100, align);
    ASSERT_EQ(0, (int64_t)ptr & (align - 1));
    ASSERT_GT(a.used(), 0);
    a.free_align(ptr);
    ASSERT_EQ(a.used(), 0);
  }
  cout << "done" << endl;
}

TEST_F(TestAllocator, reveal_unfree)
{
  ObMallocAllocator *ma = ObMallocAllocator::get_instance();
  auto ta = ma->get_ctx_allocator(ctx_id);
  ObMemAttr attr(label, ctx_id);
  has_unfree = false;
  // no unfree
  {
    ObAllocator a(nullptr, attr);
    const int64_t hold = a.used();
    void *ptr = a.alloc(100);
    ASSERT_NE(ptr, nullptr);
    ASSERT_GT(a.used(), hold);
    a.free(ptr);
    a.~ObAllocator();
    ASSERT_FALSE(has_unfree);
    ASSERT_EQ(a.used(), hold);
  }
  // has unfree
  {
    ObAllocator a(nullptr, attr);
    const int64_t hold = a.used();
    void *ptr = a.alloc(100);
    ASSERT_NE(ptr, nullptr);
    ASSERT_GT(a.used(), hold);
    //a.free(ptr);
    a.~ObAllocator();
    ASSERT_TRUE(has_unfree);
    ASSERT_EQ(a.used(), hold);
  }
}

TEST_F(TestAllocator, reset)
{
  ObMallocAllocator *ma = ObMallocAllocator::get_instance();
  auto ta = ma->get_ctx_allocator(ctx_id);
  ObMemAttr attr(label, ctx_id);
  const int64_t hold = 0;
  ObAllocator a(nullptr, attr);
  void *ptr = a.alloc(100);
  ASSERT_NE(ptr, nullptr);
  ASSERT_GT(a.used(), hold);
  // reset
  a.reset();
  ASSERT_EQ(a.used(), hold);
  // alloc after reset
  ptr = a.alloc(100);
  ASSERT_NE(ptr, nullptr);
  ASSERT_GT(a.used(), hold);
  a.~ObAllocator();
  ASSERT_EQ(a.used(), hold);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
