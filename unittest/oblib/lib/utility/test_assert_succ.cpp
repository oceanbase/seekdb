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

// Include first to check that the macro does not require a logging header.
#include "lib/utility/ob_macro_utils.h"
#include <gtest/gtest.h>
#include <csignal>
#include <cstdio>
#include <string>

using namespace oceanbase::common;

TEST(AssertSucc, evaluates_once_without_caller_ret)
{
  int calls = 0;
  OB_ASSERT_SUCC((++calls, OB_SUCCESS));
  EXPECT_EQ(1, calls);
}

TEST(AssertSucc, preserves_caller_variables)
{
  int ret = OB_TIMEOUT;
  int tmp_ret = OB_ALLOCATE_MEMORY_FAILED;
  int code = OB_SUCCESS;
  OB_ASSERT_SUCC(code);
  EXPECT_EQ(OB_TIMEOUT, ret);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, tmp_ret);
  EXPECT_EQ(OB_SUCCESS, code);
}

TEST(AssertSucc, behaves_as_one_statement)
{
  int calls = 0;
  if (false)
    OB_ASSERT_SUCC((++calls, OB_SUCCESS));
  else
    ++calls;
  EXPECT_EQ(1, calls);
  for (int i = 0; i < 3; ++i)
    OB_ASSERT_SUCC((++calls, OB_SUCCESS));
  EXPECT_EQ(4, calls);
}

TEST(AssertSucc, preserves_explicit_ret_assignment_and_short_circuit)
{
  int ret = OB_TIMEOUT;
  int calls = 0;
  auto succeed = [&calls]() { ++calls; return OB_SUCCESS; };
  if (OB_SUCC(ret)) {
    OB_ASSERT_SUCC(ret = succeed());
  }
  EXPECT_EQ(OB_TIMEOUT, ret);
  EXPECT_EQ(0, calls);

  // OB_FAIL assigns even when ret held an earlier error. A replacement that
  // needs this behavior must keep the assignment explicitly in the argument.
  OB_ASSERT_SUCC(ret = succeed());
  EXPECT_EQ(OB_SUCCESS, ret);
  EXPECT_EQ(1, calls);
}

#if GTEST_HAS_DEATH_TEST && !defined(_WIN32)
TEST(AssertSuccDeathTest, reports_expression_code_and_location)
{
  const std::string diagnostic = "OB_ASSERT_SUCC failed: expr=OB_ERR_UNEXPECTED, code=-4016, "
      "file=.*test_assert_succ.cpp, line=" + std::to_string(__LINE__ + 2);
  EXPECT_EXIT(
      OB_ASSERT_SUCC(OB_ERR_UNEXPECTED),
      ::testing::KilledBySignal(SIGABRT), diagnostic);
}

TEST(AssertSuccDeathTest, rejects_positive_nonzero_results)
{
  EXPECT_EXIT(OB_ASSERT_SUCC(1), ::testing::KilledBySignal(SIGABRT), "code=1");
}

TEST(AssertSuccDeathTest, terminates_when_diagnostics_cannot_be_written)
{
  EXPECT_EXIT({
    // A valid, read-only stream makes fprintf fail without using a closed FILE.
    if (nullptr == freopen("/dev/null", "r", stderr)) {
      _Exit(1);
    }
    OB_ASSERT_SUCC(OB_ERR_UNEXPECTED);
  }, ::testing::KilledBySignal(SIGABRT), "");
}
#endif
