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

#define USING_LOG_PREFIX SHARE_PT

#include <gmock/gmock.h>
#define private public
#include "lib/allocator/page_arena.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::Invoke;

namespace oceanbase
{
namespace common
{

class TestObDList : public ::testing::Test
{
public:
  TestObDList() {}

  virtual void SetUp() {}
  virtual void TearDown(){}
};

struct TestNode: public common::ObDLinkBase<TestNode>
{
  int64_t value_;
  OB_UNIS_VERSION(1);
};

OB_SERIALIZE_MEMBER(TestNode, value_);

TEST_F(TestObDList, encode_decode)
{
  common::ObDList<TestNode> list;
  TestNode node1;
  TestNode node2;
  node1.value_ = 1;
  node2.value_ = 2;

  ASSERT_TRUE(list.add_last(&node1));
  ASSERT_TRUE(list.add_last(&node2));

  int64_t buf_size = get_dlist_serialize_size(list);
  char *buf = static_cast<char*>(ob_malloc(buf_size, ObNewModIds::TEST));
  int64_t buf_len = buf_size;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialize_dlist(list, buf, buf_len, pos));
  ASSERT_EQ(buf_size, pos);
  LOG_INFO("print", K(buf_size));


  common::ObArenaAllocator allocator;
  common::ObDList<TestNode> new_list;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, deserialize_dlist(new_list, allocator, buf, buf_len, pos));

  TestNode *first_node = new_list.get_first();
  TestNode *second_node = first_node->get_next();
  ASSERT_TRUE(NULL != first_node);
  ASSERT_TRUE(NULL != second_node);
  ASSERT_EQ(1, first_node->value_);
  ASSERT_EQ(2, second_node->value_);
}



} // end namespace share
} // end namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
