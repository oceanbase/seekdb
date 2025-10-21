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
#define private public  // get private member
#define protected public  // get protected member
#include "observer/table/group/ob_table_tenant_group.h"

using namespace oceanbase::common;
using namespace oceanbase::table;

class TestTableGroupCommit: public ::testing::Test
{
public:
  static const int64_t DEFAULT_MAX_GROUP_SIZE;
  static const int64_t DEFAULT_MIN_GROUP_SIZE;
  TestTableGroupCommit() {};
  virtual ~TestTableGroupCommit() {}
public:
  ObTableGroupCommitMgr mgr_;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestTableGroupCommit);
};

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  OB_LOGGER.set_file_name("test_table_group.log", true);
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
