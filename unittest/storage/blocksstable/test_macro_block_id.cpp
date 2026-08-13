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
#define protected public
#define private public
#include "storage/blocksstable/ob_object_manager.h"
#undef protected
#undef private


namespace oceanbase
{
using namespace common;
using namespace blocksstable;

namespace unittest
{
class TestMacroBlockId : public ::testing::Test
{
public:
  TestMacroBlockId() = default;
  void SetUp() {}
  void TearDown() {}
  static void SetUpTestCase() {}
  static void TearDownTestCase() {}
};

TEST_F(TestMacroBlockId, local_mode)
{
  int ret = OB_SUCCESS;
  MacroBlockId m_local(77, (1L << 33), 0);
  OB_LOG(INFO, "local", K(m_local));
  OB_LOG(INFO, "raw", K(m_local.write_seq_), K(m_local.second_id_));
  ASSERT_EQ(77, m_local.write_seq_);
  ASSERT_EQ((1L << 33), m_local.block_index_);
  ASSERT_EQ(0, m_local.third_id_);

  const int64_t buf_len = 24;
  char buf[buf_len] = {0};

  int64_t pos = 0;
  ret = m_local.serialize(buf, buf_len, pos);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_TRUE(m_local.get_serialize_size() == pos);

  MacroBlockId m_local_des;
  pos = 0;
  ret = m_local_des.deserialize(buf, buf_len, pos);
  ASSERT_EQ(OB_SUCCESS, ret);
  OB_LOG(INFO, "local", K(m_local_des));
  OB_LOG(INFO, "raw", K(m_local_des.write_seq_), K(m_local_des.second_id_));
  ASSERT_TRUE(m_local.get_serialize_size() == pos);

  ASSERT_EQ(m_local.write_seq_, m_local_des.write_seq_);
  ASSERT_EQ((1L << 33), m_local_des.block_index_);
  ASSERT_EQ(0, m_local_des.third_id_);
}

TEST_F(TestMacroBlockId, verification)
{
  int ret = OB_SUCCESS;
  MacroBlockId test_id(0, -3, 0);
  ASSERT_FALSE(test_id.is_valid());
  test_id.block_index_ = -2;
  ASSERT_FALSE(test_id.is_valid());
  test_id.block_index_ = MacroBlockId::AUTONOMIC_BLOCK_INDEX;
  ASSERT_TRUE(test_id.is_valid());

  test_id.third_id_ = 1;
  ASSERT_TRUE(test_id.is_valid());
  test_id.third_id_ = -1;
  ASSERT_FALSE(test_id.is_valid());

  test_id.third_id_ = 0;
  const int64_t buf_len = 24;
  char buf[buf_len] = {0};
  int64_t pos = 0;
  ret = test_id.serialize(buf, 23, pos);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ASSERT_EQ(0, pos);

  ret = test_id.deserialize(buf, 23, pos);
  ASSERT_EQ(OB_DESERIALIZE_ERROR, ret);
  ASSERT_EQ(0, pos);
}

}
}
