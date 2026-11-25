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

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

namespace
{
class IdentifierTestAllocator : public ObIAllocator
{
public:
  explicit IdentifierTestAllocator(bool fail = false) : fail_(fail), calls_(0) {}
  ~IdentifierTestAllocator() override
  {
    for (void *ptr : allocations_) {
      std::free(ptr);
    }
  }
  void *alloc(const int64_t size) override
  {
    ++calls_;
    void *ptr = fail_ ? nullptr : std::malloc(size);
    if (nullptr != ptr) {
      allocations_.push_back(ptr);
    }
    return ptr;
  }
  void *alloc(const int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *) override {}
  int calls() const { return calls_; }

private:
  bool fail_;
  int calls_;
  std::vector<void *> allocations_;
};
} // namespace

TEST(TestSqlIdentifier, BorrowsUnescapedInput)
{
  IdentifierTestAllocator allocator(true);
  const ObString input = ObString::make_string("ordinary_column");
  ObString output;
  ASSERT_EQ(OB_SUCCESS, escape_sql_identifier(allocator, input, output));
  EXPECT_EQ(input.ptr(), output.ptr());
  EXPECT_EQ(input.length(), output.length());
  EXPECT_EQ(0, allocator.calls());
}

TEST(TestSqlIdentifier, DoublesOnlyBackticks)
{
  const char *inputs[] = {"a`b", "`", "``", "`a`", "列`名", "a\\b%`c"};
  const char *expected[] = {"a``b", "``", "````", "``a``", "列``名", "a\\b%``c"};
  IdentifierTestAllocator allocator;
  for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
    ObString output;
    ASSERT_EQ(OB_SUCCESS, escape_sql_identifier(
        allocator, ObString::make_string(inputs[i]), output));
    ASSERT_NE(nullptr, output.ptr());
    EXPECT_EQ(std::string(expected[i]), std::string(output.ptr(), output.length()));
    EXPECT_EQ('\0', output.ptr()[output.length()]);
  }
}

TEST(TestSqlIdentifier, ClearsOutputForEmptyInput)
{
  IdentifierTestAllocator allocator;
  ObString output = ObString::make_string("previous");
  ASSERT_EQ(OB_SUCCESS, escape_sql_identifier(allocator, ObString(), output));
  EXPECT_EQ(nullptr, output.ptr());
  EXPECT_EQ(0, output.length());
  EXPECT_EQ(0, allocator.calls());
}

TEST(TestSqlIdentifier, PreservesExplicitLength)
{
  IdentifierTestAllocator allocator;
  const char input[] = {'a', '\0', '`', 'b'};
  const char expected[] = {'a', '\0', '`', '`', 'b'};
  ObString output;
  ASSERT_EQ(OB_SUCCESS, escape_sql_identifier(
      allocator, ObString(sizeof(input), input), output));
  ASSERT_EQ(sizeof(expected), output.length());
  EXPECT_EQ(0, std::memcmp(expected, output.ptr(), sizeof(expected)));
  EXPECT_EQ('\0', output.ptr()[output.length()]);
}

TEST(TestSqlIdentifier, ReportsAllocationFailure)
{
  IdentifierTestAllocator allocator(true);
  ObString output = ObString::make_string("previous");
  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, escape_sql_identifier(
      allocator, ObString::make_string("a`b"), output));
  EXPECT_EQ(nullptr, output.ptr());
  EXPECT_EQ(0, output.length());
  EXPECT_EQ(1, allocator.calls());
}

TEST(TestSqlIdentifier, HandlesMaximumLengthQuotedName)
{
  IdentifierTestAllocator allocator;
  const std::string input(128, '`');
  ObString output;
  ASSERT_EQ(OB_SUCCESS, escape_sql_identifier(
      allocator, ObString(input.size(), input.data()), output));
  EXPECT_EQ(std::string(256, '`'), std::string(output.ptr(), output.length()));
}

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
