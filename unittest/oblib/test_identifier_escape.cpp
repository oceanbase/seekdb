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
#include "lib/allocator/page_arena.h"
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

class FailingAllocator final : public ObIAllocator
{
public:
  void *alloc(const int64_t) override { return nullptr; }
  void *alloc(const int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};

TEST(TestIdentifierEscape, BorrowUnescapedInput)
{
  FailingAllocator allocator;
  const ObString src = ObString::make_string("plain name");
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, src, dst));
  EXPECT_EQ(src.ptr(), dst.ptr());
  EXPECT_EQ(src.length(), dst.length());
}

TEST(TestIdentifierEscape, DoubleEveryBacktick)
{
  const struct {
    const char *name;
    const char *expected;
  } cases[] = {
    {"a`b", "a``b"},
    {"a``b", "a````b"},
    {"`", "``"},
    {"`id`", "``id``"},
  };
  ObArenaAllocator allocator;
  for (const auto &test : cases) {
    SCOPED_TRACE(test.name);
    const ObString src = ObString::make_string(test.name);
    ObString dst;
    ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, src, dst));
    EXPECT_NE(src.ptr(), dst.ptr());
    EXPECT_EQ(ObString::make_string(test.expected), dst);
    EXPECT_EQ('\0', dst.ptr()[dst.length()]);
  }
}

TEST(TestIdentifierEscape, LengthDelimitedInputAndInPlaceOutput)
{
  ObArenaAllocator allocator;
  char name[] = {'a', '`', '`', 'b', 'x'};
  ObString src(0, 4, name);
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, src, src));
  EXPECT_EQ(ObString::make_string("a````b"), src);
}

TEST(TestIdentifierEscape, EmptyInputAndAllocationFailure)
{
  FailingAllocator allocator;
  ObString dst = ObString::make_string("old");
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, ObString(), dst));
  EXPECT_EQ(0, dst.length());
  EXPECT_EQ(nullptr, dst.ptr());
  dst = ObString::make_string("old");
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      escape_mysql_identifier(allocator, ObString::make_string("a`b"), dst));
  EXPECT_EQ(0, dst.length());
  EXPECT_EQ(nullptr, dst.ptr());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
