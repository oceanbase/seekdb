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
#include "lib/oblog/ob_log.h"
#include <gtest/gtest.h>
#include <regex>

namespace oceanbase
{
namespace unittest
{
using namespace common;

TEST(TestLogMeta, test_log_meta)
{
  const char *str1 = "log/tenant_1001/1001/log";
  const char *str2 = "log/tenant_10011/1001/log";
  const char *str3 = "log/tenant_0111/1001/log";
  std::regex e(".*/tenant_[1][0-9]{0,3}/[1][0-9]{0,3}/log");
  bool is_matched = false;
  int ret = OB_SUCCESS;
  EXPECT_EQ(true, std::regex_match(str1, e));
  EXPECT_EQ(false, std::regex_match(str2, e));
  EXPECT_EQ(false, std::regex_match(str3, e));
}

}
}

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_dir_match.log", true);
  OB_LOGGER.set_log_level("INFO");
  PALF_LOG(INFO, "begin unittest::test_log_dir_match");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
