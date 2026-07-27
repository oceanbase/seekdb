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

#include "lib/ob_name_id_def.h"
#include "lib/json/ob_yson.h"
#include "storage/tx/ob_trans_define.h"
#include <gtest/gtest.h>
namespace oceanbase
{
using namespace transaction;
namespace unittest
{
struct TestUndoAction : public ::testing::Test
{
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestUndoAction, tx_seq_current_format)
{
  const ObTxSEQ seq(42, 7);
  const uint64_t expected_raw = (1ULL << 62) | (42ULL << 15) | 7;
  EXPECT_TRUE(seq.is_valid());
  EXPECT_EQ(42, seq.get_seq());
  EXPECT_EQ(7, seq.get_branch());
  EXPECT_EQ(expected_raw, seq.cast_to_int());
  EXPECT_EQ(seq, ObTxSEQ::cast_from_int(seq.cast_to_int()));

  EXPECT_FALSE(ObTxSEQ::INVL().is_valid());
  EXPECT_TRUE(ObTxSEQ::MIN_VAL().is_min());
  EXPECT_EQ(1, ObTxSEQ::MIN_VAL().get_seq());
  EXPECT_EQ(0, ObTxSEQ::MIN_VAL().get_branch());
  EXPECT_TRUE(ObTxSEQ::MAX_VAL().is_max());
  EXPECT_EQ(static_cast<uint64_t>(INT64_MAX), ObTxSEQ::MAX_VAL().cast_to_int());
  EXPECT_EQ((1LL << 47) - 1, ObTxSEQ::MAX_VAL().get_seq());
  EXPECT_EQ(INT16_MAX, ObTxSEQ::MAX_VAL().get_branch());

  ObTxSEQ atomic_seq;
  EXPECT_EQ(static_cast<int64_t>(seq.cast_to_int()), atomic_seq.inc_update(seq));
  EXPECT_EQ(seq, atomic_seq.atomic_load());
  atomic_seq.atomic_store(ObTxSEQ::MIN_VAL());
  EXPECT_TRUE(atomic_seq.atomic_load().is_min());
  atomic_seq.atomic_reset();
  EXPECT_EQ(ObTxSEQ::INVL(), atomic_seq.atomic_load());

  char buf[32] = {};
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, seq.serialize(buf, sizeof(buf), pos));
  ObTxSEQ decoded;
  int64_t decode_pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(buf, pos, decode_pos));
  EXPECT_EQ(seq, decoded);

  pos = 0;
  ASSERT_EQ(OB_SUCCESS, common::serialization::encode_vi64(buf, sizeof(buf), pos, 42));
  decode_pos = 0;
  EXPECT_EQ(OB_VERSION_NOT_MATCH, decoded.deserialize(buf, pos, decode_pos));
}

TEST_F(TestUndoAction, valid)
{
  ObUndoAction a1(ObTxSEQ(100, 0), ObTxSEQ(1, 1));
  EXPECT_FALSE(a1.is_valid());
  ObUndoAction a2(ObTxSEQ(100, 1), ObTxSEQ(100, 1));
  EXPECT_FALSE(a2.is_valid());
  ObUndoAction a3(ObTxSEQ(100, 0), ObTxSEQ(100, 0));
  EXPECT_FALSE(a3.is_valid());
  ObUndoAction a4(ObTxSEQ(100, 0), ObTxSEQ(100, 1));
  EXPECT_FALSE(a4.is_valid());
  ObUndoAction a5(ObTxSEQ(100, 1), ObTxSEQ(100, 0));
  EXPECT_FALSE(a5.is_valid());
  ObUndoAction a6(ObTxSEQ(100, 1), ObTxSEQ(1, 0));
  EXPECT_FALSE(a6.is_valid());
  ObUndoAction a7(ObTxSEQ(100, 1), ObTxSEQ(1, 1));
  EXPECT_TRUE(a7.is_valid());
  ObUndoAction a8(ObTxSEQ(100, 0), ObTxSEQ(1, 0));
  EXPECT_TRUE(a8.is_valid());
}
TEST_F(TestUndoAction, contain)
{
  ObUndoAction a1(ObTxSEQ(100,1), ObTxSEQ(1, 1));
  ObUndoAction a2(ObTxSEQ(99,1), ObTxSEQ(1, 1));
  EXPECT_TRUE(a1.is_contain(a2));
  EXPECT_FALSE(a2.is_contain(a1));
  ObUndoAction a3(ObTxSEQ(100,0), ObTxSEQ(1, 0));
  ObUndoAction a4(ObTxSEQ(99,0), ObTxSEQ(1, 0));
  EXPECT_TRUE(a3.is_contain(a4));
  EXPECT_FALSE(a4.is_contain(a3));
  ObUndoAction a5(ObTxSEQ(100,2), ObTxSEQ(1, 2));
  EXPECT_FALSE(a5.is_contain(a1));
  EXPECT_FALSE(a5.is_contain(a2));
  EXPECT_FALSE(a1.is_contain(a5));
  EXPECT_TRUE(a3.is_contain(a5));
  EXPECT_FALSE(a4.is_contain(a5));
}

TEST_F(TestUndoAction, contain_point)
{
  ObUndoAction a1(ObTxSEQ(100,1), ObTxSEQ(1, 1));
  ObUndoAction a3(ObTxSEQ(100,0), ObTxSEQ(1, 0));
  EXPECT_TRUE(a3.is_contain(ObTxSEQ(50, 1)));
  EXPECT_TRUE(a1.is_contain(ObTxSEQ(50, 1)));
  EXPECT_FALSE(a1.is_contain(ObTxSEQ(50, 0)));
  EXPECT_FALSE(a1.is_contain(ObTxSEQ(50, 2)));
  EXPECT_TRUE(a3.is_contain(ObTxSEQ(50, 0)));
}
} // unittest
} //oceanbase
using namespace oceanbase;
using namespace transaction;
int main(int argc, char **argv)
{
  int ret = 1;
  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_undo_action.log", true);
  logger.set_log_level(OB_LOG_LEVEL_INFO);
  testing::InitGoogleTest(&argc, argv);
  ret = RUN_ALL_TESTS();
  return ret;
}
