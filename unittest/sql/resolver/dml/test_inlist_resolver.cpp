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
#include <vector>
#include "sql/resolver/dml/ob_inlist_resolver.h"

namespace oceanbase
{
namespace sql
{
using namespace common;

class TestInListThreshold : public ::testing::Test
{
protected:
  void make_list(const int64_t count, const int64_t cardinality, const bool parameters = false)
  {
    nodes_.assign(count, ParseNode{});
    children_.resize(count);
    for (int64_t i = 0; i < count; ++i) {
      nodes_[i].type_ = parameters ? T_QUESTIONMARK : T_INT;
      nodes_[i].value_ = parameters ? i : (i * 17) % cardinality;
      children_[i] = &nodes_[i];
    }
    list_ = ParseNode{};
    list_.type_ = T_EXPR_LIST;
    list_.num_child_ = count;
    list_.children_ = children_.data();
  }

  bool rewrite(const int64_t threshold, const ParamStore *params = nullptr, const bool prepare = false)
  {
    bool enabled = true;
    EXPECT_EQ(OB_SUCCESS, ObInListResolver::check_inlist_rewrite_threshold(
        list_, params, prepare, threshold, enabled));
    return enabled;
  }

  ParseNode list_{};
  std::vector<ParseNode> nodes_;
  std::vector<ParseNode *> children_;
};

TEST_F(TestInListThreshold, duplicate_membership_stays_inline)
{
  make_list(3072, 3);
  EXPECT_FALSE(rewrite(1000));
  ASSERT_EQ(3072, list_.num_child_);
  for (int64_t i = 0; i < list_.num_child_; ++i) {
    EXPECT_EQ(T_INT, nodes_[i].type_);
    EXPECT_EQ((i * 17) % 3, nodes_[i].value_);
    EXPECT_EQ(&nodes_[i], children_[i]);
  }
}

TEST_F(TestInListThreshold, distinct_membership_and_boundary_still_rewrite)
{
  make_list(3072, 3072);
  EXPECT_TRUE(rewrite(1000));
  make_list(1000, 1000);
  EXPECT_TRUE(rewrite(1000));
  EXPECT_FALSE(rewrite(1001));
  make_list(3072, 999);
  EXPECT_FALSE(rewrite(1000));
}

TEST_F(TestInListThreshold, forced_rewrite_is_preserved)
{
  make_list(8, 1);
  EXPECT_TRUE(rewrite(0));
  EXPECT_TRUE(rewrite(1));
  EXPECT_FALSE(rewrite(2));
}

TEST_F(TestInListThreshold, signed_integer_boundaries_remain_distinct)
{
  make_list(8, 1);
  nodes_[0].value_ = INT64_MIN;
  nodes_[1].value_ = INT64_MAX;
  EXPECT_FALSE(rewrite(4));
  EXPECT_TRUE(rewrite(3));
}

TEST_F(TestInListThreshold, unsupported_comparisons_keep_existing_decision)
{
  for (const auto type : {T_NULL, T_VARCHAR, T_UINT64, T_NUMBER, T_OP_ROW, T_OP_NEG}) {
    make_list(8, 1);
    nodes_.back().type_ = type;
    EXPECT_TRUE(rewrite(4)) << type;
  }
  make_list(8, 1);
  children_.back() = nullptr;
  EXPECT_TRUE(rewrite(4));
}

TEST_F(TestInListThreshold, known_parameters_do_not_lose_slots_or_values)
{
  make_list(3072, 3, true);
  ObArenaAllocator allocator;
  ParamStore params{ObWrapperAllocator(allocator)};
  for (int64_t i = 0; i < 3072; ++i) {
    ObObjParam value;
    value.set_int(i % 3);
    ASSERT_EQ(OB_SUCCESS, params.push_back(value));
  }
  EXPECT_FALSE(rewrite(1000, &params));
  for (int64_t i = 0; i < 3072; ++i) {
    EXPECT_EQ(i, nodes_[i].value_);
    EXPECT_EQ(i % 3, params.at(i).get_int());
    params.at(i).set_int(i);
  }
  EXPECT_TRUE(rewrite(1000, &params));
}

TEST_F(TestInListThreshold, unknown_prepare_values_and_invalid_indices_are_conservative)
{
  make_list(8, 1, true);
  EXPECT_TRUE(rewrite(4));
  EXPECT_TRUE(rewrite(4, nullptr, true));
  ObArenaAllocator allocator;
  ParamStore params{ObWrapperAllocator(allocator)};
  for (int64_t i = 0; i < 8; ++i) {
    ObObjParam value;
    value.set_int(1);
    ASSERT_EQ(OB_SUCCESS, params.push_back(value));
  }
  EXPECT_TRUE(rewrite(4, &params, true));
  nodes_.back().value_ = -1;
  EXPECT_TRUE(rewrite(4, &params));
  nodes_.back().value_ = 8;
  EXPECT_TRUE(rewrite(4, &params));
  nodes_.back().value_ = 7;
  params.at(7).set_null();
  EXPECT_TRUE(rewrite(4, &params));
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
