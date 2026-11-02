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
#include "lib/oblog/ob_syslog_rate_limiter.h"

//using namespace ::oblib;
using namespace oceanbase::lib;
using namespace oceanbase::common;

TEST(TestSimpleRateLimiter, Basic)
{
  ObSimpleRateLimiter rl(3);
  ASSERT_EQ(OB_SUCCESS, rl.try_acquire());
  ASSERT_EQ(OB_SUCCESS, rl.try_acquire());
  ASSERT_EQ(OB_SUCCESS, rl.try_acquire());
  ASSERT_NE(OB_SUCCESS, rl.try_acquire());
  ASSERT_NE(OB_SUCCESS, rl.try_acquire());

  sleep(1);
  ASSERT_EQ(OB_SUCCESS, rl.try_acquire());
  ASSERT_EQ(OB_SUCCESS, rl.try_acquire());
  ASSERT_EQ(OB_SUCCESS, rl.try_acquire());
  ASSERT_NE(OB_SUCCESS, rl.try_acquire());
  ASSERT_NE(OB_SUCCESS, rl.try_acquire());

  sleep(1);
  rl.set_rate(1);
  ASSERT_EQ(OB_SUCCESS, rl.try_acquire());
  ASSERT_NE(OB_SUCCESS, rl.try_acquire());

  {
    ObSimpleRateLimiter rl(99);
    for (int i = 0; i < 25; i++) {
      ASSERT_EQ(OB_SUCCESS, rl.try_acquire(4)) << i;
    }
    ASSERT_NE(OB_SUCCESS, rl.try_acquire(4));
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
