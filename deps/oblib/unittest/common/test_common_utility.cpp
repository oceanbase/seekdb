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
#include "common/ob_common_utility.h"
using namespace  oceanbase::common;
TEST(TestBasicTimeGuard, tl_time_guard)
{
  EXPECT_TRUE(NULL == ObBasicTimeGuard::get_tl_time_guard());
  BASIC_TIME_GUARD(tg, "ObLog");
  EXPECT_TRUE(&tg == ObBasicTimeGuard::get_tl_time_guard());
  {
    BASIC_TIME_GUARD(tg1, "ObMalloc");
    EXPECT_TRUE(&tg1 == ObBasicTimeGuard::get_tl_time_guard());
  }
  EXPECT_TRUE(&tg == ObBasicTimeGuard::get_tl_time_guard());
}

TEST(TestBasicTimeGuard, click_infos)
{
  BASIC_TIME_GUARD(tg, "ObMalloc");
  int index = 8;
  for (int i = 0; i < 16; ++i) {
    usleep(5);
    BASIC_TIME_GUARD_CLICK("alloc_chunk");
  }
  tg.click_infos_[index].cost_time_ = 1;
  EXPECT_EQ(index, tg.click_infos_[index].seq_);
  usleep(5);
  BASIC_TIME_GUARD_CLICK("target");
  EXPECT_EQ(16, tg.click_infos_[index].seq_);
}
int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
