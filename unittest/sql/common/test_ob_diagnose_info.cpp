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
#include "deps/oblib/src/lib/ob_errno.h"

using namespace oceanbase;
using namespace common;

#define OK(value) ASSERT_EQ(OB_SUCCESS, (value))

class ObDiagnoseInfoTest: public ::testing::Test
{
  public:
    ObDiagnoseInfoTest();
    virtual ~ObDiagnoseInfoTest();
    virtual void SetUp();
    virtual void TearDown();
  private:
    // disallow copy
    ObDiagnoseInfoTest(const ObDiagnoseInfoTest &other);
    ObDiagnoseInfoTest& operator=(const ObDiagnoseInfoTest &other);
  protected:
    // data members
};

ObDiagnoseInfoTest::ObDiagnoseInfoTest()
{
}

ObDiagnoseInfoTest::~ObDiagnoseInfoTest()
{
}

void ObDiagnoseInfoTest::SetUp()
{
}

void ObDiagnoseInfoTest::TearDown()
{
}

TEST_F(ObDiagnoseInfoTest, basic_test)
{
  // Not supported now

 // EVENT_INC(RPC_PACKET_IN);
 // ASSERT_EQ(1, EVENT_GET(RPC_PACKET_IN));
 // EVENT_SET(RPC_PACKET_IN, 2);
 // ASSERT_EQ(2, EVENT_GET(RPC_PACKET_IN));
 // EVENT_ADD(RPC_PACKET_IN, 3);
 // ASSERT_EQ(5, EVENT_GET(RPC_PACKET_IN));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
