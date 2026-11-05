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

#pragma once

#include <gtest/gtest.h>
#include "src/share/ob_rs_timeout_policy.h"

using oceanbase::share::get_rs_default_timeout_us;

TEST(RsTimeoutPolicy, short_rpc_timeout)
{
#if defined(__APPLE__) || defined(_WIN32)
  EXPECT_EQ(10000000LL, get_rs_default_timeout_us(2000000));
  EXPECT_EQ(10000000LL, get_rs_default_timeout_us(1));
#else
  EXPECT_EQ(2000000, get_rs_default_timeout_us(2000000));
  EXPECT_EQ(1, get_rs_default_timeout_us(1));
#endif
}

TEST(RsTimeoutPolicy, preserves_larger_configured_timeout)
{
  EXPECT_EQ(10000000, get_rs_default_timeout_us(10000000));
  EXPECT_EQ(30000000, get_rs_default_timeout_us(30000000));
  EXPECT_EQ(INT64_MAX, get_rs_default_timeout_us(INT64_MAX));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
