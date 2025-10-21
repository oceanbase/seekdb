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
#include <sys/time.h>
#include "lib/time/ob_tsc_timestamp.h"

namespace oceanbase
{
namespace common
{

inline int64_t current_time()
{
  struct timeval t;
  if (gettimeofday(&t, NULL) < 0) {
  }
  return (static_cast<int64_t>(t.tv_sec) * static_cast<int64_t>(1000000) + static_cast<int64_t>(t.tv_usec));
}

TEST(TestObTscTimestamp, common)
{
  int ret = OB_SUCCESS;
  int64_t time1 = OB_TSC_TIMESTAMP.current_time();
  ASSERT_EQ(OB_SUCCESS, ret);
  int64_t time2 = current_time();
  ASSERT_EQ(time1 / 100000, time2 / 100000);
  usleep(3000000);//3s
  time1 = OB_TSC_TIMESTAMP.current_time();
  ASSERT_EQ(OB_SUCCESS, ret);
  time2 = current_time();
  ASSERT_EQ(time1 / 100000, time2 / 100000);
}

}//common
}//oceanbase

int main(int argc, char **argv)
{
  //oceanbase::common::ObLogger::get_logger().set_log_level("WARN");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
