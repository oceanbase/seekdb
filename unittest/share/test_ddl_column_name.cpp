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

namespace oceanbase
{
namespace share
{
using namespace common;

namespace
{
void expect_column_name(
    const std::string &name,
    const bool origin,
    const bool alias,
    const bool comma,
    const bool shadow,
    const bool cast,
    const std::string &expected)
{
  ObSqlString sql;
  const ObString column_name(static_cast<int32_t>(name.size()), name.data());
  const ObColumnNameInfo info(column_name, shadow, cast);
  ASSERT_EQ(OB_SUCCESS, ObDDLUtil::generate_column_name_str(info, origin, alias, comma, sql));
  const std::string actual(sql.empty() ? "" : sql.ptr(), sql.length());
  EXPECT_EQ(expected, actual);
}
} // namespace

TEST(TestDDLColumnName, preserves_plain_names_and_output_flags)
{
  expect_column_name("amount", true, false, false, false, false, "`amount`");
  expect_column_name("amount", false, true, false, false, false, "`amount`");
  expect_column_name("amount", true, true, true, false, false, ", `amount` AS `amount`");
  expect_column_name("amount", false, false, false, false, false, "");
  expect_column_name("amount", false, false, true, false, false, ", ");
}

TEST(TestDDLColumnName, doubles_embedded_and_adjacent_backticks)
{
  expect_column_name("a`b", true, true, false, false, false, "`a``b` AS `a``b`");
  expect_column_name("`a`", true, false, false, false, false, "```a```");
  expect_column_name("``", true, false, false, false, false, "``````");
  expect_column_name("", true, true, false, false, false, "`` AS ``");
}

TEST(TestDDLColumnName, preserves_shadow_alias_and_enum_set_cast)
{
  expect_column_name("a`b", true, true, true, true, true,
      ", cast(`a``b` as unsigned) AS `__SHADOW_a``b`");
  expect_column_name("a`b", false, true, false, true, true, "`__SHADOW_a``b`");
  expect_column_name("amount", true, false, false, true, true, "cast(`amount` as unsigned)");
}

TEST(TestDDLColumnName, preserves_length_delimited_identifier_bytes)
{
  // Neither a zero byte nor a percent sign may truncate or format the input.
  expect_column_name(std::string("a\0`b", 4), true, false, false, false, false,
      std::string("`a\0``b`", 7));
  expect_column_name("a%`b", true, false, false, false, false, "`a%``b`");
  expect_column_name("\xe5\x88\x97`x", true, false, false, false, false,
      "`\xe5\x88\x97``x`");
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
