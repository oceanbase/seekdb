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

#include <gtest/gtest.h>
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

TEST(SqlStringIdentifier, DoublesBackticksWithoutChangingOtherBytes)
{
  const char *names[] = {"plain", "a`b", "`a``b`", "列`名", "", "a\\b\"c"};
  const char *expected[] = {"plain", "a``b", "``a````b``", "列``名", "", "a\\b\"c"};
  for (int i = 0; i < 6; ++i) {
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.append("`__SHADOW_"));
    ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString::make_string(names[i])));
    ASSERT_EQ(OB_SUCCESS, sql.append("`"));
    ObSqlString wanted;
    ASSERT_EQ(OB_SUCCESS, wanted.append_fmt("`__SHADOW_%s`", expected[i]));
    EXPECT_STREQ(wanted.ptr(), sql.ptr());
  }
}

TEST(SqlStringIdentifier, RespectsStringLength)
{
  const char name[] = {'a', '`', 'b', 'x'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(3, name)));
  EXPECT_STREQ("a``b", sql.ptr());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
