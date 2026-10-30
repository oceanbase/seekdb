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

#include <cstring>
#include <string>
#include <gtest/gtest.h>
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

TEST(TestSqlStringIdentifier, EscapesBackticksOnly)
{
  const struct {
    const char *input;
    const char *expected;
  } cases[] = {
    {"column_name", "column_name"},
    {"select", "select"},
    {"a`b", "a``b"},
    {"`a", "``a"},
    {"a`", "a``"},
    {"`", "``"},
    {"a``b", "a````b"},
    {"`a`b`", "``a``b``"},
    {"a'b\"c\\d", "a'b\"c\\d"},
    {"\xe5\x88\x97`\xe5\x90\x8d", "\xe5\x88\x97``\xe5\x90\x8d"},
  };
  for (const auto &test_case : cases) {
    SCOPED_TRACE(test_case.input);
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(test_case.input)));
    EXPECT_STREQ(test_case.expected, sql.ptr());
    EXPECT_EQ(strlen(test_case.expected), sql.length());
  }
}

TEST(TestSqlStringIdentifier, UsesExplicitLength)
{
  const char name[] = {'a', '`', 'b', '`', 'x'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(3, name)));
  EXPECT_STREQ("a``b", sql.ptr());
}

TEST(TestSqlStringIdentifier, PreservesExistingContentsAndLeavesQuotingToCaller)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("SELECT `"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString("key`id")));
  ASSERT_EQ(OB_SUCCESS, sql.append("` AS `__SHADOW_"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString("key`id")));
  ASSERT_EQ(OB_SUCCESS, sql.append("`"));
  EXPECT_STREQ("SELECT `key``id` AS `__SHADOW_key``id`", sql.ptr());
}

TEST(TestSqlStringIdentifier, EmptyInputDoesNotChangeOutput)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString()));
  EXPECT_EQ(0, sql.length());
  ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString("")));
  EXPECT_STREQ("prefix", sql.ptr());
}

TEST(TestSqlStringIdentifier, GrowsOutputForRepeatedBackticks)
{
  const std::string name(1024, '`');
  const std::string expected(2048, '`');
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(name.size(), name.data())));
  EXPECT_EQ(expected.size(), sql.length());
  EXPECT_STREQ(expected.c_str(), sql.ptr());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
