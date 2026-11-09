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

TEST(TestSqlStringIdentifier, PreservesOrdinaryBytes)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString::make_string("column ' with \\ and %")));
  EXPECT_EQ("column ' with \\ and %", std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlStringIdentifier, DoublesBackticks)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString::make_string("`a``b`")));
  EXPECT_EQ("``a````b``", std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlStringIdentifier, AppendsToExistingSql)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append("`__SHADOW_"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString::make_string("a`b")));
  ASSERT_EQ(OB_SUCCESS, sql.append("`"));
  EXPECT_EQ("`__SHADOW_a``b`", std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlStringIdentifier, EmptyStringIsANoop)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString()));
  EXPECT_EQ(0, sql.length());
  ASSERT_EQ(OB_SUCCESS, sql.append("existing"));
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString()));
  EXPECT_EQ("existing", std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlStringIdentifier, UsesExplicitLength)
{
  char name[] = {'a', '`', 'b', 'x'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(3, name)));
  EXPECT_EQ("a``b", std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlStringIdentifier, PreservesUtf8)
{
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString::make_string("列`名")));
  EXPECT_EQ("列``名", std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlStringIdentifier, GrowsAcrossBufferBoundary)
{
  std::string name(1024, '`');
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(name.size(), name.data())));
  EXPECT_EQ(std::string(2048, '`'), std::string(sql.ptr(), sql.length()));
}

TEST(TestSqlStringIdentifier, PreservesEmbeddedNul)
{
  char name[] = {'a', '\0', '`', 'b'};
  const char expected[] = {'a', '\0', '`', '`', 'b'};
  ObSqlString sql;
  ASSERT_EQ(OB_SUCCESS, sql.append_escaped_identifier(ObString(sizeof(name), name)));
  EXPECT_EQ(std::string(expected, sizeof(expected)), std::string(sql.ptr(), sql.length()));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
