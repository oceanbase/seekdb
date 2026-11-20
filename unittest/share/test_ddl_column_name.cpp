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
#include "share/ob_ddl_common.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

TEST(TestDDLColumnName, IdentifierContents)
{
  const struct {
    const char *name;
    const char *expected;
  } cases[] = {
    {"id", "`id` AS `id`"},
    {"order", "`order` AS `order`"},
    {"key id", "`key id` AS `key id`"},
    {"key`id", "`key``id` AS `key``id`"},
    {"key``id", "`key````id` AS `key````id`"},
    {"`", "```` AS ````"},
    {"`id`", "```id``` AS ```id```"},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.name);
    ObSqlString sql;
    const ObColumnNameInfo column(ObString::make_string(test.name), false);
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, true, false, sql));
    EXPECT_STREQ(test.expected, sql.ptr());
  }
}

TEST(TestDDLColumnName, FormattingFlags)
{
  const struct {
    bool origin;
    bool alias;
    bool shadow;
    bool cast;
    bool comma;
    const char *expected;
  } cases[] = {
    {true, false, false, false, false, "`key``id`"},
    {false, true, false, false, false, "`key``id`"},
    {true, true, true, false, false, "`key``id` AS `__SHADOW_key``id`"},
    {false, true, true, false, false, "`__SHADOW_key``id`"},
    {true, false, true, false, false, "`key``id`"},
    {true, true, false, true, false, "cast(`key``id` as unsigned) AS `key``id`"},
    {true, false, false, true, false, "cast(`key``id` as unsigned)"},
    {true, true, true, true, true, ", cast(`key``id` as unsigned) AS `__SHADOW_key``id`"},
    {false, true, true, true, false, "`__SHADOW_key``id`"},
    {false, false, true, true, false, ""},
    {false, false, true, true, true, ", "},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.expected);
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.assign("SELECT "));
    const ObColumnNameInfo column(ObString::make_string("key`id"), test.shadow, test.cast);
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
        column, test.origin, test.alias, test.comma, sql));
    EXPECT_STREQ(test.expected, sql.ptr() + sizeof("SELECT ") - 1);
  }
}

TEST(TestDDLColumnName, ColumnListAndHeapHiddenPrimaryKey)
{
  ObSEArray<ObColumnNameInfo, 3> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(
      ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("id"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("a``b"), true, true)));
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, true, true, sql));
  EXPECT_STREQ("`id` AS `id`, cast(`a````b` as unsigned) AS `__SHADOW_a````b`", sql.ptr());
  sql.reuse();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, false, true, false, sql));
  EXPECT_STREQ("`__pk_increment`, `id`, `__SHADOW_a````b`", sql.ptr());
  columns.reset();
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObDDLUtil::generate_column_name_str(columns, true, true, false, sql));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
