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

#include "storage/memtable/mvcc/ob_mvcc_row.h"

#include <gtest/gtest.h>

namespace oceanbase
{
namespace unittest
{
using namespace oceanbase::common;
using namespace oceanbase::memtable;

TEST(TestObMemtableValue, smoke_test)
{
  ObMvccRow mr;

  ObCStringHelper helper;
  fprintf(stdout, "mr=[%s]\n", helper.convert(mr));
  fprintf(stdout, "mr=[%s]\n", helper.convert(mr, true));
}

}
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_file_name("test_mvcc_row.log", true);
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
