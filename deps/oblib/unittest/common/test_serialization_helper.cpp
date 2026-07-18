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
#include "lib/container/ob_array_serialization.h"

using namespace oceanbase;
using namespace common;

#define OK(value) ASSERT_EQ(OB_SUCCESS, (value))

class ObSerialzationHelper: public ::testing::Test
{
  public:
    ObSerialzationHelper();
    virtual ~ObSerialzationHelper();
    virtual void SetUp();
    virtual void TearDown();
  private:
    // disallow copy
    ObSerialzationHelper(const ObSerialzationHelper &other);
    ObSerialzationHelper& operator=(const ObSerialzationHelper &other);
  protected:
    // data members
};

ObSerialzationHelper::ObSerialzationHelper()
{
}

ObSerialzationHelper::~ObSerialzationHelper()
{
}

void ObSerialzationHelper::SetUp()
{
}

void ObSerialzationHelper::TearDown()
{
}

enum TestEnum1
{
  TestEnum1Value = 1024,
};

enum TestEnum2
{
  TestEnum2Value = 1L << 33,
};

TEST_F(ObSerialzationHelper, enum_type)
{
  char buf[1024];
  ObSArray<TestEnum1> array;

  ASSERT_TRUE(4 == sizeof(TestEnum1Value));

  ASSERT_EQ(OB_SUCCESS, array.push_back(TestEnum1Value));
  int64_t pos = 0;
  ASSERT_LT(0, array.get_serialize_size());
  ASSERT_EQ(OB_SUCCESS, array.serialize(buf, 1024, pos));
  pos = 0;
  array.deserialize(buf, 1024, pos);
  ASSERT_EQ(1, array.count());
  ASSERT_EQ(TestEnum1Value, array.at(0));

  ASSERT_TRUE(8 == sizeof(TestEnum2Value));
  ObSArray<TestEnum2> array2;
  pos = 0;
  // only 4 byte enum supported right now, the following code will fail to compile.
  // array2.get_serialize_size();
  // array2.serialize(buf, 1024, pos);
  // array2.deserialize(buf, 1024, pos);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
