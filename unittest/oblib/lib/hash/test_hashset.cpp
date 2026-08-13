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

#include "lib/hash/ob_hashset.h"

using namespace oceanbase;
using namespace common;
using namespace hash;

TEST(TestHashSet, InsertFindAndIterate)
{
  ObHashSet<int64_t> set;
  ObHashSet<int64_t>::iterator iter;
  ASSERT_EQ(OB_SUCCESS, set.create(10));

  for (int64_t i = 0; i < 10; i++)
  {
    ASSERT_EQ(OB_SUCCESS, set.set_refactored(i));
  }

  for (int64_t i = 0; i < 10; i++)
  {
    ASSERT_EQ(OB_HASH_EXIST, set.exist_refactored(i));
  }

  int64_t i = 0;
  for (iter = set.begin(); iter != set.end(); iter++, i++)
  {
    ASSERT_EQ(i, iter->first);
  }
  ASSERT_EQ(10, i);
}
