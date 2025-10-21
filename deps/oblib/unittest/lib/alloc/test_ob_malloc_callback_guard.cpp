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

#include "lib/alloc/ob_malloc_callback.h"
#include "lib/allocator/ob_malloc.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;

class TestObMallocCallbackGuard : public ::testing::Test
{
};

class MallocCallback final : public ObMallocCallback
{
public:
  MallocCallback(int64_t& hold) : hold_(hold) {}
  virtual void operator()(const ObMemAttr& attr, int64_t used) override
  {
    UNUSED(attr);
    hold_ += used;
    std::cout << hold_ << " " << used << std::endl;
  }
private:
  int64_t& hold_;
};

TEST_F(TestObMallocCallbackGuard, DISABLED_AllocAndFree)
{
  int64_t hold = 0;
  MallocCallback cb(hold);
  ObMallocCallbackGuard guard(cb);
  auto *ptr = ob_malloc(2113, ObNewModIds::TEST);
  std::cout << "alloc" << std::endl;
  ASSERT_EQ(hold, 2113);
  ob_free(ptr);
  std::cout << "free" << std::endl << std::endl;
  ASSERT_EQ(hold, 0);
  {
    int64_t hold2 = 0;
    MallocCallback cb(hold2);
    ObMallocCallbackGuard guard(cb);
    auto *ptr = ob_malloc(2113, ObNewModIds::TEST);
    ASSERT_EQ(hold, 2113);
    ASSERT_EQ(hold2, 2113);
    std::cout << "alloc" << std::endl;
    ob_free(ptr);
    ASSERT_EQ(hold, 0);
    ASSERT_EQ(hold2, 0);
    std::cout << "free" << std::endl << std::endl;
  }
  ptr = ob_malloc(2113, ObNewModIds::TEST);
  ASSERT_EQ(hold, 2113);
  std::cout << "alloc" << std::endl;
  ob_free(ptr);
  std::cout << "free" << std::endl;
  ASSERT_EQ(hold, 0);
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
