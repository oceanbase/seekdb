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
#include "sql/engine/dml/ob_table_insert_up.h"
#include "sql/engine/dml/ob_table_update.h"
#include "sql/engine/dml/ob_table_replace.h"
using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{
class TestInsertUp : public ::testing::Test
{
public:
  TestInsertUp() {}
  virtual ~TestInsertUp() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestInsertUp, test_reset)
{
  ObArenaAllocator alloc;
  ObTableInsertUp insert_up(alloc);
  const int64_t COUNT=5;
  EXPECT_EQ(OB_SUCCESS, insert_up.init_scan_column_id_array(COUNT));
  EXPECT_EQ(OB_SUCCESS, insert_up.init_update_related_column_array(COUNT));
  EXPECT_EQ(OB_SUCCESS, insert_up.init_column_ids_count(COUNT));
  EXPECT_EQ(OB_SUCCESS, insert_up.init_column_infos_count(COUNT));
  insert_up.reset();
  insert_up.reuse();
  ObTableUpdate update(alloc);
  EXPECT_EQ(OB_SUCCESS, update.init_updated_column_count(alloc, COUNT));
  EXPECT_EQ(OB_SUCCESS, update.init_column_ids_count(COUNT));
  EXPECT_EQ(OB_SUCCESS, update.init_column_infos_count(COUNT));
  update.reset();
  update.reuse();
  ObTableReplace replace(alloc);
  EXPECT_EQ(OB_SUCCESS, replace.init_column_ids_count(COUNT));
  EXPECT_EQ(OB_SUCCESS, replace.init_column_infos_count(COUNT));
  replace.reset();
  replace.reuse();
}
} //namespace sql
} //namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
