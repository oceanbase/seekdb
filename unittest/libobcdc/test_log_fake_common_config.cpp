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
#include "ob_log_fake_common_config.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace libobcdc
{
class TestLogFakeCommonConfig : public ::testing::Test
{
public:
  TestLogFakeCommonConfig() {}
  ~TestLogFakeCommonConfig() {}

  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestLogFakeCommonConfig, common_test)
{
  ObLogFakeCommonConfig fake_config;
  EXPECT_EQ(OB_OBLOG, fake_config.get_server_type());
}

}
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
