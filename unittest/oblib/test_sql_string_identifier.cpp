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

#include <string>
#include <gtest/gtest.h>
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

TEST(SqlStringIdentifier, QuotesAndEscapes)
{
  const char *names[] = {"plain", "a`b", "`leading", "trailing`", "``", "", "列`名", "a\\b"};
  const char *expected[] = {"`plain`", "`a``b`", "```leading`", "`trailing```", "``````", "``", "`列``名`", "`a\\b`"};
  const int64_t count = sizeof(names) / sizeof(names[0]);
  for (int64_t i = 0; i < count; ++i) {
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.append_quoted_identifier(ObString(names[i])));
    EXPECT_STREQ(expected[i], sql.ptr()) << names[i];
  }
}

TEST(SqlStringIdentifier, PrefixAndExistingSql)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("cast("));
  ASSERT_EQ(OB_SUCCESS, sql.append_quoted_identifier(ObString("a`b")));
  ASSERT_EQ(OB_SUCCESS, sql.append(" as unsigned) AS "));
  ASSERT_EQ(OB_SUCCESS, sql.append_quoted_identifier(ObString("a`b"), ObString("__SHADOW_")));
  EXPECT_STREQ("cast(`a``b` as unsigned) AS `__SHADOW_a``b`", sql.ptr());
  sql.reuse();
  ASSERT_EQ(OB_SUCCESS, sql.append_quoted_identifier(ObString("name"), ObString("pre`")));
  EXPECT_STREQ("`pre``name`", sql.ptr());
}

TEST(SqlStringIdentifier, LengthDelimitedAndLongNames)
{
  const char name[] = {'a', '`', 'b', 'X'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_quoted_identifier(ObString(3, name)));
  EXPECT_STREQ("`a``b`", sql.ptr());
  sql.reuse();
  const std::string long_name(1024, '`');
  ASSERT_EQ(OB_SUCCESS, sql.append_quoted_identifier(ObString(long_name.size(), long_name.data())));
  EXPECT_EQ(std::string(2050, '`'), std::string(sql.ptr(), sql.length()));
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
