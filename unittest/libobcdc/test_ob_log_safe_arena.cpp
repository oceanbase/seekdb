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
#include "ob_log_safe_arena.h"
#include <thread>

namespace oceanbase
{
namespace libobcdc
{

bool check_memset(const void *ptr, const char c, const int64_t size) {
  bool bret = true;
  const char *cptr = static_cast<const char*>(ptr);
  for (int i = 0; bret && i < size; i++) {
    if (cptr[i] != c) {
      bret = false;
    }
  }
  return bret;
}

TEST(ObLogSafeArena, test_ob_log_safe_arena)
{
  ObCdcSafeArena safe_arena;
  const int64_t THREAD_NUM = 32;
  std::vector<std::thread> threads;
  auto alloc_test_func = [&](int idx) {
    constexpr int64_t ALLOC_CNT = 1024;
    constexpr int64_t ALLOC_SIZE = 1024;
    ObRandom rand;
    char c = static_cast<char>(idx & 0xFF);
    for (int i = 0; i < ALLOC_CNT; i++) {
      int64_t alloc_size = rand.get(1, ALLOC_SIZE);
      void *ptr = safe_arena.alloc(alloc_size);
      EXPECT_TRUE(NULL != ptr);
      MEMSET(ptr, c, alloc_size);
      EXPECT_TRUE(check_memset(ptr, c, alloc_size));
    }
  };
  for (int i = 0; i < THREAD_NUM; i++) {
    threads.emplace_back(std::thread(alloc_test_func, i));
  }
  for (int i = 0; i < THREAD_NUM; i++) {
    threads[i].join();
  }
  safe_arena.clear();
}

} // namespace libobcdc
} // ns oceanbase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("DEBUG");
  OB_LOGGER.set_log_level("DEBUG");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
