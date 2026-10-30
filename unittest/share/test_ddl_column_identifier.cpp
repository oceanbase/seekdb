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
#include "lib/string/ob_sql_string.h"
#include "share/ob_ddl_common.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

TEST(TestDDLColumnIdentifier, OrdinaryColumn)
{
  ObSqlString sql;
  const ObColumnNameInfo column(ObString::make_string("select"), false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, true, false, sql));
  EXPECT_STREQ("`select` AS `select`", sql.ptr());
}

TEST(TestDDLColumnIdentifier, EmbeddedAndConsecutiveBackticks)
{
  ObSqlString sql;
  const ObColumnNameInfo column(ObString::make_string("`a``b`"), false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, true, false, sql));
  EXPECT_STREQ("```a````b``` AS ```a````b```", sql.ptr());
}

TEST(TestDDLColumnIdentifier, CastAndShadowAlias)
{
  ObSqlString sql;
  const ObColumnNameInfo column(ObString::make_string("e`num"), true, true);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, true, true, sql));
  EXPECT_STREQ(", cast(`e``num` as unsigned) AS `__SHADOW_e``num`", sql.ptr());
}

TEST(TestDDLColumnIdentifier, ProjectionFlags)
{
  const ObColumnNameInfo column(ObString::make_string("a`b"), true, true);
  for (int origin = 0; origin < 2; ++origin) {
    for (int alias = 0; alias < 2; ++alias) {
      for (int comma = 0; comma < 2; ++comma) {
        ObSqlString sql;
        ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
        ASSERT_EQ(OB_SUCCESS,
                  ObDDLUtil::generate_column_name_str(column, origin, alias, comma, sql));
        std::string expected = "prefix";
        if (comma) { expected += ", "; }
        if (origin) { expected += "cast(`a``b` as unsigned)"; }
        if (origin && alias) { expected += " AS "; }
        if (alias) { expected += "`__SHADOW_a``b`"; }
        EXPECT_EQ(expected, std::string(sql.ptr(), sql.length()));
      }
    }
  }
}

TEST(TestDDLColumnIdentifier, BoundedUtf8Name)
{
  // No trailing NUL; the identifier is a length-delimited byte string.
  char name[] = {char(0xe5), char(0x88), char(0x97), '`', 'x'};
  ObSqlString sql;
  const ObColumnNameInfo column(ObString(sizeof(name), name), false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, false, false, sql));
  EXPECT_EQ(std::string("`\xe5\x88\x97``x`"), std::string(sql.ptr(), sql.length()));
}

TEST(TestDDLColumnIdentifier, HeapHiddenColumnDoesNotLeaveComma)
{
  ObArray<ObColumnNameInfo> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(
      ObColumnNameInfo(ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("a`b"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("c"), false)));
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, false, true, sql));
  EXPECT_STREQ("`a``b`, `c`", sql.ptr());
}

TEST(TestDDLColumnIdentifier, EscapingAppendsAndPreservesOtherBytes)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString()));
  EXPECT_STREQ("prefix", sql.ptr());
  const char name[] = {'a', '\0', '`', '\\', '\''};
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(sizeof(name), name)));
  const char expected[] = {'p', 'r', 'e', 'f', 'i', 'x', 'a', '\0', '`', '`', '\\', '\''};
  EXPECT_EQ(std::string(expected, sizeof(expected)), std::string(sql.ptr(), sql.length()));
}

TEST(TestDDLColumnIdentifier, LongEscapedName)
{
  std::string name(1024, '`');
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(name.size(), name.data())));
  EXPECT_EQ(std::string(2048, '`'), std::string(sql.ptr(), sql.length()));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
