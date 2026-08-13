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
#include "common/ob_version_def.h"
#include <gtest/gtest.h>

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestVersionDef, is_valid)
{
  ASSERT_EQ(OB_SUCCESS, VersionUtil::is_valid("1.0.0.0"));
}

TEST(TestVersionDef, get_version)
{
  uint64_t version = 0;
  ASSERT_EQ(OB_SUCCESS, VersionUtil::get_version("1.0.0.0", version));
  ASSERT_EQ(cal_version(1, 0, 0, 0), version);
}

TEST(TestVersionDef, print_version_str)
{
  char version_str[OB_SERVER_VERSION_LENGTH] = {0};
  ASSERT_NE(OB_INVALID_INDEX,
            VersionUtil::print_version_str(version_str,
                                           OB_SERVER_VERSION_LENGTH,
                                           cal_version(1, 0, 0, 0)));
  ASSERT_STREQ("1.0.0.0", version_str);
}

} // end share
} // end oceanbase
