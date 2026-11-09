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

using namespace oceanbase::common;
using namespace oceanbase::share;

TEST(DDLColumnName, quotes_all_formatter_branches)
{
  struct Case { const char *name; const char *quoted; };
  const Case cases[] = {
    {"ordinary", "`ordinary`"},
    {"select", "`select`"},
    {"a`b", "`a``b`"},
    {"`", "````"},
    {"``", "``````"},
    {"`edge`", "```edge```"},
    {"a\\b", "`a\\b`"},
    {"a'b", "`a'b`"},
    {"with space", "`with space`"},
    {"", "``"},
  };
  for (const auto &item : cases) {
    for (int flags = 0; flags < 32; ++flags) {
      const bool origin = flags & 1;
      const bool alias = flags & 2;
      const bool comma = flags & 4;
      const bool shadow = flags & 8;
      const bool cast = flags & 16;
      SCOPED_TRACE(::testing::Message() << item.name << ":" << flags);
      ObSqlString sql;
      ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
      ObColumnNameInfo info(ObString::make_string(item.name), shadow, cast);
      ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, origin, alias, comma, sql));
      std::string expected = "prefix";
      if (comma) { expected += ", "; }
      if (origin) {
        expected += cast ? std::string("cast(") + item.quoted + " as unsigned)" : item.quoted;
      }
      if (origin && alias) { expected += " AS "; }
      if (alias) {
        expected += shadow ? std::string("`__SHADOW_") + (item.quoted + 1) : item.quoted;
      }
      EXPECT_EQ(expected, std::string(sql.ptr(), sql.length()));
    }
  }
}

TEST(DDLColumnName, respects_length_and_preserves_utf8)
{
  const char bytes[] = {'a', '`', 'b', 'X'};
  ObColumnNameInfo info(ObString(3, bytes), false);
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, true, true, false, sql));
  EXPECT_EQ("`a``b` AS `a``b`", std::string(sql.ptr(), sql.length()));
  sql.reset();
  const char utf8[] = "\xE5\x88\x97`\xE5\x90\x8D";
  info.column_name_ = ObString::make_string(utf8);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, true, false, false, sql));
  EXPECT_EQ("`\xE5\x88\x97``\xE5\x90\x8D`", std::string(sql.ptr(), sql.length()));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
