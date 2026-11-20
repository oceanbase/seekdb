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

#include <string>
#include <gtest/gtest.h>
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

TEST(TestEscapedIdentifier, backticks)
{
  const char *inputs[] = {"plain", "a`b", "`edge`", "``", "a``b", "select", "with space"};
  const char *expected[] = {"plain", "a``b", "``edge``", "````", "a````b", "select", "with space"};
  for (int i = 0; i < 7; ++i) {
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.append("`"));
    ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString::make_string(inputs[i])));
    ASSERT_EQ(OB_SUCCESS, sql.append("`"));
    EXPECT_EQ(std::string("`") + expected[i] + "`", std::string(sql.ptr(), sql.length()));
  }
}

TEST(TestEscapedIdentifier, bounded_input)
{
  char name[] = {'a', '`', 'b', 'x'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(3, name)));
  EXPECT_STREQ("a``b", sql.ptr());
}

TEST(TestEscapedIdentifier, embedded_zero)
{
  char name[] = {'a', '\0', '`', 'b'};
  const char expected[] = {'a', '\0', '`', '`', 'b'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(4, name)));
  EXPECT_EQ(std::string(expected, sizeof(expected)), std::string(sql.ptr(), sql.length()));
}

TEST(TestEscapedIdentifier, empty_input)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString()));
  EXPECT_STREQ("prefix", sql.ptr());
}

TEST(TestEscapedIdentifier, long_and_multibyte_input)
{
  const std::string part = "\xe5\x88\x97`";
  std::string name;
  std::string expected;
  for (int i = 0; i < 1024; ++i) {
    name += part;
    expected += part + "`";
  }
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(
      ObString(static_cast<int32_t>(name.size()), name.data())));
  EXPECT_EQ(expected, std::string(sql.ptr(), sql.length()));
}

TEST(TestEscapedIdentifier, input_aliases_output_across_reallocation)
{
  std::string name(500, '`');
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append(name.c_str()));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(sql.string()));
  EXPECT_EQ(std::string(1500, '`'), std::string(sql.ptr(), sql.length()));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
