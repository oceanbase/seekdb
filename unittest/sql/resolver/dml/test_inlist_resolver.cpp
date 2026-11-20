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

class TestInListRewriteThreshold : public ::testing::Test
{
protected:
  void make_list(const int64_t count, const int64_t cardinality)
  {
    nodes_.assign(count, ParseNode{});
    children_.resize(count);
    for (int64_t i = 0; i < count; ++i) {
      nodes_[i].type_ = T_INT;
      nodes_[i].value_ = (i * 17) % cardinality;
      children_[i] = &nodes_[i];
    }
    list_ = ParseNode{};
    list_.type_ = T_EXPR_LIST;
    list_.num_child_ = count;
    list_.children_ = children_.data();
  }

  bool should_rewrite(const int64_t column_cnt, const bool is_prepare_stmt,
                      const int64_t threshold)
  {
    bool enabled = false;
    EXPECT_EQ(OB_SUCCESS, ObInListResolver::check_inlist_rewrite_threshold(
        list_, column_cnt, is_prepare_stmt, threshold, enabled));
    return enabled;
  }

  ParseNode list_{};
  std::vector<ParseNode> nodes_;
  std::vector<ParseNode *> children_;
};

TEST_F(TestInListRewriteThreshold, duplicate_integer_list_stays_inline)
{
  make_list(3072, 3);
  EXPECT_FALSE(should_rewrite(1, false, 1000));
  EXPECT_EQ(3072, list_.num_child_);
  for (int64_t i = 0; i < list_.num_child_; ++i) {
    EXPECT_EQ(T_INT, nodes_[i].type_);
    EXPECT_EQ((i * 17) % 3, nodes_[i].value_);
    EXPECT_EQ(&nodes_[i], children_[i]);
  }
}

TEST_F(TestInListRewriteThreshold, distinct_count_controls_threshold)
{
  make_list(3072, 3072);
  EXPECT_TRUE(should_rewrite(1, false, 1000));
  make_list(1000, 1000);
  EXPECT_TRUE(should_rewrite(1, false, 1000));
  make_list(3072, 999);
  EXPECT_FALSE(should_rewrite(1, false, 1000));
}

TEST_F(TestInListRewriteThreshold, unsupported_shapes_keep_existing_decision)
{
  make_list(8, 1);
  nodes_.back().type_ = T_VARCHAR;
  EXPECT_TRUE(should_rewrite(1, false, 4));

  make_list(8, 1);
  EXPECT_TRUE(should_rewrite(2, false, 4));
  EXPECT_TRUE(should_rewrite(1, true, 4));
}

TEST_F(TestInListRewriteThreshold, threshold_one_preserves_forced_rewrite)
{
  make_list(8, 1);
  EXPECT_TRUE(should_rewrite(1, false, 1));
  EXPECT_FALSE(should_rewrite(1, false, 9));
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
