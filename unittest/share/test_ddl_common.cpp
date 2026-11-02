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
#include "lib/container/ob_array.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace share
{
using namespace common;

class TestDDLCommon : public ::testing::Test
{
protected:
  void check_column(const ObString &name, const bool shadow, const bool cast,
                    const bool origin, const bool alias, const bool comma,
                    const char *expected)
  {
    ObSqlString sql;
    const ObColumnNameInfo info(name, shadow, cast);
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, origin, alias, comma, sql));
    EXPECT_EQ(std::string(expected), std::string(sql.ptr() == nullptr ? "" : sql.ptr(), sql.length()));
  }
};

TEST_F(TestDDLCommon, quote_column_names)
{
  check_column(ObString::make_string("plain"), false, false, true, false, false, "`plain`");
  check_column(ObString::make_string("order"), false, false, true, false, false, "`order`");
  check_column(ObString::make_string("key id"), false, false, true, false, false, "`key id`");
  check_column(ObString::make_string("key`id"), false, false, true, false, false, "`key``id`");
  check_column(ObString::make_string("`key"), false, false, true, false, false, "```key`");
  check_column(ObString::make_string("key`"), false, false, true, false, false, "`key```");
  check_column(ObString::make_string("a``b"), false, false, true, false, false, "`a````b`");
  check_column(ObString::make_string("``"), false, false, true, false, false, "``````");
  check_column(ObString::make_string("列`名"), false, false, true, false, false, "`列``名`");
}

TEST_F(TestDDLCommon, preserve_formatting_options)
{
  const ObString name = ObString::make_string("key`id");
  check_column(name, false, false, true, true, false, "`key``id` AS `key``id`");
  check_column(name, true, false, true, true, false, "`key``id` AS `__SHADOW_key``id`");
  check_column(name, true, false, false, true, true, ", `__SHADOW_key``id`");
  check_column(name, true, true, true, true, true,
               ", cast(`key``id` as unsigned) AS `__SHADOW_key``id`");
  check_column(name, false, true, true, false, false, "cast(`key``id` as unsigned)");
  check_column(name, false, true, false, true, false, "`key``id`");
  check_column(name, false, false, false, false, false, "");
  check_column(name, false, false, false, false, true, ", ");
  check_column(ObString::make_string("plain"), true, true, true, true, false,
               "cast(`plain` as unsigned) AS `__SHADOW_plain`");
}

TEST_F(TestDDLCommon, length_delimited_and_empty_names)
{
  char buffer[] = {'k', '`', 'x', 'Z'};
  check_column(ObString(3, buffer), false, false, true, false, false, "`k``x`");
  check_column(ObString(), false, false, true, false, false, "``");
  check_column(ObString(), true, true, true, true, false,
               "cast(`` as unsigned) AS `__SHADOW_`");
}

TEST_F(TestDDLCommon, append_to_existing_sql)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("SELECT "));
  const ObColumnNameInfo info(ObString::make_string("key`id"), true, true);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, true, true, false, sql));
  EXPECT_STREQ("SELECT cast(`key``id` as unsigned) AS `__SHADOW_key``id`", sql.ptr());
}

TEST_F(TestDDLCommon, column_list_and_hidden_heap_key)
{
  ObArray<ObColumnNameInfo> columns;
  ObSqlString sql;
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObDDLUtil::generate_column_name_str(columns, true, false, true, sql));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(
      ObColumnNameInfo(ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false)));
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, false, true, sql));
  EXPECT_TRUE(sql.empty());
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("key`id"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("enum`col"), true, true)));
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, true, true, sql));
  EXPECT_STREQ("`key``id` AS `key``id`, cast(`enum``col` as unsigned) AS `__SHADOW_enum``col`", sql.ptr());
  sql.reuse();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, false, false, sql));
  EXPECT_STREQ("`__pk_increment`, `key``id`, cast(`enum``col` as unsigned)", sql.ptr());
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
