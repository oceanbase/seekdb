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
#include "lib/allocator/ob_tc_malloc.h"
#include "lib/time/ob_time_utility.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_part_mgr_util.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{
static const int64_t BUF_SIZE = 1024*10;
TEST(ObSchemaStructTest, hash_map)
{
  ObArenaAllocator allocator;
  ColumnHashMap map(allocator);
  int32_t value = 0;
  int ret = map.get(1, value);
  ASSERT_EQ(OB_NOT_INIT, ret);
  ret = map.set(1, 20);
  ASSERT_EQ(OB_NOT_INIT, ret);

  int64_t bucket_num = 100;
  ret = map.init(bucket_num);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = map.init(bucket_num);
  ASSERT_EQ(OB_INIT_TWICE, ret);

  ret = map.get(1, value);
  ASSERT_EQ(OB_HASH_NOT_EXIST, ret);
  ret = map.set(1, 20);
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = map.set(1, 30);
  ASSERT_EQ(OB_HASH_EXIST, ret);
  ret = map.get(1, value);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(20, value);

  for (int64_t i = 1; i < 1000; ++i) {
    ret = map.set(i+1, 88);
    ASSERT_EQ(OB_SUCCESS, ret);
    ret = map.get(i+1, value);
    ASSERT_EQ(OB_SUCCESS, ret);
    ASSERT_EQ(88, value);
  }
}

}//end of namespace schema
}//end of namespace share
}//end of namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("DEBUG");
//  OB_LOGGER.set_file_name("test_schema.log", true);
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
