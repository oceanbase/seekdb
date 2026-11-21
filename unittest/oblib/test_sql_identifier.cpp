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

#include <array>
#include <cstring>
#include <string>
#include <gtest/gtest.h>
#include "lib/string/ob_sql_identifier.h"

using namespace oceanbase::common;

namespace
{
class FixedBufferAllocator
{
public:
  void *alloc(const int64_t size)
  {
    ++allocations_;
    allocated_size_ = size;
    return fail_ || size <= 0 || size > static_cast<int64_t>(buffer_.size()) ? nullptr : buffer_.data();
  }

  std::array<char, 4096> buffer_{};
  int64_t allocated_size_ = 0;
  int allocations_ = 0;
  bool fail_ = false;
};

TEST(SqlIdentifier, ordinary_names_borrow_the_original_buffer)
{
  FixedBufferAllocator allocator;
  allocator.fail_ = true;
  const char name[] = "column_name";
  const char *escaped = nullptr;
  int32_t length = 0;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, name, static_cast<int32_t>(sizeof(name) - 1), escaped, length));
  EXPECT_EQ(name, escaped);
  EXPECT_EQ(static_cast<int32_t>(sizeof(name) - 1), length);
  EXPECT_EQ(0, allocator.allocations_);
}

TEST(SqlIdentifier, backticks_are_doubled_without_adding_delimiters)
{
  const std::pair<std::string, std::string> cases[] = {
      {"`", "``"},
      {"key`id", "key``id"},
      {"`leading", "``leading"},
      {"trailing`", "trailing``"},
      {"two``ticks", "two````ticks"},
      {"`a`b`", "``a``b``"},
      {"__SHADOW_key`id", "__SHADOW_key``id"},
      {"列`名", "列``名"},
      {"a'\"\\b`c", "a'\"\\b``c"},
      {std::string(1024, '`'), std::string(2048, '`')},
  };
  for (const auto &entry : cases) {
    SCOPED_TRACE(entry.first);
    FixedBufferAllocator allocator;
    const char *escaped = nullptr;
    int32_t length = 0;
    ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(
        allocator, entry.first.data(), static_cast<int32_t>(entry.first.size()), escaped, length));
    ASSERT_NE(nullptr, escaped);
    EXPECT_EQ(entry.second, std::string(escaped, length));
    EXPECT_EQ('\0', escaped[length]);
    EXPECT_EQ(1, allocator.allocations_);
    EXPECT_EQ(length + 1, allocator.allocated_size_);
    EXPECT_NE(entry.first.data(), escaped);
  }
}

TEST(SqlIdentifier, null_and_empty_names_clear_the_output_without_allocating)
{
  const char *sources[] = {nullptr, ""};
  for (const char *src : sources) {
    FixedBufferAllocator allocator;
    const char *escaped = "previous";
    int32_t length = 8;
    ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, src, 0, escaped, length));
    EXPECT_EQ(nullptr, escaped);
    EXPECT_EQ(0, length);
    EXPECT_EQ(0, allocator.allocations_);
  }
}

TEST(SqlIdentifier, allocation_failure_does_not_publish_a_partial_identifier)
{
  FixedBufferAllocator allocator;
  allocator.fail_ = true;
  const char *escaped = "previous";
  int32_t length = 8;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, escape_mysql_identifier(allocator, "a`b", 3, escaped, length));
  EXPECT_EQ(nullptr, escaped);
  EXPECT_EQ(0, length);
}

TEST(SqlIdentifier, respects_the_input_span_instead_of_a_c_string_terminator)
{
  FixedBufferAllocator allocator;
  const char name[] = {'a', '`', 'b', '`'};
  const char *escaped = nullptr;
  int32_t length = 0;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, name, 3, escaped, length));
  EXPECT_EQ("a``b", std::string(escaped, length));
  EXPECT_EQ('`', name[3]);
}

TEST(SqlIdentifier, preserves_bytes_after_an_embedded_zero)
{
  FixedBufferAllocator allocator;
  const char name[] = {'a', '\0', '`', 'b'};
  const char expected[] = {'a', '\0', '`', '`', 'b'};
  const char *escaped = nullptr;
  int32_t length = 0;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, name, static_cast<int32_t>(sizeof(name)), escaped, length));
  ASSERT_EQ(static_cast<int32_t>(sizeof(expected)), length);
  EXPECT_EQ(0, std::memcmp(expected, escaped, sizeof(expected)));
  EXPECT_EQ('\0', escaped[length]);
}

TEST(SqlIdentifier, supports_reusing_input_variables_for_the_output)
{
  FixedBufferAllocator allocator;
  const char *name = "a`b";
  int32_t length = 3;
  ASSERT_EQ(OB_SUCCESS, escape_mysql_identifier(allocator, name, length, name, length));
  EXPECT_EQ("a``b", std::string(name, length));
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
