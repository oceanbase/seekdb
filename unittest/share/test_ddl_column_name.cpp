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

#include "share/ob_ddl_common.h"
#include <gtest/gtest.h>

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestDDLColumnName, quoted_identifiers)
{
  const char *names[] = {"plain", "select", "a`b", "`leading", "trailing`", "a``b", "`", "列`名"};
  const char *quoted[] = {
    "`plain`", "`select`", "`a``b`", "```leading`", "`trailing```", "`a````b`", "````", "`列``名`"
  };
  const char *shadow[] = {
    "`__SHADOW_plain`", "`__SHADOW_select`", "`__SHADOW_a``b`", "`__SHADOW_``leading`",
    "`__SHADOW_trailing```", "`__SHADOW_a````b`", "`__SHADOW_```", "`__SHADOW_列``名`"
  };
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    for (int flags = 0; flags < 32; ++flags) {
      SCOPED_TRACE(::testing::Message() << names[i] << " flags=" << flags);
      const bool origin = flags & 1;
      const bool alias = flags & 2;
      const bool comma = flags & 4;
      const bool is_shadow = flags & 8;
      const bool cast = flags & 16;
      ObColumnNameInfo info(ObString::make_string(names[i]), is_shadow, cast);
      ObSqlString actual;
      ObSqlString expected;
      ASSERT_EQ(OB_SUCCESS, actual.append("prefix "));
      ASSERT_EQ(OB_SUCCESS, expected.append("prefix "));
      if (comma) {
        ASSERT_EQ(OB_SUCCESS, expected.append(", "));
      }
      if (origin) {
        ASSERT_EQ(OB_SUCCESS, expected.append_fmt(cast ? "cast(%s as unsigned)" : "%s", quoted[i]));
      }
      if (origin && alias) {
        ASSERT_EQ(OB_SUCCESS, expected.append(" AS "));
      }
      if (alias) {
        ASSERT_EQ(OB_SUCCESS, expected.append(is_shadow ? shadow[i] : quoted[i]));
      }
      ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, origin, alias, comma, actual));
      EXPECT_STREQ(expected.ptr(), actual.ptr());
    }
  }
}

TEST(TestDDLColumnName, length_delimited_identifier)
{
  // Bytes after the ObString's length are not part of the identifier.
  char name[] = {'a', '`', 'b', 'x'};
  ObColumnNameInfo info(ObString(3, name), true, true);
  ObSqlString actual;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, true, true, false, actual));
  EXPECT_STREQ("cast(`a``b` as unsigned) AS `__SHADOW_a``b`", actual.ptr());
}

TEST(TestDDLColumnName, column_list_and_heap_hidden_key)
{
  ObArray<ObColumnNameInfo> columns;
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("__pk_increment"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("a`b"), false)));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(ObColumnNameInfo(ObString::make_string("c``d"), true)));
  ObSqlString actual;
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, true, true, true, actual));
  EXPECT_STREQ("`a``b` AS `a``b`, `c````d` AS `__SHADOW_c````d`", actual.ptr());

  actual.reuse();
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(columns, false, true, false, actual));
  EXPECT_STREQ("`__pk_increment`, `a``b`, `__SHADOW_c````d`", actual.ptr());

  columns.reset();
  actual.reuse();
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObDDLUtil::generate_column_name_str(columns, true, true, false, actual));
}
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
