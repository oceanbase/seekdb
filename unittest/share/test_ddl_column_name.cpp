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
#include "lib/container/ob_array.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestDDLColumnName, escape_identifiers)
{
  const char *names[] = {"plain", "a`b", "`leading", "trailing`", "a``b", "`", "列`名"};
  const char *quoted[] = {"`plain`", "`a``b`", "```leading`", "`trailing```", "`a````b`", "````", "`列``名`"};
  for (int64_t i = 0; i < 7; ++i) {
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
        ObColumnNameInfo(ObString::make_string(names[i]), false), true, false, false, sql));
    EXPECT_STREQ(quoted[i], sql.ptr());
  }
}

TEST(TestDDLColumnName, aliases_and_casts)
{
  const ObString name = ObString::make_string("a`b");
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      ObColumnNameInfo(name, false), true, true, false, sql));
  EXPECT_STREQ("`a``b` AS `a``b`", sql.ptr());
  sql.reset();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      ObColumnNameInfo(name, true, true), true, true, false, sql));
  EXPECT_STREQ("cast(`a``b` as unsigned) AS `__SHADOW_a``b`", sql.ptr());
  sql.reset();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      ObColumnNameInfo(name, true, true), false, true, false, sql));
  EXPECT_STREQ("`__SHADOW_a``b`", sql.ptr());
  sql.reset();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      ObColumnNameInfo(name, false, true), true, false, true, sql));
  EXPECT_STREQ(", cast(`a``b` as unsigned)", sql.ptr());
}

TEST(TestDDLColumnName, length_delimited_name)
{
  const char raw[] = {'a', '`', 'b', 'x'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
      ObColumnNameInfo(ObString(3, raw), false), true, false, false, sql));
  EXPECT_STREQ("`a``b`", sql.ptr());
}

TEST(TestDDLColumnName, column_lists_and_heap_hidden_key)
{
  ObArray<ObColumnNameInfo> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(
      ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("a`b"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("plain"), false)));
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, false, true, sql));
  EXPECT_STREQ("`a``b`, `plain`", sql.ptr());
  sql.reset();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, false, false, sql));
  ObSqlString expected;
  ASSERT_EQ(OB_SUCCESS, expected.append_fmt("`%s`, `a``b`, `plain`", OB_HIDDEN_PK_INCREMENT_COLUMN_NAME));
  EXPECT_STREQ(expected.ptr(), sql.ptr());
}

TEST(TestDDLColumnName, empty_column_list_is_invalid)
{
  ObArray<ObColumnNameInfo> columns;
  ObSqlString sql;
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObDDLUtil::generate_column_name_str(columns, true, false, false, sql));
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
