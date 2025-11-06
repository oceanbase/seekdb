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

#include "gtest/gtest.h"
#include "lib/hash/ob_placement_hashset.h"

using namespace oceanbase;
using namespace common;
using namespace hash;

TEST(TestObPlacementHashSet, single_bucket)
{
  ObPlacementHashSet<int64_t, 1> hashset;
  ASSERT_EQ(OB_SUCCESS, hashset.set_refactored(1));
  ASSERT_EQ(OB_HASH_EXIST, hashset.set_refactored(1));
  ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(1));
}

TEST(TestObPlacementHashSet, many_buckets)
{
  const uint64_t N = 10345;
  ObPlacementHashSet<int64_t, N> hashset;
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_NOT_EXIST, hashset.exist_refactored(i));
  }
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_SUCCESS, hashset.set_refactored(i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashset.set_refactored(N));
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(i));
  }
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.set_refactored(i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashset.set_refactored(N));
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(i));
  }

  hashset.clear();
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_NOT_EXIST, hashset.exist_refactored(i));
  }
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_SUCCESS, hashset.set_refactored(i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashset.set_refactored(N));
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(i));
  }
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.set_refactored(i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashset.set_refactored(N));
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(i));
  }
}

TEST(TestObPlacementHashSet, many_buckets2)
{
  const uint64_t N = 10345;
  ObPlacementHashSet<int64_t, N> hashset;
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_NOT_EXIST, hashset.exist_refactored(i));
  }
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_SUCCESS, hashset.set_refactored(i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashset.set_refactored(0));
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(i));
  }
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.set_refactored(i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashset.set_refactored(0));
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(i));
  }

  hashset.clear();
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_NOT_EXIST, hashset.exist_refactored(i));
  }
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_SUCCESS, hashset.set_refactored(i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashset.set_refactored(0));
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(i));
  }
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.set_refactored(i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashset.set_refactored(0));
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_EXIST, hashset.exist_refactored(i));
  }
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
