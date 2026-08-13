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
#define UNITTEST_DEBUG
#include <gtest/gtest.h>
#define private public
#define protected public
#include "share/cache/ob_recycle_multi_kvcache.h"
#include "recycle_buffer_test_types.h"

namespace oceanbase {
namespace unittest {

using namespace common;
using namespace std;
using namespace common::cache;

class TestObVtableRecycleEventBuffer: public ::testing::Test
{
public:
  TestObVtableRecycleEventBuffer() {}
  virtual ~TestObVtableRecycleEventBuffer() {}
  virtual void SetUp() {
  }
  virtual void TearDown() {
  }
public:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestObVtableRecycleEventBuffer);
};

TEST_F(TestObVtableRecycleEventBuffer, test_hash_bkt) {
  KVNode<HashKey, Simple> *stack_buffer[3];
  ObRecycleMultiKVCache<HashKey, Simple>::HashBkt hash_bkt(stack_buffer, 3, 0);
  auto node1_1 = new KVNode<HashKey, Simple>(1, 1);
  auto node1_2 = new KVNode<HashKey, Simple>(1, 2);
  auto node1_3 = new KVNode<HashKey, Simple>(1, 3);
  auto node2_1 = new KVNode<HashKey, Simple>(2, 1);
  auto node4_1 = new KVNode<HashKey, Simple>(4, 1);
  auto node4_2 = new KVNode<HashKey, Simple>(4, 2);
  auto node4_3 = new KVNode<HashKey, Simple>(4, 3);
  hash_bkt.insert(node1_1);
  ASSERT_EQ(node1_1, hash_bkt.hash_bkt_[1]);
  ASSERT_EQ(nullptr, hash_bkt.hash_bkt_[1]->hash_bkt_next_);
  ASSERT_EQ(node1_1, hash_bkt.hash_bkt_[1]->key_node_next_);
  ASSERT_EQ(node1_1, hash_bkt.hash_bkt_[1]->key_node_prev_);
  hash_bkt.insert(node2_1);
  ASSERT_EQ(node2_1, hash_bkt.hash_bkt_[2]);
  hash_bkt.insert(node4_1);
  ASSERT_EQ(node4_1, hash_bkt.hash_bkt_[1]->hash_bkt_next_);
  ASSERT_EQ(hash_bkt.hash_bkt_[1]->hash_bkt_next_, *node4_1->hash_bkt_prev_next_ptr_);
  ASSERT_EQ(nullptr, hash_bkt.hash_bkt_[1]->hash_bkt_next_->hash_bkt_next_);
  hash_bkt.insert(node1_2);
  ASSERT_EQ(node1_2, hash_bkt.hash_bkt_[1]->key_node_next_);
  ASSERT_EQ(node1_2, hash_bkt.hash_bkt_[1]->key_node_prev_);
  ASSERT_EQ(hash_bkt.hash_bkt_[1], node1_2->key_node_next_);
  ASSERT_EQ(hash_bkt.hash_bkt_[1], node1_2->key_node_prev_);
  hash_bkt.remove(node1_2);
  ASSERT_EQ(node1_1, hash_bkt.hash_bkt_[1]->key_node_next_);
  ASSERT_EQ(node1_1, hash_bkt.hash_bkt_[1]->key_node_prev_);
  hash_bkt.insert(node1_2);
  hash_bkt.remove(node1_1);
  ASSERT_EQ(node1_2, hash_bkt.hash_bkt_[1]);
  ASSERT_EQ(node1_2, hash_bkt.hash_bkt_[1]->key_node_next_);
  ASSERT_EQ(node1_2, hash_bkt.hash_bkt_[1]->key_node_prev_);
  ASSERT_EQ(node4_1, hash_bkt.hash_bkt_[1]->hash_bkt_next_);
  ASSERT_EQ(node1_2->hash_bkt_next_, *node4_1->hash_bkt_prev_next_ptr_);
  hash_bkt.remove(node1_2);
  ASSERT_EQ(node4_1, hash_bkt.hash_bkt_[1]);
  KVNode<HashKey, Simple> **bkt_prev_node_next_ptr;
  KVNode<HashKey, Simple> *bkt_next;
  auto list = hash_bkt.find_list(4, bkt_prev_node_next_ptr, bkt_next);
  ASSERT_EQ(Simple(1), list->v_);
  ASSERT_EQ(list, list->key_node_next_);
  hash_bkt.insert(node4_2);
  hash_bkt.insert(node4_3);
  list = hash_bkt.find_list(4, bkt_prev_node_next_ptr, bkt_next);
  ASSERT_EQ(Simple(1), list->v_);
  ASSERT_EQ(Simple(2), list->key_node_next_->v_);
  ASSERT_EQ(Simple(3), list->key_node_next_->key_node_next_->v_);
  ASSERT_EQ(list, list->key_node_next_->key_node_next_->key_node_next_);
  hash_bkt.remove(node4_2);
  hash_bkt.insert(node4_2);
  ASSERT_EQ(Simple(1), list->v_);
  ASSERT_EQ(Simple(3), list->key_node_next_->v_);
  ASSERT_EQ(Simple(2), list->key_node_next_->key_node_next_->v_);
  ASSERT_EQ(list, list->key_node_next_->key_node_next_->key_node_next_);
}

TEST_F(TestObVtableRecycleEventBuffer, test_recycle_buffer) {
  ObRecycleMultiKVCache<HashKey, Complicated> recycle_buffer;
  int64_t sizeof_event_node = sizeof(KVNode<HashKey, Complicated>);
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.init("TEST",
                                            DefaultAllocator::get_instance(),
                                            3 * (sizeof_event_node + 1),
                                            3));
  ASSERT_EQ(recycle_buffer.total_buffer_ + sizeof(void*) * 3, recycle_buffer.buffer_);
  ASSERT_EQ((char *)recycle_buffer.hash_bkt_.hash_bkt_, recycle_buffer.total_buffer_);
  ASSERT_EQ(recycle_buffer.round_end_(recycle_buffer.offset_next_to_appended_), recycle_buffer.offset_can_write_end_);
  auto value1 = Complicated({'1', 1});
  auto value2 = Complicated({'2', 1});
  auto value3 = Complicated({'3', 1});
  auto value4 = Complicated({'4', 1});
  auto value5 = Complicated({'5', 1});
  auto value6 = Complicated({'6', 2});
  auto value7 = Complicated({'7', 2});
  auto value8 = Complicated({'8', 1});
  auto value9 = Complicated({'9', 1});
  auto value10 = Complicated({'0', 1});
  auto value11 = Complicated({'1', 1_KB});
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value1));
  ASSERT_EQ(sizeof_event_node + 1, recycle_buffer.offset_next_to_appended_);
  ASSERT_EQ(recycle_buffer.round_end_(recycle_buffer.offset_next_to_appended_), recycle_buffer.offset_can_write_end_);
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value2));
  ASSERT_EQ(2 * (sizeof_event_node + 1), recycle_buffer.offset_next_to_appended_);
  ASSERT_EQ(recycle_buffer.round_end_(recycle_buffer.offset_next_to_appended_), recycle_buffer.offset_can_write_end_);
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value3));
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value4));
  ASSERT_EQ(4 * (sizeof_event_node + 1), recycle_buffer.offset_next_to_appended_);
  ASSERT_EQ(recycle_buffer.buffer_len_ + (sizeof_event_node + 1), recycle_buffer.offset_can_write_end_);
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value5));
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value6));
  ASSERT_EQ(recycle_buffer.buffer_len_ * 2 + (sizeof_event_node + 2), recycle_buffer.offset_next_to_appended_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 2 + 2 * (sizeof_event_node + 1), recycle_buffer.offset_reserved_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 3, recycle_buffer.offset_can_write_end_);
  int idx = 0;
  int ret = recycle_buffer.for_each(HashKey(1), [&](const Complicated &vale) -> int {
    int ret = OB_SUCCESS;
    switch (++idx) {
    case 1:
      if (value6 != vale) {
        ret = OB_ERR_UNEXPECTED;
        OCCAM_LOG(ERROR, "not expected", K(vale));
      }
      break;
    default:
      OB_ASSERT(false);
      break;
    }
    return ret;
  });
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value7));
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value8));
  ASSERT_EQ(recycle_buffer.buffer_len_ * 3 + (sizeof_event_node + 1), recycle_buffer.offset_next_to_appended_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 3 + 2 * (sizeof_event_node + 2), recycle_buffer.offset_reserved_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 3 + sizeof_event_node + 2, recycle_buffer.offset_can_write_end_);
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value9));
  ASSERT_EQ(recycle_buffer.buffer_len_ * 3 + 2 * (sizeof_event_node + 1), recycle_buffer.offset_next_to_appended_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 3 + 2 * (sizeof_event_node + 2), recycle_buffer.offset_reserved_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 4, recycle_buffer.offset_can_write_end_);
  ASSERT_EQ(OB_SUCCESS, recycle_buffer.append({1}, value10));
  ASSERT_EQ(recycle_buffer.buffer_len_ * 4, recycle_buffer.offset_next_to_appended_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 3 + 2 * (sizeof_event_node + 2), recycle_buffer.offset_reserved_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 4, recycle_buffer.offset_can_write_end_);
  idx = 0;
  ret = recycle_buffer.for_each(HashKey(1), [&](const Complicated &vale) -> int {
    int ret = OB_SUCCESS;
    switch (++idx) {
    case 1:
      if (value8 != vale) {
        ret = OB_ERR_UNEXPECTED;
        OCCAM_LOG(ERROR, "not expected", K(vale));
      }
      break;
    case 2:
      if (value9 != vale) {
        ret = OB_ERR_UNEXPECTED;
        OCCAM_LOG(ERROR, "not expected", K(vale));
      }
      break;
    case 3:
      if (value10 != vale) {
        ret = OB_ERR_UNEXPECTED;
        OCCAM_LOG(ERROR, "not expected", K(vale));
      }
      break;
    default:
      OB_ASSERT(false);
      break;
    }
    return ret;
  });
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(OB_BUF_NOT_ENOUGH, recycle_buffer.append({11}, value11));
  ASSERT_EQ(recycle_buffer.buffer_len_ * 4, recycle_buffer.offset_next_to_appended_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 3 + 2 * (sizeof_event_node + 2), recycle_buffer.offset_reserved_);
  ASSERT_EQ(recycle_buffer.buffer_len_ * 5, recycle_buffer.offset_can_write_end_);
}

}
}
