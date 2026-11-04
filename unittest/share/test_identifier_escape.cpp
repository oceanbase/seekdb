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
#include <gtest/gtest.h>
#include "lib/alloc/ob_iallocator.h"     // ObIAllocator
#include "lib/string/ob_string.h"        // ObString
#include "share/ob_identifier_escape.h"  // generate_escaped_identifier_name

namespace
{
using oceanbase::common::OB_ALLOCATE_MEMORY_FAILED;
using oceanbase::common::OB_SUCCESS;
using oceanbase::common::ObIAllocator;
using oceanbase::common::ObString;
using oceanbase::share::generate_escaped_identifier_name;

// Minimal ObIAllocator implementation so the header-only escaper can be
// exercised without pulling in Share runtime symbols.
class EscapeTestAllocator final : public ObIAllocator
{
public:
  EscapeTestAllocator() : fail_allocation_(false) {}
  virtual ~EscapeTestAllocator() override {}
  void set_fail_allocation(const bool fail) { fail_allocation_ = fail; }
  virtual void *alloc(const int64_t size) override
  {
    return fail_allocation_ ? nullptr : std::malloc(size > 0 ? size : 1);
  }
  virtual void *alloc(const int64_t size, const oceanbase::lib::ObMemAttr &attr) override
  {
    UNUSED(attr);
    return alloc(size);
  }
  virtual void free(void *ptr) override { std::free(ptr); }
private:
  bool fail_allocation_;
};

ObString make_string(const char *text)
{
  return ObString(static_cast<int64_t>(std::strlen(text)), text);
}
} // namespace

TEST(ShareIdentifierEscape, PlainNameIsReusedWithoutCopy)
{
  EscapeTestAllocator allocator;
  const ObString name = make_string("c1");
  ObString escaped;
  ASSERT_EQ(OB_SUCCESS, generate_escaped_identifier_name(allocator, name, escaped));
  EXPECT_EQ(0, escaped.compare(name));
  EXPECT_EQ(name.ptr(), escaped.ptr());
}

TEST(ShareIdentifierEscape, DoublesEveryBacktick)
{
  struct Case
  {
    const char *input;
    const char *expected;
  };
  const Case cases[] = {
      {"key`id", "key``id"},
      {"`leading", "``leading"},
      {"trailing`", "trailing``"},
      {"`a``b`", "``a````b``"},
      {"``", "````"},
  };
  EscapeTestAllocator allocator;
  for (const Case &test_case : cases) {
    ObString escaped;
    ASSERT_EQ(OB_SUCCESS, generate_escaped_identifier_name(
        allocator, make_string(test_case.input), escaped))
        << "input: " << test_case.input;
    EXPECT_EQ(0, escaped.compare(make_string(test_case.expected)))
        << "input: " << test_case.input;
  }
}

TEST(ShareIdentifierEscape, EmptyIdentifierYieldsEmptyResult)
{
  EscapeTestAllocator allocator;
  ObString default_constructed;
  ObString escaped;
  ASSERT_EQ(OB_SUCCESS, generate_escaped_identifier_name(allocator, default_constructed, escaped));
  EXPECT_TRUE(escaped.empty());
  const char *empty_text = "";
  const ObString zero_length(0, empty_text);
  ASSERT_EQ(OB_SUCCESS, generate_escaped_identifier_name(allocator, zero_length, escaped));
  EXPECT_TRUE(escaped.empty());
}

TEST(ShareIdentifierEscape, AllocationFailureIsReported)
{
  EscapeTestAllocator allocator;
  allocator.set_fail_allocation(true);
  ObString escaped = make_string("stale");
  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      generate_escaped_identifier_name(allocator, make_string("a`b"), escaped));
  EXPECT_TRUE(escaped.empty());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
