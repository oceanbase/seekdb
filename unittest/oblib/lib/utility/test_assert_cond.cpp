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

// Include first to check that the macro needs no logging or error-code header.
#include "lib/utility/ob_macro_utils.h"
#include "lib/ob_errno.h"
#include <gtest/gtest.h>
#include <csignal>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>

using namespace oceanbase::common;

namespace
{
template <typename T, typename = void>
struct AcceptsCondition : std::false_type {};

template <typename T>
struct AcceptsCondition<T, decltype(assert_cond(std::declval<T>(), "", "", 0), void())>
    : std::true_type {};

static_assert(AcceptsCondition<bool>::value, "boolean conditions must be accepted");
static_assert(!AcceptsCondition<int>::value, "error codes must be rejected");
static_assert(!AcceptsCondition<void *>::value, "pointers need an explicit null comparison");
}

TEST(AssertCond, evaluates_once_without_caller_ret)
{
  int calls = 0;
  ASSERT_COND(++calls == 1);
  EXPECT_EQ(1, calls);
}

TEST(AssertCond, preserves_caller_errors)
{
  int ret = OB_TIMEOUT;
  int tmp_ret = OB_ALLOCATE_MEMORY_FAILED;
  ASSERT_COND(ret == OB_TIMEOUT);
  EXPECT_EQ(OB_TIMEOUT, ret);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, tmp_ret);
}

TEST(AssertCond, behaves_as_one_statement)
{
  int calls = 0;
  if (false)
    ASSERT_COND(++calls == 1);
  else
    ++calls;
  for (int i = 0; i < 3; ++i)
    ASSERT_COND(++calls > 1);
  EXPECT_EQ(4, calls);
}

TEST(AssertCond, preserves_condition_short_circuit)
{
  int *ptr = nullptr;
  int calls = 0;
  ASSERT_COND(ptr == nullptr || (++calls, *ptr == 0));
  EXPECT_EQ(0, calls);
  if (false) {
    ASSERT_COND(false);
  }
}

#if GTEST_HAS_DEATH_TEST && !defined(_WIN32)
TEST(AssertCondDeathTest, reports_condition_and_location)
{
  const std::string diagnostic = "ASSERT_COND failed: condition=false, "
      "file=.*test_assert_cond.cpp, line=" + std::to_string(__LINE__ + 2);
  EXPECT_EXIT(
      ASSERT_COND(false),
      ::testing::KilledBySignal(SIGABRT), diagnostic);
}

TEST(AssertCondDeathTest, stops_before_dereferencing_null)
{
  int *ptr = nullptr;
  EXPECT_EXIT(ASSERT_COND(ptr != nullptr && *ptr == 0),
              ::testing::KilledBySignal(SIGABRT), "ASSERT_COND failed");
}

TEST(AssertCondDeathTest, terminates_when_diagnostics_cannot_be_written)
{
  EXPECT_EXIT({
    if (nullptr == freopen("/dev/null", "r", stderr)) {
      _Exit(1);
    }
    ASSERT_COND(false);
  }, ::testing::KilledBySignal(SIGABRT), "");
}
#endif
