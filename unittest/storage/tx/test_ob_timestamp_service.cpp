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
#include "storage/tx/ob_timestamp_service.h"

namespace oceanbase
{
using namespace common;
using namespace transaction;
namespace unittest
{

class TestTimestampService : public ObTimestampService
{
public:
  int init_for_test()
  {
    int ret = ObTimestampService::init();
    if (OB_SUCC(ret)) {
      last_id_ = 0;
      limited_id_ = ObTimeUtility::current_time_ns() + TIMESTAMP_PREALLOCATED_RANGE;
    }
    return ret;
  }
};

TEST(TestObTimestampService, get_local_timestamp)
{
  TestTimestampService service;
  int64_t first = 0;
  int64_t second = 0;

  ASSERT_EQ(OB_SUCCESS, service.init_for_test());
  ASSERT_EQ(OB_SUCCESS, service.get_timestamp(first));
  ASSERT_EQ(OB_SUCCESS, service.get_timestamp(second));
  EXPECT_GT(first, 0);
  EXPECT_GT(second, first);
}

} // namespace unittest
} // namespace oceanbase

using namespace oceanbase;
using namespace oceanbase::common;

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
