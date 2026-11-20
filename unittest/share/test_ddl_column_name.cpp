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
#include "lib/container/ob_array.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_ddl_common.h"

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestDDLColumnName, quote_names_for_all_formatting_options)
{
  const struct {
    const char *name;
    const char *escaped_name;
  } cases[] = {
    {"ordinary", "ordinary"},
    {"a`b", "a``b"},
    {"`leading", "``leading"},
    {"trailing`", "trailing``"},
    {"a``b", "a````b"},
    {"`", "``"},
    {"中文`列", "中文``列"},
    {"select", "select"},
    {"with space", "with space"},
  };
  for (const auto &test_case : cases) {
    for (int flags = 0; flags < 32; ++flags) {
      SCOPED_TRACE(::testing::Message() << test_case.name << ", flags=" << flags);
      const bool with_origin = (flags & 1) != 0;
      const bool with_alias = (flags & 2) != 0;
      const bool with_comma = (flags & 4) != 0;
      const bool shadow = (flags & 8) != 0;
      const bool cast_to_unsigned = (flags & 16) != 0;
      const std::string quoted_name = "`" + std::string(test_case.escaped_name) + "`";
      std::string expected = "prefix";
      if (with_comma) {
        expected += ", ";
      }
      if (with_origin) {
        expected += cast_to_unsigned ? "cast(" + quoted_name + " as unsigned)" : quoted_name;
      }
      if (with_origin && with_alias) {
        expected += " AS ";
      }
      if (with_alias) {
        expected += shadow ? "`__SHADOW_" + std::string(test_case.escaped_name) + "`" : quoted_name;
      }
      ObSqlString sql;
      ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
      const ObColumnNameInfo info(ObString(test_case.name), shadow, cast_to_unsigned);
      ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
          info, with_origin, with_alias, with_comma, sql));
      EXPECT_EQ(expected, std::string(sql.ptr(), sql.length()));
    }
  }
}

TEST(TestDDLColumnName, respect_string_length_and_grow_the_output_buffer)
{
  const char name[] = {'a', '`', 'b', '`'};
  ObSqlString sql;
  const ObColumnNameInfo bounded_name(ObString(3, name), false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(bounded_name, true, false, false, sql));
  EXPECT_EQ("`a``b`", std::string(sql.ptr(), sql.length()));

  sql.reset();
  const std::string long_name(1024, '`');
  const ObColumnNameInfo long_info(ObString(long_name.size(), long_name.data()), false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(long_info, true, false, false, sql));
  EXPECT_EQ(std::string(2050, '`'), std::string(sql.ptr(), sql.length()));
}

TEST(TestDDLColumnName, separate_column_list_entries)
{
  ObArray<ObColumnNameInfo> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString("a`b"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString("c``d"), true, true)));
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, true, false, sql));
  EXPECT_EQ("`a``b` AS `a``b`, cast(`c````d` as unsigned) AS `__SHADOW_c````d`",
      std::string(sql.ptr(), sql.length()));
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
