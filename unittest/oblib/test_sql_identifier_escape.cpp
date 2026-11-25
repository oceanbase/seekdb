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
#include "lib/string/ob_string.h"

using namespace oceanbase::common;

namespace
{
class IdentifierAllocator
{
public:
  IdentifierAllocator() : calls_(0), requested_(0), fail_(false) {}
  void *alloc(const int64_t size)
  {
    ++calls_;
    requested_ = size;
    return fail_ || size > static_cast<int64_t>(sizeof(buffer_)) ? nullptr : buffer_;
  }
  int calls_;
  int64_t requested_;
  bool fail_;
  char buffer_[1024];
};
}

TEST(TestSqlIdentifierEscape, EmptyNamesResetDestinationWithoutAllocation)
{
  IdentifierAllocator allocator;
  const ObString names[] = {ObString(), ObString(0, "")};
  for (const ObString &src : names) {
    ObString dst = ObString::make_string("previous");
    ASSERT_EQ(OB_SUCCESS, ob_escape_sql_identifier(allocator, src, dst));
    EXPECT_EQ(nullptr, dst.ptr());
    EXPECT_EQ(0, dst.length());
  }
  EXPECT_EQ(0, allocator.calls_);
}

TEST(TestSqlIdentifierEscape, PlainNamesKeepOriginalStorage)
{
  IdentifierAllocator allocator;
  allocator.fail_ = true;
  const char bytes[] = {'n', 'a', 'm', 'e'};
  const ObString src(sizeof(bytes), bytes);
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, ob_escape_sql_identifier(allocator, src, dst));
  EXPECT_EQ(src.ptr(), dst.ptr());
  EXPECT_EQ(src.length(), dst.length());
  EXPECT_EQ(0, allocator.calls_);
}

TEST(TestSqlIdentifierEscape, DoublesEveryBacktick)
{
  const char *names[] = {"`", "``", "`head", "tail`", "a`b", "a``b", "select", "a b", "a\\b", "列`名"};
  const char *expected[] = {"``", "````", "``head", "tail``", "a``b", "a````b", "select", "a b", "a\\b", "列``名"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    SCOPED_TRACE(names[i]);
    IdentifierAllocator allocator;
    const std::string original(names[i]);
    const ObString src = ObString::make_string(names[i]);
    ObString dst;
    ASSERT_EQ(OB_SUCCESS, ob_escape_sql_identifier(allocator, src, dst));
    EXPECT_EQ(std::string(expected[i]), std::string(dst.ptr(), dst.length()));
    EXPECT_EQ(original, std::string(src.ptr(), src.length()));
    if (allocator.calls_ != 0) {
      EXPECT_EQ(1, allocator.calls_);
      EXPECT_EQ(dst.length() + 1, allocator.requested_);
      EXPECT_EQ('\0', dst.ptr()[dst.length()]);
    }
  }
}

TEST(TestSqlIdentifierEscape, UsesLengthInsteadOfTerminator)
{
  IdentifierAllocator allocator;
  const char bytes[] = {'a', '`', 'b', '`'};
  const ObString src(3, bytes);
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, ob_escape_sql_identifier(allocator, src, dst));
  EXPECT_EQ("a``b", std::string(dst.ptr(), dst.length()));
  EXPECT_EQ(5, allocator.requested_);
  EXPECT_EQ('`', bytes[3]);
}

TEST(TestSqlIdentifierEscape, HandlesAllBacktickNames)
{
  IdentifierAllocator allocator;
  const std::string name(256, '`');
  const ObString src(name.size(), name.data());
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, ob_escape_sql_identifier(allocator, src, dst));
  EXPECT_EQ(std::string(512, '`'), std::string(dst.ptr(), dst.length()));
  EXPECT_EQ(513, allocator.requested_);
  EXPECT_EQ('\0', dst.ptr()[dst.length()]);
  EXPECT_EQ(std::string(256, '`'), name);
}

TEST(TestSqlIdentifierEscape, AllocationFailureClearsDestination)
{
  IdentifierAllocator allocator;
  allocator.fail_ = true;
  const ObString src = ObString::make_string("a`b");
  ObString dst = ObString::make_string("previous");
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ob_escape_sql_identifier(allocator, src, dst));
  EXPECT_EQ(nullptr, dst.ptr());
  EXPECT_EQ(0, dst.length());
  EXPECT_EQ(1, allocator.calls_);
  EXPECT_EQ(5, allocator.requested_);
}

TEST(TestSqlIdentifierEscape, SupportsSameSourceAndDestinationObject)
{
  IdentifierAllocator allocator;
  ObString name = ObString::make_string("a`b");
  ASSERT_EQ(OB_SUCCESS, ob_escape_sql_identifier(allocator, name, name));
  EXPECT_EQ("a``b", std::string(name.ptr(), name.length()));
  EXPECT_EQ('\0', name.ptr()[name.length()]);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
