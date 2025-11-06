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
#include "lib/utility/ob_test_util.h"
#include "lib/container/ob_2d_array.h"
using namespace oceanbase::common;

class TestIArray: public ::testing::Test
{
public:
  TestIArray();
  virtual ~TestIArray();
  virtual void SetUp();
  virtual void TearDown();
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestIArray);
protected:
  // function members
  template <typename T>
  void setup(int32_t N, T &arr);
  void verify(int32_t N, const ObIArray<int32_t> &arr);
protected:
  // data members
};

TestIArray::TestIArray()
{
}

TestIArray::~TestIArray()
{
}

void TestIArray::SetUp()
{
}

void TestIArray::TearDown()
{
}

template <typename T>
void TestIArray::setup(int32_t N, T &arr)
{
  arr.reset();
  for (int32_t i = 0;i < N; ++i) {
    OK(arr.push_back(i));
  } // end for
}

void TestIArray::verify(int32_t N, const ObIArray<int32_t> &arr)
{
  ASSERT_EQ(N, arr.count());
  for (int32_t i = 0; i < N; ++i) {
    ASSERT_EQ(i, arr.at(i));
  }
}

TEST_F(TestIArray, array_assign)
{
  ObArray<int32_t> se;

  ObArray<int32_t> arr1;
  ObSEArray<int32_t, 8> arr2;

  int32_t sizes[2] = {8, 4096};

  for (int32_t i = 0; i < 2; ++i) {
    int32_t N = sizes[i];
    setup(N, arr1);
    setup(N, arr2);
    OK(se.assign(arr1));
    OK(se.assign(arr2));
  }
}

TEST_F(TestIArray, se_array_assign)
{
  ObSEArray<int32_t, 8> se;

  ObArray<int32_t> arr1;
  ObSEArray<int32_t, 4> arr2;
  ObSEArray<int32_t, 8> arr4;

  int32_t sizes[3] = {4, 8, 4096};

  for (int32_t i = 0; i < 3; ++i) {
    int32_t N = sizes[i];
    setup(N, arr1);
    setup(N, arr2);
    setup(N, arr4);
    OK(se.assign(arr1));
    OK(se.assign(arr2));
    OK(se.assign(arr4));
  }
}

TEST_F(TestIArray, 2d_array_assign)
{
  Ob2DArray<int32_t> se;

  ObArray<int32_t> arr1;
  ObSEArray<int32_t, 8> arr2;
  Ob2DArray<int32_t> arr3;

  int32_t sizes[2] = {8, 4096};

  for (int32_t i = 0; i < 2; ++i) {
    int32_t N = sizes[i];
    setup(N, arr1);
    setup(N, arr2);
    setup(N, arr3);
    OK(se.assign(arr1));
    OK(se.assign(arr2));
    OK(se.assign(arr3));
  }
}
TEST_F(TestIArray, array_append)
{
  ObArray<int32_t> arr1;
  ObSEArray<int32_t, 8> arr2;
  OK(arr1.push_back(0));
  OK(arr2.push_back(1));
  OK(append(arr1, arr2));
  ASSERT_EQ(2, arr1.count());
  for (int32_t i = 0; i < 2; ++i) {
    ASSERT_EQ(i, arr1.at(i));
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
