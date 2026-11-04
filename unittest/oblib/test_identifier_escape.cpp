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
#include "lib/string/ob_identifier_escape.h"

using namespace oceanbase::common;

TEST(IdentifierEscape, DoublesBackticks)
{
  const char *names[] = {"id", "key`id", "`id`", "a``b", "```", "列`名"};
  const char *expected[] = {"id", "key``id", "``id``", "a````b", "``````", "列``名"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    char buffer[64];
    int64_t length = -1;
    ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(names[i], std::strlen(names[i]),
        buffer, sizeof(buffer), length));
    EXPECT_STREQ(expected[i], buffer);
    EXPECT_EQ(std::strlen(expected[i]), static_cast<size_t>(length));
  }
}

TEST(IdentifierEscape, EmptyName)
{
  char buffer[1] = {'x'};
  int64_t length = -1;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(nullptr, 0, buffer, sizeof(buffer), length));
  EXPECT_EQ(0, length);
  EXPECT_EQ('\0', buffer[0]);
}

TEST(IdentifierEscape, UsesExplicitInputLength)
{
  const char name[] = {'a', '`', 'b', '`'};
  char buffer[8];
  int64_t length = -1;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(name, 3, buffer, sizeof(buffer), length));
  EXPECT_STREQ("a``b", buffer);
  EXPECT_EQ(4, length);
}

TEST(IdentifierEscape, PreservesEmbeddedNul)
{
  const char name[] = {'a', '\0', '`', 'b'};
  const char expected[] = {'a', '\0', '`', '`', 'b', '\0'};
  char buffer[6];
  int64_t length = -1;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(name, sizeof(name), buffer, sizeof(buffer), length));
  EXPECT_EQ(5, length);
  EXPECT_EQ(0, std::memcmp(expected, buffer, sizeof(expected)));
}

TEST(IdentifierEscape, ExactCapacityAndOverflow)
{
  char buffer[5] = "keep";
  int64_t length = -1;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, escape_mysql_identifier("a`b", 3, buffer, sizeof(buffer) - 1, length));
  EXPECT_STREQ("keep", buffer);
  EXPECT_EQ(0, length);
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier("a`b", 3, buffer, sizeof(buffer), length));
  EXPECT_STREQ("a``b", buffer);
  EXPECT_EQ(4, length);
}

TEST(IdentifierEscape, MaximumBinaryColumnName)
{
  const std::string name(512, '`');
  char buffer[1025];
  int64_t length = -1;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(name.data(), name.size(), buffer, sizeof(buffer), length));
  EXPECT_EQ(std::string(1024, '`'), std::string(buffer, length));
  EXPECT_EQ('\0', buffer[1024]);
}

TEST(IdentifierEscape, InPlace)
{
  char buffer[16] = "`a``b`";
  int64_t length = -1;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(buffer, std::strlen(buffer), buffer, sizeof(buffer), length));
  EXPECT_STREQ("``a````b``", buffer);
  EXPECT_EQ(10, length);
}

TEST(IdentifierEscape, InvalidArguments)
{
  char buffer[8] = "keep";
  int64_t length = -1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, escape_mysql_identifier(nullptr, 1, buffer, sizeof(buffer), length));
  EXPECT_EQ(OB_INVALID_ARGUMENT, escape_mysql_identifier("a", -1, buffer, sizeof(buffer), length));
  EXPECT_EQ(OB_INVALID_ARGUMENT, escape_mysql_identifier("a", 1, nullptr, sizeof(buffer), length));
  EXPECT_EQ(OB_INVALID_ARGUMENT, escape_mysql_identifier("a", 1, buffer, 0, length));
  EXPECT_EQ(OB_INVALID_ARGUMENT, escape_mysql_identifier("a", 1, buffer, -1, length));
  EXPECT_STREQ("keep", buffer);
  EXPECT_EQ(0, length);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
