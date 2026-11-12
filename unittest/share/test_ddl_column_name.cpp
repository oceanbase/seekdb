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
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace share
{
using namespace common;

namespace
{
void expect_column_name(const ObString &name,
                        const bool with_origin,
                        const bool with_alias,
                        const bool is_shadow,
                        const bool needs_cast,
                        const bool with_comma,
                        const char *expected)
{
  SCOPED_TRACE(expected);
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("SELECT "));
  const ObColumnNameInfo column(name, is_shadow, needs_cast);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      column, with_origin, with_alias, with_comma, sql));
  EXPECT_STREQ(expected, sql.ptr());
}
} // namespace

TEST(DdlColumnName, escapes_original_name)
{
  expect_column_name(ObString::make_string("a`b"), true, false, false, false, false,
                     "SELECT `a``b`");
}

TEST(DdlColumnName, escapes_alias_name)
{
  expect_column_name(ObString::make_string("a`b"), false, true, false, false, false,
                     "SELECT `a``b`");
  expect_column_name(ObString::make_string("a`b"), true, true, false, false, false,
                     "SELECT `a``b` AS `a``b`");
}

TEST(DdlColumnName, escapes_cast_and_shadow_alias)
{
  expect_column_name(ObString::make_string("a`b"), true, false, false, true, false,
                     "SELECT cast(`a``b` as unsigned)");
  expect_column_name(ObString::make_string("a`b"), false, true, true, false, false,
                     "SELECT `__SHADOW_a``b`");
  expect_column_name(ObString::make_string("a`b"), true, true, true, true, true,
                     "SELECT , cast(`a``b` as unsigned) AS `__SHADOW_a``b`");
}

TEST(DdlColumnName, escapes_leading_trailing_and_repeated_backticks)
{
  expect_column_name(ObString::make_string("`a"), true, false, false, false, false,
                     "SELECT ```a`");
  expect_column_name(ObString::make_string("a`"), true, false, false, false, false,
                     "SELECT `a```");
  expect_column_name(ObString::make_string("a``b"), true, true, false, false, false,
                     "SELECT `a````b` AS `a````b`");
  expect_column_name(ObString::make_string("``"), true, false, false, false, false,
                     "SELECT ``````");
}

TEST(DdlColumnName, preserves_plain_names_and_formatting)
{
  expect_column_name(ObString::make_string("plain"), true, false, false, false, false,
                     "SELECT `plain`");
  expect_column_name(ObString::make_string("plain"), true, true, false, false, true,
                     "SELECT , `plain` AS `plain`");
  expect_column_name(ObString::make_string("plain"), true, true, true, true, false,
                     "SELECT cast(`plain` as unsigned) AS `__SHADOW_plain`");
  expect_column_name(ObString::make_string("plain"), false, false, true, true, false,
                     "SELECT ");
  expect_column_name(ObString::make_string("plain"), false, false, true, true, true,
                     "SELECT , ");
}

TEST(DdlColumnName, preserves_utf8_and_other_identifier_characters)
{
  expect_column_name(ObString::make_string("列名"), true, true, false, false, false,
                     "SELECT `列名` AS `列名`");
  expect_column_name(ObString::make_string("列`名"), true, true, true, false, false,
                     "SELECT `列``名` AS `__SHADOW_列``名`");
  expect_column_name(ObString::make_string("a'\\%b"), true, false, false, false, false,
                     "SELECT `a'\\%b`");
}

TEST(DdlColumnName, uses_the_explicit_name_length)
{
  char name[] = {'a', '`', 'b', 'x'};
  expect_column_name(ObString(3, name), true, true, false, false, false,
                     "SELECT `a``b` AS `a``b`");
}

TEST(DdlColumnName, preserves_empty_name_formatting)
{
  expect_column_name(ObString(), true, true, false, false, false,
                     "SELECT `` AS ``");
  expect_column_name(ObString(), true, true, true, true, false,
                     "SELECT cast(`` as unsigned) AS `__SHADOW_`");
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
