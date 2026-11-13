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
#include <string>
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestDDLColumnName, escapes_backticks_in_origins_and_aliases)
{
  struct Case { const char *name; const char *quoted; };
  const Case cases[] = {
      {"plain", "`plain`"},
      {"a`b", "`a``b`"},
      {"`a", "```a`"},
      {"a`", "`a```"},
      {"a``b", "`a````b`"},
      {"`", "````"},
      {"", "``"},
      {"列`名", "`列``名`"},
  };
  for (const auto &c : cases) {
    SCOPED_TRACE(c.name);
    ObColumnNameInfo column(ObString::make_string(c.name), false);
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, false, false, sql));
    EXPECT_STREQ(c.quoted, sql.ptr());
    sql.reset();
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, false, true, false, sql));
    EXPECT_STREQ(c.quoted, sql.ptr());
    sql.reset();
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, true, false, sql));
    const std::string expected = std::string(c.quoted) + " AS " + c.quoted;
    EXPECT_STREQ(expected.c_str(), sql.ptr());
  }
}

TEST(TestDDLColumnName, preserves_cast_shadow_alias_and_comma)
{
  ObColumnNameInfo column(ObString::make_string("e`num"), true, true);
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("`id`"));
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, true, true, sql));
  EXPECT_STREQ("`id`, cast(`e``num` as unsigned) AS `__SHADOW_e``num`", sql.ptr());
  sql.reset();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, false, true, false, sql));
  EXPECT_STREQ("`__SHADOW_e``num`", sql.ptr());
}

TEST(TestDDLColumnName, respects_explicit_string_length)
{
  const char name[] = {'a', '`', 'b', 'X'};
  ObColumnNameInfo column(ObString(3, name), false);
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, true, false, sql));
  EXPECT_STREQ("`a``b` AS `a``b`", sql.ptr());
}

TEST(TestDDLColumnName, formats_column_list)
{
  ObArray<ObColumnNameInfo> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("id"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("a`b"), false)));
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, true, false, sql));
  EXPECT_STREQ("`id` AS `id`, `a``b` AS `a``b`", sql.ptr());
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
