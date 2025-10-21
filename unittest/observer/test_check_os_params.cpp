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

#define USING_LOG_PREFIX RS
#include <gmock/gmock.h>
#define private public
#include "src/share/config/ob_config.h"
namespace oceanbase
{
using namespace common;
using namespace share;
namespace observer
{
class TestEndpointIngressService : public testing::Test
{
public:
  TestEndpointIngressService()
  {}
  virtual ~TestEndpointIngressService()
  {}
  virtual void SetUp(){};
  virtual void TearDown()
  {}
  virtual void TestBody()
  {}
};

TEST_F(TestEndpointIngressService, ingress_service)
{
  int ret = OB_SUCCESS;
  // bool strict_check = false;
  // strict_check = GCONF.strict_check_os_params;
  // LOG_WARN("", K(strict_check), K(GCONF.strict_check_os_params));
  // ret = check_os_params(strict_check);
  // CheckAllParams::check_all_params(strict_check);
  // int64_t value = 0;
  // const char* str1 = "/proc/sys/vm/max_map_count";
  // obj.read_one_int(str1 , value);
  // EXPECT_EQ(value, 65536);
  // bool res1 = obj.is_path_valid(str1);
  // EXPECT_EQ(res1, true);
  // bool res2 = obj.is_path_valid("/proc/1/sys/vm/max_map_count");
  // EXPECT_EQ(res2, false);
  // obj.check_all_params(false);
}
}  // namespace rootserver
}  // namespace oceanbase
int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#undef private
