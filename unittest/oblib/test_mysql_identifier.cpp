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

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "lib/string/ob_sql_string.h"

using namespace oceanbase::common;

namespace
{
class IdentifierAllocator : public ObIAllocator
{
public:
  explicit IdentifierAllocator(const bool fail = false)
    : fail_(fail), calls_(0), last_size_(0)
  {}
  ~IdentifierAllocator() override
  {
    for (void *ptr : allocations_) {
      std::free(ptr);
    }
  }
  void *alloc(const int64_t size) override
  {
    ++calls_;
    last_size_ = size;
    void *ptr = fail_ ? nullptr : std::malloc(size);
    if (nullptr != ptr) {
      allocations_.push_back(ptr);
    }
    return ptr;
  }
  void *alloc(const int64_t size, const ObMemAttr &) override
  {
    return alloc(size);
  }
  void free(void *ptr) override
  {
    for (void *&allocation : allocations_) {
      if (ptr == allocation) {
        std::free(allocation);
        allocation = nullptr;
        break;
      }
    }
  }
  const bool fail_;
  int64_t calls_;
  int64_t last_size_;
  std::vector<void *> allocations_;
};
}

TEST(TestMysqlIdentifier, empty_clears_output_without_allocating)
{
  IdentifierAllocator allocator(true);
  ObString dst = ObString::make_string("old output");
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, ObString(), dst));
  EXPECT_EQ(0, dst.length());
  EXPECT_EQ(nullptr, dst.ptr());
  EXPECT_EQ(0, allocator.calls_);
}

TEST(TestMysqlIdentifier, ordinary_names_borrow_the_input)
{
  IdentifierAllocator allocator(true);
  const ObString src = ObString::make_string("normal_column");
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, src, dst));
  EXPECT_EQ(src.ptr(), dst.ptr());
  EXPECT_EQ(src.length(), dst.length());
  EXPECT_EQ(0, allocator.calls_);
}

TEST(TestMysqlIdentifier, doubles_every_backtick)
{
  const char *inputs[] = {"e`n", "`lead", "trail`", "a``b", "`", "``", "列`名", "percent%`name"};
  const char *expected[] = {"e``n", "``lead", "trail``", "a````b", "``", "````", "列``名", "percent%``name"};
  IdentifierAllocator allocator;
  for (int64_t i = 0; i < static_cast<int64_t>(sizeof(inputs) / sizeof(inputs[0])); ++i) {
    SCOPED_TRACE(inputs[i]);
    const ObString src = ObString::make_string(inputs[i]);
    ObString dst;
    ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, src, dst));
    EXPECT_EQ(std::string(expected[i]), std::string(dst.ptr(), dst.length()));
    EXPECT_NE(src.ptr(), dst.ptr());
    EXPECT_EQ('\0', dst.ptr()[dst.length()]);
    EXPECT_EQ(dst.length() + 1, allocator.last_size_);
  }
}

TEST(TestMysqlIdentifier, respects_explicit_input_length)
{
  IdentifierAllocator allocator;
  const char bytes[] = {'a', '`', 'b', '`'};
  const ObString src(3, bytes);
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, src, dst));
  EXPECT_EQ("a``b", std::string(dst.ptr(), dst.length()));
}

TEST(TestMysqlIdentifier, preserves_embedded_nul_bytes)
{
  IdentifierAllocator allocator;
  const char bytes[] = {'a', '\0', '`', 'b'};
  const char expected[] = {'a', '\0', '`', '`', 'b'};
  const ObString src(sizeof(bytes), bytes);
  ObString dst;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, src, dst));
  ASSERT_EQ(sizeof(expected), static_cast<size_t>(dst.length()));
  EXPECT_EQ(0, std::memcmp(expected, dst.ptr(), sizeof(expected)));
  EXPECT_EQ('\0', dst.ptr()[dst.length()]);
}

TEST(TestMysqlIdentifier, allocation_failure_clears_stale_output)
{
  IdentifierAllocator allocator(true);
  const ObString src = ObString::make_string("a`b");
  ObString dst = ObString::make_string("stale output");
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, escape_mysql_identifier(allocator, src, dst));
  EXPECT_EQ(1, allocator.calls_);
  EXPECT_EQ(nullptr, dst.ptr());
  EXPECT_EQ(0, dst.length());
}

TEST(TestMysqlIdentifier, supports_the_same_input_and_output_string)
{
  IdentifierAllocator allocator;
  ObString name = ObString::make_string("a`b");
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, name, name));
  EXPECT_EQ("a``b", std::string(name.ptr(), name.length()));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
