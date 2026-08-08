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

#include <cstring>
#include <limits>
#include <random>

#include "sql/engine/expr/ob_distribution.h"

using oceanbase::sql::ObDistribution;

namespace
{

uint64_t double_bits(double value)
{
  uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "unexpected double size");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

uint64_t int_bits(int64_t value)
{
  uint64_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "unexpected int64_t size");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

} // namespace

TEST(ObDistribution, uniform_int_matches_gcc12)
{
  std::mt19937_64 gen(3);
  EXPECT_EQ(-3, ObDistribution::uniform_int(gen, -10, 2));

  gen.seed(3);
  EXPECT_EQ(1, ObDistribution::uniform_int(gen, 0, 2));

  gen.seed(3);
  EXPECT_EQ(16, ObDistribution::uniform_int(gen, 10, 20));

  gen.seed(3);
  EXPECT_EQ(10, ObDistribution::uniform_int(gen, 10, 10));
}

TEST(ObDistribution, uniform_int_handles_full_int64_range)
{
  std::mt19937_64 actual_gen(42);
  std::mt19937_64 expected_gen(42);
  const int64_t value = ObDistribution::uniform_int(
      actual_gen, std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
  const uint64_t expected_bits = expected_gen()
                               + static_cast<uint64_t>(std::numeric_limits<int64_t>::min());
  EXPECT_EQ(expected_bits, int_bits(value));
}

TEST(ObDistribution, uniform_real_matches_gcc12)
{
  std::mt19937_64 gen(3);
  const double value = ObDistribution::uniform_real(gen, 3.1415, 20.0);
  EXPECT_EQ(UINT64_C(0x40291f7737ce087c), double_bits(value));
}

TEST(ObDistribution, normal_matches_gcc12)
{
  std::mt19937_64 gen(3);
  const double value = ObDistribution::normal(gen, 3.1415, 2.0);
  EXPECT_EQ(UINT64_C(0x3fdb2ffaeda08b88), double_bits(value));
}
