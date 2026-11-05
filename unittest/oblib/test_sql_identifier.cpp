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

#include <string>
#include <gtest/gtest.h>
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

TEST(TestSqlIdentifier, EscapesBackticksOnly)
{
  const char *inputs[] = {"plain", "select", "a`b", "`", "``", "`a`", "列`名", "a'b\\c"};
  const char *expected[] = {"plain", "select", "a``b", "``", "````", "``a``", "列``名", "a'b\\c"};
  for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(inputs[i])));
    EXPECT_EQ(std::string(expected[i]), std::string(sql.ptr(), sql.length()));
    EXPECT_EQ('\0', sql.ptr()[sql.length()]);
  }
}

TEST(TestSqlIdentifier, PreservesExistingSqlAndEmptyInput)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString()));
  EXPECT_EQ(0, sql.length());
  ASSERT_EQ(OB_SUCCESS, sql.append("cast(`__SHADOW_"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString("a`b")));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString("")));
  ASSERT_EQ(OB_SUCCESS, sql.append("` as unsigned)"));
  EXPECT_STREQ("cast(`__SHADOW_a``b` as unsigned)", sql.ptr());
}

TEST(TestSqlIdentifier, RespectsExplicitLength)
{
  const char input[] = {'a', '`', 'b', 'x'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(3, input)));
  EXPECT_STREQ("a``b", sql.ptr());

  const char input_with_zero[] = {'a', '\0', '`', 'b'};
  const char expected[] = {'a', '\0', '`', '`', 'b'};
  sql.reuse();
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(4, input_with_zero)));
  EXPECT_EQ(std::string(expected, sizeof(expected)), std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlIdentifier, GrowsForLongIdentifiers)
{
  const std::string input(2048, '`');
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("prefix:"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(input.size(), input.data())));
  EXPECT_EQ("prefix:" + std::string(4096, '`'), std::string(sql.ptr(), sql.length()));
  EXPECT_EQ('\0', sql.ptr()[sql.length()]);
}

TEST(TestSqlIdentifier, SupportsSelfReferencingInput)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.assign("a`b"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(sql.string()));
  EXPECT_STREQ("a`ba``b", sql.ptr());

  const std::string input(500, '`');
  ASSERT_EQ(OB_SUCCESS, sql.assign(input.data(), input.size()));
  ASSERT_GT(sql.length() * 3, sql.capacity());
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(sql.string()));
  EXPECT_EQ(std::string(1500, '`'), std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlIdentifier, SupportsSelfReferencingSliceAcrossGrowth)
{
  const std::string input = std::string(508, 'a') + "`b`";
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.assign(input.data(), input.size()));
  ASSERT_GT(sql.length() + 6, sql.capacity());
  const ObString slice(4, sql.ptr() + 507);
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(slice));
  EXPECT_EQ(input + "a``b``", std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlIdentifier, RejectsInvalidLengthsWithoutChangingOutput)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.assign("unchanged"));
  EXPECT_EQ(OB_INVALID_ARGUMENT, sql.append_escaped_identifier(ObString(-1, "x")));
  EXPECT_STREQ("unchanged", sql.ptr());
  const ObString invalid_slice(sql.length() + 1, sql.ptr());
  EXPECT_EQ(OB_INVALID_ARGUMENT, sql.append_escaped_identifier(invalid_slice));
  EXPECT_STREQ("unchanged", sql.ptr());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
