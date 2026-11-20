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

#include "share/ob_ddl_identifier_escape.h"

#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace oceanbase
{
namespace share
{
using namespace common;

namespace
{
std::string escape_for_test(const char *input)
{
  ObArenaAllocator allocator("TestEsc");
  ObString src(input);
  ObString dst;
  const int ret = escape_ddl_identifier(allocator, src, dst);
  EXPECT_EQ(OB_SUCCESS, ret);
  if (NULL == dst.ptr()) {
    return std::string();
  }
  return std::string(dst.ptr(), dst.length());
}
} // anonymous namespace

TEST(TestEscapeDdlIdentifier, NullAndEmptyInputYieldEmptyResult)
{
  ObArenaAllocator allocator("TestEsc");
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, escape_ddl_identifier(allocator, ObString(), dst));
  EXPECT_EQ(0, dst.length());
  EXPECT_TRUE(NULL == dst.ptr());

  char buf[1] = {'x'};
  ObString empty(0, buf);
  ObString dst2;
  ASSERT_EQ(OB_SUCCESS, escape_ddl_identifier(allocator, empty, dst2));
  EXPECT_EQ(0, dst2.length());
}

TEST(TestEscapeDdlIdentifier, PlainIdentifierAliasesSource)
{
  ObArenaAllocator allocator("TestEsc");
  ObString src("plain_column");
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, escape_ddl_identifier(allocator, src, dst));
  EXPECT_EQ(src, dst);
  EXPECT_EQ(src.ptr(), dst.ptr());
}

TEST(TestEscapeDdlIdentifier, DoublesEmbeddedBackticks)
{
  EXPECT_EQ(std::string("a``b``c"), escape_for_test("a`b`c"));
  EXPECT_EQ(std::string("col``name"), escape_for_test("col`name"));
}

TEST(TestEscapeDdlIdentifier, DoublesLeadingAndTrailingBackticks)
{
  EXPECT_EQ(std::string("``abc"), escape_for_test("`abc"));
  EXPECT_EQ(std::string("abc``"), escape_for_test("abc`"));
  EXPECT_EQ(std::string("``abc``"), escape_for_test("`abc`"));
}

TEST(TestEscapeDdlIdentifier, BacktickOnlyIdentifier)
{
  EXPECT_EQ(std::string("``"), escape_for_test("`"));
  EXPECT_EQ(std::string("``````"), escape_for_test("```"));
}

TEST(TestEscapeDdlIdentifier, ResultIsNullTerminated)
{
  ObArenaAllocator allocator("TestEsc");
  ObString src("col`name");
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, escape_ddl_identifier(allocator, src, dst));
  ASSERT_EQ(9, dst.length());
  ASSERT_TRUE(NULL != dst.ptr());
  EXPECT_EQ('\0', dst.ptr()[dst.length()]);
  EXPECT_EQ(0, std::strcmp(dst.ptr(), "col``name"));
}

TEST(TestEscapeDdlIdentifier, KeepsSourceBytesUntouched)
{
  ObArenaAllocator allocator("TestEsc");
  char buf[] = "col`name";
  ObString src(static_cast<int64_t>(std::strlen(buf)), buf);
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, escape_ddl_identifier(allocator, src, dst));
  EXPECT_EQ(0, std::strcmp(buf, "col`name"));
  EXPECT_EQ(std::string("col``name"), std::string(dst.ptr(), dst.length()));
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
