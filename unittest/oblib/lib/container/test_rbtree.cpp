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

#include "gtest/gtest.h"
#include "lib/container/ob_rbtree.h"

namespace oceanbase
{
namespace container
{

struct TestRbTreeNode
{
  RBNODE(TestRbTreeNode, rblink);
  int key_;

  int compare(const TestRbTreeNode *other) const
  {
    return (key_ > other->key_) - (key_ < other->key_);
  }
};

TEST(TestRbTree, Empty)
{
  ObRbTree<TestRbTreeNode, ObDummyCompHelper<TestRbTreeNode>> tree;
  TestRbTreeNode key;
  TestRbTreeNode *result = nullptr;
  key.key_ = 0;

  tree.init_tree();
  EXPECT_TRUE(tree.is_empty());
  EXPECT_EQ(nullptr, tree.get_first());
  EXPECT_EQ(nullptr, tree.get_last());
  EXPECT_EQ(OB_SUCCESS, tree.search(&key, result));
  EXPECT_EQ(nullptr, result);
}

} // namespace container
} // namespace oceanbase
