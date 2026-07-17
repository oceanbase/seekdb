/*
 * Copyright (c) 2026 OceanBase.
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

#include "lib/allocator/page_arena.h"
#include "storage/fts/dict/ob_ft_dat_dict.h"

#include <gtest/gtest.h>
#include <new>

namespace oceanbase
{
namespace storage
{

TEST(Task4FtsPerf, CompactTokenMapFindsInsertedAndMissingTokens)
{
  common::ObArenaAllocator allocator("Task4FtsPerf");
  void *memory = allocator.alloc(ObArrayHashMap::calc_memory_size(3));
  ASSERT_NE(nullptr, memory);
  ObArrayHashMap *map = new (memory) ObArrayHashMap();
  ASSERT_EQ(OB_SUCCESS, map->init(3));
  ASSERT_EQ(OB_SUCCESS, map->insert(ObString("a"), 7));
  ASSERT_EQ(OB_SUCCESS, map->insert(ObString("中"), 11));
  ASSERT_EQ(OB_SUCCESS, map->insert(ObString("z"), 13));

  ObFTTokenCode code = -1;
  ASSERT_EQ(OB_SUCCESS, map->find(ObString("a"), code));
  ASSERT_EQ(7, code);
  ASSERT_EQ(OB_SUCCESS, map->find(ObString("中"), code));
  ASSERT_EQ(11, code);
  ASSERT_EQ(OB_SUCCESS, map->find(ObString("z"), code));
  ASSERT_EQ(13, code);
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, map->find(ObString("x"), code));
}

} // namespace storage
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
