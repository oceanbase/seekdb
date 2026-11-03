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

#include <cstring>
#include <string>
#include <gtest/gtest.h>
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

TEST(TestSqlStringIdentifier, EscapesEveryBacktick)
{
  const char *names[] = {"column", "a`b", "`", "``", "`a`", "a``b`", "select", "a\\b'c"};
  const char *escaped[] = {"column", "a``b", "``", "````", "``a``", "a````b``", "select", "a\\b'c"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    ObSqlString sql;
    ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString::make_string(names[i])));
    EXPECT_STREQ(escaped[i], sql.ptr());
    EXPECT_EQ(strlen(escaped[i]), static_cast<size_t>(sql.length()));
  }
}

TEST(TestSqlStringIdentifier, PreservesLengthAndExistingSql)
{
  // The identifier need not be NUL-terminated; bytes beyond its length are not read.
  char name[] = {'a', '`', 'b', 'x'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("SELECT `"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(3, name)));
  ASSERT_EQ(OB_SUCCESS, sql.append("`"));
  EXPECT_STREQ("SELECT `a``b`", sql.ptr());

  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString()));
  EXPECT_STREQ("SELECT `a``b`", sql.ptr());
  sql.reuse();
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString::make_string("plain")));
  EXPECT_STREQ("plain", sql.ptr());
}

TEST(TestSqlStringIdentifier, PreservesMultibyteAndEmbeddedNul)
{
  char name[] = {'\xe5', '\x88', '\x97', '`', '\0', 'x'};
  const char expected[] = {'\xe5', '\x88', '\x97', '`', '`', '\0', 'x'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(sizeof(name), name)));
  ASSERT_EQ(sizeof(expected), static_cast<size_t>(sql.length()));
  EXPECT_EQ(0, memcmp(expected, sql.ptr(), sizeof(expected)));
  EXPECT_EQ('\0', sql.ptr()[sql.length()]);
}

TEST(TestSqlStringIdentifier, GrowsOutputAcrossEscapedSegments)
{
  const std::string name(1024, '`');
  const std::string expected(2048, '`');
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("prefix:"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(name.size(), name.data())));
  EXPECT_EQ("prefix:" + expected, std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlStringIdentifier, RejectsNegativeLengthWithoutChangingOutput)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("prefix"));
  EXPECT_EQ(OB_INVALID_ARGUMENT, sql.append_escaped_identifier(ObString(-1, "x")));
  EXPECT_STREQ("prefix", sql.ptr());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
