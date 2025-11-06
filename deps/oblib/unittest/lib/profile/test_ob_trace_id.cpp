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
#include "deps/oblib/src/lib/thread/thread.h"

using namespace oceanbase::common;
class TestTraceID: public ::testing::Test
{
public:
  TestTraceID() {}
  virtual ~TestTraceID(){}
  virtual void SetUp() {}
  virtual void TearDown() {}
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestTraceID);
protected:
  // function members
protected:
  // data members
};

TEST_F(TestTraceID, basic_test)
{
  ObAddr fake_addr;
  fake_addr.parse_from_cstring("127.0.0.1:1000");

  ASSERT_EQ(32, sizeof(ObCurTraceId::TraceId));
  ObCurTraceId::init(fake_addr);
  ASSERT_FALSE(ObCurTraceId::is_user_request());
  ObCurTraceId::mark_user_request();
  ASSERT_TRUE(ObCurTraceId::is_user_request());
}

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
