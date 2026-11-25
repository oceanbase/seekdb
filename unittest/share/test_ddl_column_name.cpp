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
#include <string>
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestDDLColumnName, doubles_every_backtick)
{
  const char *names[] = {"plain", "a`b", "`head", "tail`", "``", "中`文", "a` FROM t; --"};
  const char *quoted[] = {"`plain`", "`a``b`", "```head`", "`tail```", "``````", "`中``文`", "`a`` FROM t; --`"};
  for (int64_t i = 0; i < ARRAYSIZEOF(names); ++i) {
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
        ObColumnNameInfo(ObString::make_string(names[i]), false), true, false, false, sql));
    EXPECT_STREQ(quoted[i], sql.ptr());
  }
}

TEST(TestDDLColumnName, preserves_cast_alias_shadow_and_comma_flags)
{
  for (bool origin : {false, true}) {
    for (bool alias : {false, true}) {
      for (bool shadow : {false, true}) {
        for (bool cast : {false, true}) {
          for (bool comma : {false, true}) {
            ObSqlString sql;
            ASSERT_EQ(OB_SUCCESS, sql.append("SELECT "));
            const ObColumnNameInfo name(ObString::make_string("a`b"), shadow, cast);
            ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(name, origin, alias, comma, sql));
            std::string expected = "SELECT ";
            if (comma) { expected += ", "; }
            if (origin) { expected += cast ? "cast(`a``b` as unsigned)" : "`a``b`"; }
            if (origin && alias) { expected += " AS "; }
            if (alias) { expected += shadow ? "`__SHADOW_a``b`" : "`a``b`"; }
            EXPECT_STREQ(expected.c_str(), sql.ptr());
          }
        }
      }
    }
  }
}

TEST(TestDDLColumnName, respects_string_length)
{
  const char buffer[] = {'x', '`', 'y', 'z'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      ObColumnNameInfo(ObString(3, buffer), false), true, true, false, sql));
  EXPECT_STREQ("`x``y` AS `x``y`", sql.ptr());
}

TEST(TestDDLColumnName, handles_empty_and_maximum_length_names)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      ObColumnNameInfo(ObString(), false), true, false, false, sql));
  EXPECT_STREQ("``", sql.ptr());
  sql.reset();
  std::string name(OB_MAX_COLUMN_NAME_LENGTH, '`');
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      ObColumnNameInfo(ObString(name.size(), name.data()), false), true, false, false, sql));
  EXPECT_EQ(name.size() * 2 + 2, sql.length());
  EXPECT_EQ(std::string(name.size() * 2 + 2, '`'), std::string(sql.ptr(), sql.length()));
}

TEST(TestDDLColumnName, column_list_skips_heap_key_without_leading_comma)
{
  ObArray<ObColumnNameInfo> names;
  ASSERT_EQ(OB_SUCCESS, names.push_back(ObColumnNameInfo(
      ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false)));
  ASSERT_EQ(OB_SUCCESS, names.push_back(ObColumnNameInfo(ObString::make_string("a`b"), true)));
  ASSERT_EQ(OB_SUCCESS, names.push_back(ObColumnNameInfo(ObString::make_string("c"), false, true)));
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(names, true, true, true, sql));
  EXPECT_STREQ("`a``b` AS `__SHADOW_a``b`, cast(`c` as unsigned) AS `c`", sql.ptr());
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
