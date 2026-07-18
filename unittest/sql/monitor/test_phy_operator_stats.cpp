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
#include "sql/engine/ob_physical_plan.h"
using namespace std;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
class TestPhyOperatorStats : public ::testing::Test
{
public:
  TestPhyOperatorStats() {}
  ~TestPhyOperatorStats() {}
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestPhyOperatorStats, init)
{
  ObArenaAllocator alloc;
  int64_t op_count = 5;
  ObPhyOperatorStats stat;
  EXPECT_EQ(OB_SUCCESS, stat.init(&alloc, op_count));
  EXPECT_EQ(stat.count(), 5);
  EXPECT_EQ(stat.array_size_, 5 * (StatId::MAX_STAT * ObPhyOperatorStats::COPY_COUNT));
}
}
}
int main(int argc, char *argv[])
{
  OB_LOGGER.set_log_level("INFO");
  OB_LOGGER.set_file_name("test_phy_operator.log", true);
  testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
