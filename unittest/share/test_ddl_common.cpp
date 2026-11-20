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

#include "share/ob_ddl_common.h"
#include <gtest/gtest.h>
#include <string>

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestDDLCommon, quoted_column_names)
{
  struct Case
  {
    const char *name;
    const char *quoted;
  };
  const Case cases[] = {
    {"plain", "`plain`"},
    {"select", "`select`"},
    {"a`b", "`a``b`"},
    {"`a``b`", "```a````b```"},
    {"`", "````"},
    {"a\\b", "`a\\b`"},
    {"列`名", "`列``名`"},
  };
  for (const auto &item : cases) {
    for (const bool shadow : {false, true}) {
      for (const bool cast : {false, true}) {
        for (const bool origin : {false, true}) {
          for (const bool alias : {false, true}) {
            for (const bool comma : {false, true}) {
              ObSqlString sql;
              ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
              ObColumnNameInfo column(ObString::make_string(item.name), shadow, cast);
              ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
                  column, origin, alias, comma, sql));
              std::string expected = comma ? "prefix, " : "prefix";
              if (origin) {
                expected += cast ? std::string("cast(") + item.quoted + " as unsigned)"
                                 : item.quoted;
              }
              if (origin && alias) {
                expected += " AS ";
              }
              if (alias) {
                expected += shadow ? std::string("`__SHADOW_") + (item.quoted + 1)
                                   : item.quoted;
              }
              EXPECT_EQ(expected, std::string(sql.ptr(), sql.length()))
                  << item.name << " shadow=" << shadow << " cast=" << cast
                  << " origin=" << origin << " alias=" << alias << " comma=" << comma;
            }
          }
        }
      }
    }
  }
}

TEST(TestDDLCommon, column_name_uses_explicit_length)
{
  const char name[] = {'a', '`', 'b', 'x'};
  ObSqlString sql;
  ObColumnNameInfo column(ObString(3, name), false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, true, false, sql));
  EXPECT_STREQ("`a``b` AS `a``b`", sql.ptr());
}

TEST(TestDDLCommon, column_list_skips_hidden_heap_key)
{
  ObArray<ObColumnNameInfo> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(
      ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("a`b"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("c`d"), true, true)));
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, true, true, sql));
  EXPECT_STREQ("`a``b` AS `a``b`, cast(`c``d` as unsigned) AS `__SHADOW_c``d`", sql.ptr());
  sql.reset();
  columns.reset();
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObDDLUtil::generate_column_name_str(columns, true, true, true, sql));
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
