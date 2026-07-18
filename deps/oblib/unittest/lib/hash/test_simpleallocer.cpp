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

#include "lib/hash/ob_hashtable.h"
#include "gtest/gtest.h"

using namespace oceanbase;
using namespace common;
using namespace hash;

std::set<void *> ptr_set;
class MyAllocator : public ObIAllocator
{
public:
  void *alloc(const int64_t size)
  {
    void *ptr = ob_malloc(size, "test");
    ptr_set.insert(ptr);
    return ptr;
  }
  void *alloc(const int64_t , const ObMemAttr &)
    { return NULL; }
  void free(void *ptr)
  {
    ptr_set.erase(ptr);
  }
};

TEST(TestSimpleAllocer, allocate)
{
  SimpleAllocer<int, 10, SpinMutexDefendMode, MyAllocator> alloc;
  const int N = 100;
  int *ptrs[N];
  int i = N;
  while (i--) {
    int *ptr = alloc.alloc();
    EXPECT_TRUE(ptr != NULL);
    ptrs[i] = ptr;
  }
  i= N;
  EXPECT_TRUE(!ptr_set.empty());
  while (i--) {
    alloc.free(ptrs[i]);
  }
  EXPECT_TRUE(ptr_set.empty());
}

int main(int argc, char **argv)
{
  ObLogger::get_logger().set_log_level("ERROR");
  testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
