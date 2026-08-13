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
#include "storage/ob_col_map.h"
using namespace oceanbase::common;
using namespace oceanbase::common::hash;

TEST(TestObPlacementHashMap, single_bucket)
{
  ObPlacementHashMap<int64_t, int64_t, 1> hashmap;
  int64_t v;
  ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(1, 1));
  ASSERT_EQ(OB_HASH_EXIST, hashmap.set_refactored(1, 1));
  ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(1, 1, 1));
  ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(1, 1, 1, 1));
  ASSERT_EQ(OB_SUCCESS, hashmap.get_refactored(1, v));
  ASSERT_EQ(1, v);
  ASSERT_EQ(1, *hashmap.get(1));
}

TEST(TestObPlacementHashMap, many_buckets)
{
  const uint64_t N = 10345;
  int64_t v = 0;
  ObPlacementHashMap<int64_t, int64_t, N> hashmap;
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_HASH_NOT_EXIST, hashmap.get_refactored(i, v));
    ASSERT_EQ(NULL, hashmap.get(i));
  }
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(i, i * i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashmap.set_refactored(N, N * N));
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_SUCCESS, hashmap.get_refactored(i, v));
    ASSERT_EQ(static_cast<int64_t>(i * i) , v);
    ASSERT_EQ(static_cast<int64_t>(i * i), *hashmap.get(i));
  }
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(i, i * i, 1));
    ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(i, i * i, 1, 1));
  }
  ASSERT_EQ(OB_HASH_FULL, hashmap.set_refactored(N, N * N));
  for (uint64_t i = 0; i < N; i++)
  {
    ASSERT_EQ(OB_SUCCESS, hashmap.get_refactored(i, v));
    ASSERT_EQ(static_cast<int64_t>(i * i) , v);
    ASSERT_EQ(static_cast<int64_t>(i * i), *hashmap.get(i));
  }
}

TEST(TestObPlacementHashMap, many_buckets2)
{
  const uint64_t N = 10345;
  int64_t v = 0;
  ObPlacementHashMap<int64_t, int64_t, N> hashmap;
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_HASH_NOT_EXIST, hashmap.get_refactored(i, v));
    ASSERT_EQ(NULL, hashmap.get(i));
  }
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(i, i * i));
  }
  ASSERT_EQ(OB_HASH_FULL, hashmap.set_refactored(0, 0));
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_SUCCESS, hashmap.get_refactored(i, v));
    ASSERT_EQ(static_cast<int64_t>(i * i) , v);
    ASSERT_EQ(static_cast<int64_t>(i * i), *hashmap.get(i));
  }
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(i, i * i, 1));
    ASSERT_EQ(OB_SUCCESS, hashmap.set_refactored(i, i * i, 1, 1));
  }
  ASSERT_EQ(OB_HASH_FULL, hashmap.set_refactored(0, 0));
  for (uint64_t i = N; i > 0; i--)
  {
    ASSERT_EQ(OB_SUCCESS, hashmap.get_refactored(i, v));
    ASSERT_EQ(static_cast<int64_t>(i * i) , v);
    ASSERT_EQ(static_cast<int64_t>(i * i), *hashmap.get(i));
  }
}
