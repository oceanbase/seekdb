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

#define USING_LOG_PREFIX SHARE
#include "share/ob_version_parser.h"
#include <gtest/gtest.h>


namespace oceanbase
{
namespace share
{
using namespace common;
using namespace oceanbase::lib;
class TestVersionParser: public ::testing::Test
{
public:
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestVersionParser, is_valid)
{
  ASSERT_EQ(ObVersionParser::is_valid("1.0.0.0"), OB_SUCCESS);
}

TEST_F(TestVersionParser, get_version)
{
  uint64_t version = 0;
  uint64_t res_version = 0;
  version = cal_version(1, 0, 0, 0);
  ASSERT_EQ(ObVersionParser::get_version("1.0.0.0", res_version), OB_SUCCESS);
  ASSERT_EQ(version, res_version);
}

TEST_F(TestVersionParser, print_version_str)
{
  char version_str[OB_VERSION_LENGTH] = {0};
  uint64_t version = 0;
  int64_t pos = 0;

  version = cal_version(1, 0, 0, 0);
  ASSERT_NE(OB_INVALID_INDEX, ObVersionParser::print_version_str(version_str, OB_VERSION_LENGTH, version));
  ASSERT_EQ(0, STRNCMP(version_str, "1.0.0.0", OB_VERSION_LENGTH));
}

} // end share
} // end oceanbase
