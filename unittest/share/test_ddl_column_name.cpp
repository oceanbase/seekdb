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
namespace
{
void expect_quoted_column(const char *name, bool shadow, bool cast,
    bool origin, bool alias, bool comma, const char *expected)
{
  common::ObSqlString sql;
  ObColumnNameInfo info(common::ObString::make_string(name), shadow, cast);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, origin, alias, comma, sql));
  ASSERT_STREQ(expected, sql.ptr());
}
}

TEST(TestDDLColumnName, ordinary_and_reserved_identifiers)
{
  expect_quoted_column("name", false, false, true, false, false, "`name`");
  expect_quoted_column("select", false, false, true, true, true, ", `select` AS `select`");
}

TEST(TestDDLColumnName, embedded_and_boundary_backticks)
{
  expect_quoted_column("a`b", false, false, true, false, false, "`a``b`");
  expect_quoted_column("`a``", false, false, true, true, false, "```a````` AS ```a`````");
  expect_quoted_column("`", false, false, true, false, false, "````");
}

TEST(TestDDLColumnName, shadow_alias_and_unsigned_cast)
{
  expect_quoted_column("a`b", true, true, true, true, true,
      ", cast(`a``b` as unsigned) AS `__SHADOW_a``b`");
  expect_quoted_column("a`b", true, true, false, true, false, "`__SHADOW_a``b`");
  expect_quoted_column("a`b", true, true, true, false, false, "cast(`a``b` as unsigned)");
}

TEST(TestDDLColumnName, unicode_and_length_delimited_names)
{
  expect_quoted_column("列`名", false, false, true, true, false, "`列``名` AS `列``名`");
  const char name[] = {'a', '`', 'b', 'X'};
  common::ObSqlString sql;
  ObColumnNameInfo info(common::ObString(3, name), false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, true, false, false, sql));
  ASSERT_STREQ("`a``b`", sql.ptr());
}

TEST(TestDDLColumnName, no_requested_name_preserves_existing_sql)
{
  common::ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
  ObColumnNameInfo info(common::ObString::make_string("a`b"), false);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, false, false, false, sql));
  ASSERT_STREQ("prefix", sql.ptr());
}
}
}
