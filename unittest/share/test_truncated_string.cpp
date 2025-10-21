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

#define USING_LOG_PREFIX LIB

#include <gtest/gtest.h>

#include "share/ob_define.h"
#include "share/ob_truncated_string.h"

namespace oceanbase
{
namespace common
{
TEST(TestTruncatedString, common)
{
  // construct functions
  ObString str("123456789");
  ObTruncatedString printer_less(str, 5);
  ObTruncatedString printer_more(str, 10);
  ObTruncatedString printer_empty(str, 0);
  ObTruncatedString printer_error(str, -1);
  ObCStringHelper helper;
  ASSERT_EQ(0, strcmp(helper.convert(printer_less), "12345"));
  ASSERT_EQ(0, strcmp(helper.convert(printer_more), "123456789"));
  ASSERT_EQ(0, strcmp(helper.convert(printer_empty), ""));
  ASSERT_EQ(0, strcmp(helper.convert(printer_error), ""));
}

}//end namespace common
}//end namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
