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
#include "lib/wait_event/ob_wait_event.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace common
{
TEST(ObWaitEventDesc, normal)
{
  ObWaitEventDesc des1;
  ObWaitEventDesc des2;

  des1.reset();
  des1.wait_begin_time_ = 1;
  des2.reset();
  des2.wait_begin_time_ = 2;
  ASSERT_TRUE(des1 < des2);
  ASSERT_TRUE(des2 > des1);

  des1.add(des2);
  ASSERT_TRUE(des1 == des2);

  COMMON_LOG(INFO, "ObWaitEventDesc, ", K(des1));
}

TEST(ObWaitEventStat, normal)
{
  ObWaitEventStat stat1;
  ObWaitEventStat stat2;
  ObWaitEventStat stat3;

  stat1.reset();
  stat1.total_waits_ = 1;
  stat1.time_waited_ = 10;
  stat1.max_wait_ = 10;
  stat1.total_timeouts_ = 0;

  stat2.reset();
  stat2.total_waits_ = 2;
  stat2.time_waited_ = 10;
  stat2.max_wait_ = 6;
  stat2.total_timeouts_ = 0;

  stat3 = stat1;
  stat3.add(stat2);
  ASSERT_EQ(stat3.total_waits_, 3);
  ASSERT_EQ(stat3.time_waited_, 20);
  ASSERT_EQ(stat3.max_wait_, 10);
  ASSERT_EQ(stat3.total_timeouts_, 0);
}

}
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


