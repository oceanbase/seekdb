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

#include "share/deadlock/ob_deadlock_detector_common_define.h"

#include <gmock/gmock.h>

namespace oceanbase {
namespace unittest {

using namespace common;
using namespace share::detector;
using namespace std;


class TestDeadLockUtility : public ::testing::Test {
public:
  TestDeadLockUtility() {}
  ~TestDeadLockUtility() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
};
// Serialization and deserialization functionality
TEST_F(TestDeadLockUtility, interface) {
  ObDetectorUserReportInfo info;
  ObSharedGuard<char> ptr;
  ptr.assign((char*)("a"), [](char*){});
  ASSERT_EQ(OB_SUCCESS, info.set_module_name(ptr));
  ptr.assign((char*)("b"), [](char*){});
  ASSERT_EQ(OB_SUCCESS, info.set_visitor(ptr));
  ptr.assign((char*)("c"), [](char*){});
  ASSERT_EQ(OB_SUCCESS, info.set_resource(ptr));
  ASSERT_EQ(OB_SUCCESS, info.set_extra_info("1","2"));// OK
  // ASSERT_EQ(OB_SUCCESS, info.set_extra_info(ObString("1"),"2"));// OK
  // ASSERT_EQ(OB_SUCCESS, info.set_extra_info("1",ObString("2")));// OK
  // ASSERT_EQ(OB_SUCCESS, info.set_extra_info(ObString("1"),ObString("2")));// OK
  // ASSERT_EQ(OB_SUCCESS, info.set_extra_info("1",ObString("2"),ObString("3"),"4"));// OK
  // info.set_extra_info("1",ObString("2"),ObString("3"));// compile error, number of args not even
  // info.set_extra_info(string("1"), "2");// compile error, arg types neither ObString nor char*
  // info.set_extra_info("1","2","3","4","5","6","7","8","9","10","11","12");// compile error, number of args reach column's limit
}

}// namespace unittest
}// namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_deadlock_utility.log");
  oceanbase::common::ObLogger &logger = oceanbase::common::ObLogger::get_logger();
  logger.set_file_name("test_deadlock_utility.log", false);
  logger.set_log_level(OB_LOG_LEVEL_DEBUG);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
