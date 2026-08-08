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

#include "lib/container/ob_2d_array.h"
#include "lib/thread/ob_test_util.h"
#include <gtest/gtest.h>
using namespace oceanbase::common;

class Ob2DArrayTest: public ::testing::Test
{
  public:
    Ob2DArrayTest();
    virtual ~Ob2DArrayTest();
    virtual void SetUp();
    virtual void TearDown();
  private:
    // disallow copy
    Ob2DArrayTest(const Ob2DArrayTest &other);
    Ob2DArrayTest& operator=(const Ob2DArrayTest &other);
  protected:
    // data members
};

Ob2DArrayTest::Ob2DArrayTest()
{
}

Ob2DArrayTest::~Ob2DArrayTest()
{
}

void Ob2DArrayTest::SetUp()
{
}

void Ob2DArrayTest::TearDown()
{
}

TEST_F(Ob2DArrayTest, basic_test)
{
  const int64_t block_size = sizeof(int) * 1024;
  Ob2DArray<int, block_size> arr;
  Ob2DArray<int, block_size> arr2;
  _OB_LOG(INFO, "sizeof(2darray)=%ld", sizeof(arr));
  const int N = 1024*32+1;
  for (int round = 0; round < 10; ++round)
  {
    ASSERT_EQ(0, arr.count());
    for (int i = 0; i < N; ++i)
    {
      ASSERT_EQ(OB_SUCCESS, arr.push_back(i));
    }
    ASSERT_EQ(N, arr.count());
    for (int i = 0; i < N; ++i)
    {
      ASSERT_EQ(i, arr.at(i));
    }
    ASSERT_EQ(1024*33, arr.get_capacity());
    // test copy
    ASSERT_EQ(OB_SUCCESS, arr2.assign(arr));
    for (int i = 0; i < N; ++i)
    {
      ASSERT_EQ(i, arr2.at(i));
    }

    ASSERT_EQ(OB_SUCCESS, arr.reserve(2 * (N-1)));
    ASSERT_EQ(1024*32*2, arr.get_capacity());
    //ASSERT_EQ(1024*32*2, arr.count());

    arr.reset();

    ASSERT_EQ(OB_SUCCESS, arr.reserve((N-1)));
    ASSERT_EQ(1024*32, arr.get_capacity());
    //ASSERT_EQ(1024*32, arr.count());

  }
  _OB_LOG(INFO, "done");
}

// TEST_F(Ob2DArrayTest, 2DSEArray_test)
// {
//   typedef ObSEArray<int32_t, 4,
//                     ObWrapperAllocator,
//                     false,
//                     ObArrayDefaultCallBack<int32_t>,
//                     NotImplementItemEncode<int32_t>,
//                     Ob2DArray<int32_t,
//                               ObWrapperAllocator,
//                               false,
//                               ObSEArray<char*, 16> > > Se2DArray;
//   const int64_t block_size = sizeof(int) * 1024;
//   ObArenaAllocator block_allocator(1);
//   Se2DArray arr(block_size, ObWrapperAllocator(&block_allocator));
//   Se2DArray arr2(block_size, ObWrapperAllocator(&block_allocator));
//   _OB_LOG(INFO, "sizeof(2darray)=%ld", sizeof(arr));
//   const int N = 16*1024*32+1;
//   for (int round = 0; round < 10; ++round)
//   {
//     ASSERT_EQ(0, arr.count());
//     for (int i = 0; i < N; ++i)
//     {
//       ASSERT_EQ(OB_SUCCESS, arr.push_back(i));
//     }
//     ASSERT_EQ(N, arr.count());
//     for (int i = 0; i < N; ++i)
//     {
//       ASSERT_EQ(i, arr.at(i));
//     }
//     //ASSERT_EQ(1024*33, arr.get_capacity());
//     // test copy
//     ASSERT_EQ(OB_SUCCESS, arr2.assign(arr));
//     arr.reset();
//     for (int i = 0; i < N; ++i)
//     {
//       ASSERT_EQ(i, arr2.at(i));
//     }
//     // test copy constructor
//     ObSEArray<int, 16> arr3;
//     ASSERT_EQ(OB_SUCCESS, arr3.assign(arr2));
//     for (int i = 0; i < N; ++i)
//     {
//       ASSERT_EQ(i, arr3.at(i));
//     }
//     // pop back
//     int j = 0;
//     for (int i = 0; i < N; ++i)
//     {
//       ASSERT_EQ(OB_SUCCESS, arr3.pop_back(j));
//       ASSERT_EQ(N-i-1, j);
//     }
//   }
//   _OB_LOG(INFO, "done2");
// }

TEST_F(Ob2DArrayTest, remove)
{
  Ob2DArray<int64_t> arr;
  OK(arr.push_back(1));
  OK(arr.push_back(2));
  OK(arr.push_back(3));
  ASSERT_EQ(3, arr.count());
  ASSERT_EQ(OB_ARRAY_OUT_OF_RANGE, arr.remove(4));
  OK(arr.remove(1));
  ASSERT_EQ(2, arr.count());
  ASSERT_EQ(1, arr.at(0));
  ASSERT_EQ(3, arr.at(1));
}

#if 0
TEST_F(Ob2DArrayTest, swap)
{
  const int64_t block_size = sizeof(int) * 1024;
  Ob2DArray<int, block_size> arr1;
  Ob2DArray<int, block_size> arr2;
  const int len1 = 1024*32+1;
  const int len2 = 1024+1;
  for (int round = 0; round < 10; ++round)
  {
    ASSERT_EQ(0, arr1.count());
    ASSERT_EQ(0, arr2.count());

    for (int i = 0; i < len1; ++i)
    {
      ASSERT_EQ(OB_SUCCESS, arr1.push_back(i));
    }
    ASSERT_EQ(len1, arr1.count());

    for (int i = 0; i < len2; ++i)
    {
      ASSERT_EQ(OB_SUCCESS, arr2.push_back(-i));
    }
    ASSERT_EQ(len2, arr2.count());

    for (int i = 0; i < len1; ++i)
    {
      ASSERT_EQ(i, arr1.at(i));
    }
    for (int i = 0; i < len2; ++i)
    {
      ASSERT_EQ(-i, arr2.at(i));
    }

    OK(arr1.swap(arr2));

    ASSERT_EQ(len1, arr2.count());
    ASSERT_EQ(len2, arr1.count());

    for (int i = 0; i < len1; ++i)
    {
      ASSERT_EQ(i, arr2.at(i));
    }
    for (int i = 0; i < len2; ++i)
    {
      ASSERT_EQ(-i, arr1.at(i));
    }

    arr1.reset();
    arr2.reset();
  }
  _OB_LOG(INFO, "done");
}
#endif

// performance_test parameters
using obj_type = int*;
const int array_size = 100000;
const int access_index_num = 100000 * 500;
const int access_run = 1;
const int ptr_array_capacity = 1000;
const int block_size = OB_MALLOC_BIG_BLOCK_SIZE;
using Tested2DArray = Ob2DArray<obj_type, block_size,
                                ModulePageAllocator,
                                false,
                                ObSEArray<obj_type *, ptr_array_capacity,
                                          ModulePageAllocator, false>>;

using namespace std;

#if 0
#endif
