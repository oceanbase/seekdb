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
#include "logservice/localservice/ob_local_log_handler_set.h"
#undef private

namespace oceanbase
{
using namespace logservice;
namespace unittest
{
class MockRoleChangeHandler : public ObILocalLogHandler
{
public:
  void deactivate() override final
  {}
  int activate() override final
  {
    return OB_SUCCESS;
  }
};
TEST(TestRoleChangeHander, test_basic_func)
{
  ObLocalLogHandlerSet handler;
  ObLogBaseType type = ObLogBaseType::TRANS_SERVICE_LOG_BASE_TYPE;
  MockRoleChangeHandler mock_handler;
  ASSERT_EQ(OB_SUCCESS, handler.register_handler(type, &mock_handler));
  ASSERT_EQ(OB_SUCCESS, handler.activate());
  handler.deactivate();
}
} // end namespace unittest
} // end namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_role_change_handler.log", true);
  OB_LOGGER.set_log_level("INFO");
  PALF_LOG(INFO, "begin unittest::test_role_change_handler");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
