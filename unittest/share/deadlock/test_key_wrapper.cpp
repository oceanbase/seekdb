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

#include "src/share/deadlock/ob_deadlock_detector_common_define.h"
#include "share/deadlock/test/test_key.h"

#include <gmock/gmock.h>

namespace oceanbase {
namespace unittest {

using namespace common;
using namespace share::detector;
using namespace std;


class TestUserBinaryKey : public ::testing::Test {
public:
  TestUserBinaryKey() {}
  ~TestUserBinaryKey() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
};
// Serialization and deserialization functionality
TEST_F(TestUserBinaryKey, serialization) {
  UserBinaryKey key1;
  key1.set_user_key(ObDeadLockTestIntKey(1));
  const int64_t length = 1024;
  int64_t pos = 0;
  char* buffer = new char[key1.get_serialize_size()];
  ASSERT_EQ(OB_SUCCESS, key1.serialize(buffer, length, pos));
  UserBinaryKey key2;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, key2.deserialize(buffer, length, pos));
  ASSERT_EQ(true, key1 == key2);
}

}// namespace unittest
}// namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_key_wrapper.log");
  oceanbase::common::ObLogger &logger = oceanbase::common::ObLogger::get_logger();
  logger.set_file_name("test_key_wrapper.log", false);
  logger.set_log_level(OB_LOG_LEVEL_DEBUG);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
