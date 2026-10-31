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

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestDDLCommon, quoted_column_names)
{
  const struct {
    const char *name;
    const char *expected;
  } cases[] = {
    {"plain", "`plain`"},
    {"select", "`select`"},
    {"a`b", "`a``b`"},
    {"`leading", "```leading`"},
    {"trailing`", "`trailing```"},
    {"a``b", "`a````b`"},
    {"`", "````"},
    {"a\\b", "`a\\b`"},
  };
  for (const auto &test : cases) {
    SCOPED_TRACE(test.name);
    ObSqlString sql;
    const ObColumnNameInfo column(ObString::make_string(test.name), false);
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, true, false, false, sql));
    EXPECT_STREQ(test.expected, sql.ptr());
  }
}

TEST(TestDDLCommon, aliases_and_enum_set_casts)
{
  ObSqlString sql;
  const ObString name = ObString::make_string("a`b");
  const ObColumnNameInfo column(name, false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(column, false, true, false, sql));
  EXPECT_STREQ("`a``b`", sql.ptr());

  sql.reuse();
  const ObColumnNameInfo shadow(name, true);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(shadow, true, true, true, sql));
  EXPECT_STREQ(", `a``b` AS `__SHADOW_a``b`", sql.ptr());

  sql.reuse();
  const ObColumnNameInfo enum_set(name, false, true);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(enum_set, true, true, false, sql));
  EXPECT_STREQ("cast(`a``b` as unsigned) AS `a``b`", sql.ptr());

  sql.reuse();
  const ObColumnNameInfo shadow_enum_set(name, true, true);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(shadow_enum_set, true, true, false, sql));
  EXPECT_STREQ("cast(`a``b` as unsigned) AS `__SHADOW_a``b`", sql.ptr());

  sql.reuse();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(shadow_enum_set, false, true, false, sql));
  EXPECT_STREQ("`__SHADOW_a``b`", sql.ptr());
}

TEST(TestDDLCommon, column_list_skips_hidden_heap_key)
{
  ObArray<ObColumnNameInfo> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(
      ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("a`b"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("c`d"), false)));
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, true, true, sql));
  EXPECT_STREQ("`a``b` AS `a``b`, `c``d` AS `c``d`", sql.ptr());
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
