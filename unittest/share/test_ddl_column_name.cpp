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
#include "lib/container/ob_se_array.h"
#include "lib/string/ob_sql_string.h"

namespace oceanbase
{
namespace share
{
namespace
{
using namespace common;

// The escaped names below are explicit expectations, independent of the
// production escaping loop. This helper only assembles the formatting flags.
std::string expected_column(const std::string &escaped_name,
                            const bool with_origin,
                            const bool with_alias,
                            const bool shadow,
                            const bool needs_cast,
                            const bool with_comma)
{
  const std::string quoted = "`" + escaped_name + "`";
  std::string expected = with_comma ? ", " : "";
  if (with_origin) {
    expected += needs_cast ? "cast(" + quoted + " as unsigned)" : quoted;
  }
  if (with_origin && with_alias) {
    expected += " AS ";
  }
  if (with_alias) {
    expected += "`" + std::string(shadow ? "__SHADOW_" : "") + escaped_name + "`";
  }
  return expected;
}

void check_all_flags(const ObString &name, const std::string &escaped_name)
{
  // Origin, alias, shadow, ENUM/SET cast and comma are independent inputs.
  for (int flags = 0; flags < 32; ++flags) {
    SCOPED_TRACE(flags);
    const bool with_origin = (flags & 1) != 0;
    const bool with_alias = (flags & 2) != 0;
    const bool shadow = (flags & 4) != 0;
    const bool needs_cast = (flags & 8) != 0;
    const bool with_comma = (flags & 16) != 0;
    const ObColumnNameInfo info(name, shadow, needs_cast);
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
        info, with_origin, with_alias, with_comma, sql));
    const std::string expected = "prefix" + expected_column(
        escaped_name, with_origin, with_alias, shadow, needs_cast, with_comma);
    EXPECT_EQ(expected, std::string(sql.ptr(), sql.length()));
  }
}

TEST(DDLColumnName, quotes_identifiers_in_every_formatting_combination)
{
  struct NameCase
  {
    std::string name;
    std::string escaped;
  };
  const NameCase cases[] = {
      {"id", "id"},
      {"select", "select"},
      {"", ""},
      {"a`b", "a``b"},
      {"`leading", "``leading"},
      {"trailing`", "trailing``"},
      {"a``b", "a````b"},
      {"`", "``"},
      {"```", "``````"},
      {"中文`列", "中文``列"},
      {"a`, `b", "a``, ``b"},
      {std::string(128, 'x'), std::string(128, 'x')},
      {std::string(128, '`'), std::string(256, '`')},
      {std::string(126, 'x') + "``", std::string(126, 'x') + "````"},
  };
  for (const NameCase &item : cases) {
    SCOPED_TRACE(item.name);
    const ObString name(static_cast<int64_t>(item.name.length()), item.name.data());
    ASSERT_NO_FATAL_FAILURE(check_all_flags(name, item.escaped));
  }
}

TEST(DDLColumnName, honors_non_null_terminated_string_slices)
{
  // Neither slice ends at a NUL byte. Sentinel bytes must not enter the SQL.
  const char storage[] = {'X', 'a', '`', 'b', 'Y', '`', 'Z'};
  ASSERT_NO_FATAL_FAILURE(check_all_flags(ObString(3, storage + 1), "a``b"));
  ASSERT_NO_FATAL_FAILURE(check_all_flags(ObString(1, storage + 5), "``"));
}

TEST(DDLColumnName, skips_hidden_heap_keys_without_extra_commas)
{
  const ObColumnNameInfo hidden(ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false);
  const ObColumnNameInfo first(ObString::make_string("a`b"), true);
  const ObColumnNameInfo second(ObString::make_string("enum`col"), false, true);
  ObSEArray<ObColumnNameInfo, 5> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(hidden));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(first));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(hidden));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(second));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(hidden));

  for (int heap = 0; heap < 2; ++heap) {
    for (int flags = 0; flags < 4; ++flags) {
      SCOPED_TRACE(heap);
      SCOPED_TRACE(flags);
      const bool with_origin = (flags & 1) != 0;
      const bool with_alias = (flags & 2) != 0;
      ObSqlString sql;
      ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
      ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
          columns, with_origin, with_alias, heap != 0, sql));
      std::string expected = "prefix";
      bool with_comma = false;
      for (int i = 0; i < 5; ++i) {
        if (i == 1) {
          expected += expected_column("a``b", with_origin, with_alias, true, false, with_comma);
          with_comma = true;
        } else if (i == 3) {
          expected += expected_column("enum``col", with_origin, with_alias, false, true, with_comma);
          with_comma = true;
        } else if (heap == 0) {
          expected += expected_column("__pk_increment", with_origin, with_alias, false, false, with_comma);
          with_comma = true;
        }
      }
      EXPECT_EQ(expected, std::string(sql.ptr(), sql.length()));
    }
  }
}

TEST(DDLColumnName, all_hidden_heap_keys_leave_output_unchanged)
{
  ObSEArray<ObColumnNameInfo, 2> columns;
  const ObColumnNameInfo hidden(ObString::make_string(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME), false);
  ASSERT_EQ(OB_SUCCESS, columns.push_back(hidden));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(hidden));
  for (int flags = 0; flags < 4; ++flags) {
    SCOPED_TRACE(flags);
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
    ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(
        columns, (flags & 1) != 0, (flags & 2) != 0, true, sql));
    EXPECT_EQ("prefix", std::string(sql.ptr(), sql.length()));
  }
}

TEST(DDLColumnName, empty_column_array_is_rejected_without_appending)
{
  ObSEArray<ObColumnNameInfo, 1> columns;
  for (int heap = 0; heap < 2; ++heap) {
    for (int flags = 0; flags < 4; ++flags) {
      SCOPED_TRACE(heap);
      SCOPED_TRACE(flags);
      ObSqlString sql;
      ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
      EXPECT_EQ(OB_INVALID_ARGUMENT, ObDDLUtil::generate_column_name_str(
          columns, (flags & 1) != 0, (flags & 2) != 0, heap != 0, sql));
      EXPECT_EQ("prefix", std::string(sql.ptr(), sql.length()));
    }
  }
}

} // namespace
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
