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
#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>
#include "sql/resolver/dml/inlist_cardinality.h"

namespace oceanbase
{
namespace sql
{
namespace
{
bool check(const std::vector<int64_t> &values, const int64_t threshold)
{
  return has_small_integer_inlist(values.size(), threshold,
      [&](int64_t i, int64_t &value) { value = values[i]; return true; });
}

TEST(InListCardinality, ShuffledDuplicateMembership)
{
  std::vector<int64_t> values;
  for (int i = 0; i < 1024; ++i) {
    values.insert(values.end(), {1, 2, 3});
  }
  std::mt19937 generator(1024);
  std::shuffle(values.begin(), values.end(), generator);
  const auto original = values;
  EXPECT_TRUE(check(values, 1000));
  EXPECT_EQ(original, values);
}

TEST(InListCardinality, HonorThresholdBoundary)
{
  EXPECT_FALSE(check({}, 4));
  EXPECT_FALSE(check({1, 1, 1}, 4));
  EXPECT_TRUE(check({1, 2, 3, 1}, 4));
  EXPECT_FALSE(check({1, 2, 3, 4}, 4));
  EXPECT_FALSE(check({1, 1, 1}, 1));
  EXPECT_FALSE(check({1, 1, 1}, 0));
  EXPECT_FALSE(check({1, 1, 1}, -1));
}

TEST(InListCardinality, SignedIntegerBoundaries)
{
  const auto lo = std::numeric_limits<int64_t>::min();
  const auto hi = std::numeric_limits<int64_t>::max();
  EXPECT_TRUE(check({lo, hi, 0, -1, lo, hi, 0, -1}, 8));
  EXPECT_FALSE(check({lo, hi, 0, -1}, 4));
}

TEST(InListCardinality, UnsupportedValueRetainsRewrite)
{
  for (int64_t unsupported : {0, 1536, 3071}) {
    EXPECT_FALSE(has_small_integer_inlist(3072, 1000,
        [&](int64_t i, int64_t &value) { value = i % 3; return i != unsupported; }));
  }
}

TEST(InListCardinality, BoundedWorkForManyDistinctValues)
{
  int64_t reads = 0;
  EXPECT_FALSE(has_small_integer_inlist(1000000, 1000,
      [&](int64_t i, int64_t &value) { ++reads; value = i; return true; }));
  EXPECT_EQ(65, reads);
  EXPECT_TRUE(has_small_integer_inlist(3072, 1000,
      [](int64_t i, int64_t &value) { value = i % 64; return true; }));
}

TEST(InListCardinality, StopAtRewriteThreshold)
{
  int64_t reads = 0;
  EXPECT_FALSE(has_small_integer_inlist(3072, 4,
      [&](int64_t i, int64_t &value) { ++reads; value = i; return true; }));
  EXPECT_EQ(4, reads);
}

TEST(InListCardinality, ChangingBindingsDoesNotMutateInputs)
{
  std::vector<int64_t> bindings(3072, 1);
  EXPECT_TRUE(check(bindings, 1000));
  for (int64_t i = 0; i < static_cast<int64_t>(bindings.size()); ++i) {
    bindings[i] = i;
  }
  const auto original = bindings;
  EXPECT_FALSE(check(bindings, 1000));
  EXPECT_EQ(original, bindings);
}
}  // namespace
}  // namespace sql
}  // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
