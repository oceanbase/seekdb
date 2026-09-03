/*
 * Copyright (c) 2026 OceanBase.
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

#include "storage/throttle/ob_share_resource_throttle_tool.h"

namespace oceanbase
{
namespace share
{
namespace
{

constexpr int64_t TEST_THROTTLE_DURATION = 2LL * 60LL * 60LL * 1000LL * 1000LL;

template <int Tag>
class AvailableResourceTestAllocator
{
public:
  explicit AvailableResourceTestAllocator(const int64_t hold = 0) : hold_(hold) {}

  static const lib::ObLabel throttle_unit_name()
  {
    static lib::ObLabel label(Tag == 0 ? "TestShare" :
                              Tag == 1 ? "TestVector" : "TestOther");
    return label;
  }

  static int64_t resource_unit_size() { return 1; }

  static void init_throttle_config(int64_t &resource_limit,
                                   int64_t &trigger_percentage,
                                   int64_t &max_duration)
  {
    resource_limit = 100;
    trigger_percentage = 50;
    max_duration = TEST_THROTTLE_DURATION;
  }

  static void adaptive_update_limit(const int64_t holding_size,
                                    const int64_t config_specify_resource_limit,
                                    int64_t &resource_limit,
                                    int64_t &last_update_limit_ts,
                                    bool &is_updated)
  {
    UNUSEDx(holding_size, config_specify_resource_limit, resource_limit,
            last_update_limit_ts);
    is_updated = false;
  }

  int64_t hold() { return hold_; }
  void set_hold(const int64_t hold) { hold_ = hold; }

private:
  int64_t hold_;
};

using TestShareAllocator = AvailableResourceTestAllocator<0>;
using TestVectorAllocator = AvailableResourceTestAllocator<1>;
using TestOtherAllocator = AvailableResourceTestAllocator<2>;
using TestThrottleTool = ObShareResourceThrottleTool<TestShareAllocator,
                                                     TestVectorAllocator,
                                                     TestOtherAllocator>;

TEST(TestShareResourceAvailable, uses_smaller_module_and_share_remaining)
{
  TestVectorAllocator vector_allocator(30);
  TestOtherAllocator other_allocator(20);
  TestThrottleTool throttle_tool;
  ASSERT_EQ(OB_SUCCESS, throttle_tool.init(&vector_allocator, &other_allocator));
  throttle_tool.set_resource_limit<TestShareAllocator>(100);
  throttle_tool.set_resource_limit<TestVectorAllocator>(80);

  int64_t module_remaining = 0;
  int64_t share_remaining = 0;
  bool limit_exceeded = false;
  EXPECT_EQ(50, throttle_tool.get_available_resource<TestVectorAllocator>(
                    module_remaining, share_remaining, &limit_exceeded));
  EXPECT_EQ(50, module_remaining);
  EXPECT_EQ(50, share_remaining);
  EXPECT_FALSE(limit_exceeded);

  other_allocator.set_hold(60);
  EXPECT_EQ(10, throttle_tool.get_available_resource<TestVectorAllocator>(
                    module_remaining, share_remaining, &limit_exceeded));
  EXPECT_EQ(50, module_remaining);
  EXPECT_EQ(10, share_remaining);
  EXPECT_FALSE(limit_exceeded);
}

TEST(TestShareResourceAvailable, clamps_exhausted_resources_and_reports_overshoot)
{
  TestVectorAllocator vector_allocator(80);
  TestOtherAllocator other_allocator(20);
  TestThrottleTool throttle_tool;
  ASSERT_EQ(OB_SUCCESS, throttle_tool.init(&vector_allocator, &other_allocator));
  throttle_tool.set_resource_limit<TestShareAllocator>(100);
  throttle_tool.set_resource_limit<TestVectorAllocator>(80);

  int64_t module_remaining = -1;
  int64_t share_remaining = -1;
  bool limit_exceeded = true;
  EXPECT_EQ(0, throttle_tool.get_available_resource<TestVectorAllocator>(
                   module_remaining, share_remaining, &limit_exceeded));
  EXPECT_EQ(0, module_remaining);
  EXPECT_EQ(0, share_remaining);
  EXPECT_FALSE(limit_exceeded);

  vector_allocator.set_hold(81);
  EXPECT_EQ(0, throttle_tool.get_available_resource<TestVectorAllocator>(
                   module_remaining, share_remaining, &limit_exceeded));
  EXPECT_EQ(0, module_remaining);
  EXPECT_EQ(0, share_remaining);
  EXPECT_TRUE(limit_exceeded);

  vector_allocator.set_hold(30);
  other_allocator.set_hold(71);
  EXPECT_EQ(0, throttle_tool.get_available_resource<TestVectorAllocator>(
                   module_remaining, share_remaining, &limit_exceeded));
  EXPECT_EQ(50, module_remaining);
  EXPECT_EQ(0, share_remaining);
  EXPECT_TRUE(limit_exceeded);
}

TEST(TestShareResourceAvailable, config_update_changes_available_resource)
{
  TestVectorAllocator vector_allocator(30);
  TestOtherAllocator other_allocator(20);
  TestThrottleTool throttle_tool;
  ASSERT_EQ(OB_SUCCESS, throttle_tool.init(&vector_allocator, &other_allocator));

  bool config_changed = false;
  throttle_tool.update_throttle_config<TestVectorAllocator>(
      70, 50, TEST_THROTTLE_DURATION, config_changed);
  EXPECT_TRUE(config_changed);

  int64_t module_remaining = 0;
  int64_t share_remaining = 0;
  bool limit_exceeded = false;
  EXPECT_EQ(40, throttle_tool.get_available_resource<TestVectorAllocator>(
                    module_remaining, share_remaining, &limit_exceeded));
  EXPECT_EQ(40, module_remaining);
  EXPECT_EQ(50, share_remaining);
  EXPECT_FALSE(limit_exceeded);

  config_changed = false;
  throttle_tool.update_throttle_config<TestShareAllocator>(
      40, 50, TEST_THROTTLE_DURATION, config_changed);
  EXPECT_TRUE(config_changed);
  EXPECT_EQ(0, throttle_tool.get_available_resource<TestVectorAllocator>(
                   module_remaining, share_remaining, &limit_exceeded));
  EXPECT_EQ(40, module_remaining);
  EXPECT_EQ(0, share_remaining);
  EXPECT_TRUE(limit_exceeded);
}

} // namespace
} // namespace share
} // namespace oceanbase
