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
#define private public
#include "storage/tx/ob_tx_log.h"
#undef private

void ob_abort (void) __THROW {}
namespace oceanbase
{
using namespace common;
using namespace transaction;
using namespace storage;
using namespace share;

namespace unittest
{
class TestObTxLog : public ::testing::Test
{
public:
  virtual void SetUp() {}
  virtual void TearDown() {}
public:
};

//const TEST
TxID TEST_TX_ID = 1024;
common::ObString TEST_TRACE_ID_STR("trace_id_test");
bool TEST_CAN_ELR =  false;
common::ObString TEST_TRCE_INFO("trace_info_test");
LogOffSet TEST_LOG_OFFSET(10);
int64_t TEST_COMMIT_VERSION = 190878;
int64_t TEST_CHECKSUM = 29890209;
ObArray<uint8_t> TEST_CHECKSUM_SIGNATURE_ARRAY;
int64_t TEST_LOG_ENTRY_NO = 1233;
ObTxPrevLogType TEST_PREV_LOG_TYPE(ObTxPrevLogType::TypeEnum::COMMIT_INFO);


// test ObTxLogBlockHeader
TEST_F(TestObTxLog, tx_log_block_header)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());
  int64_t pos = 0;
  ObTxLogBlock fill_block, replay_block;

  ObTxLogBlockHeader &fill_block_header =  fill_block.get_header();
  fill_block_header.init(TEST_LOG_ENTRY_NO, ObTransID(TEST_TX_ID));
  fill_block_header.set_serial_final();
  ASSERT_TRUE(fill_block_header.is_serial_final());
  ASSERT_EQ(OB_SUCCESS, fill_block.init_for_fill());
  fill_block.seal(TEST_TX_ID);
  // check log_block_header
  char *buf = fill_block.get_buf();
  logservice::ObLogBaseHeader base_header_1;


  pos = 0;
  base_header_1.deserialize(buf, base_header_1.get_serialize_size(),  pos);
  EXPECT_EQ(base_header_1.get_log_type() , ObTxLogBlock::DEFAULT_LOG_BLOCK_TYPE);
  EXPECT_EQ(base_header_1.get_replay_hint(), TEST_TX_ID);

  ObTxLogBlockHeader &replay_block_header = replay_block.get_header();
  ASSERT_EQ(OB_SUCCESS, replay_block.init_for_replay(buf, fill_block.get_size()));
  EXPECT_EQ(replay_block.get_log_base_header().get_replay_hint(), TEST_TX_ID);
  EXPECT_EQ(TEST_LOG_ENTRY_NO, replay_block_header.get_log_entry_no());
  EXPECT_EQ(fill_block_header.flags(), replay_block_header.flags());
  EXPECT_TRUE(replay_block_header.is_serial_final());

  // reuse
  fill_block.get_header().init(TEST_LOG_ENTRY_NO + 1, ObTransID(TEST_TX_ID + 1));
  fill_block.reuse_for_fill();
  fill_block.seal(TEST_TX_ID + 1);
  buf = fill_block.get_buf();
  pos = 0;

  logservice::ObLogBaseHeader base_header_2;
  base_header_2.deserialize(buf, base_header_2.get_serialize_size(),  pos);
  EXPECT_EQ(base_header_2.get_log_type() , ObTxLogBlock::DEFAULT_LOG_BLOCK_TYPE);
  EXPECT_EQ(base_header_2.get_replay_hint(), TEST_TX_ID + 1);
  ObTxLogBlock replay_block2;
  ObTxLogBlockHeader &replay_block_header2 = replay_block2.get_header();
  ASSERT_EQ(OB_SUCCESS, replay_block2.init_for_replay(buf, fill_block.get_size(), pos));
  EXPECT_EQ(TEST_LOG_ENTRY_NO + 1, replay_block_header2.get_log_entry_no());
  EXPECT_EQ(fill_block_header.flags(), replay_block_header2.flags());
  EXPECT_FALSE(replay_block_header2.is_serial_final());
}

TEST_F(TestObTxLog, tx_log_body_except_redo)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());
  ObTxLogBlock fill_block;
  ObTxLogBlock replay_block;

  ObRedoLSNArray TEST_LOG_OFFSET_ARRY;
  TEST_LOG_OFFSET_ARRY.push_back(TEST_LOG_OFFSET);
  ObTxBufferNodeArray TEST_TX_BUFFER_NODE_ARRAY;
  ObString str("TEST CASE");
  ObTxBufferNode node;
  node.init(ObTxDataSourceType::LS_TABLE, str, share::SCN(), transaction::ObTxSEQ(100, 0), nullptr);
  TEST_TX_BUFFER_NODE_ARRAY.push_back(node);

  ObTxCommitInfoLog fill_commit_state(TEST_CAN_ELR,
                                       TEST_TRACE_ID_STR,
                                       TEST_LOG_OFFSET,
                                       TEST_LOG_OFFSET_ARRY);
  ObTxCommitLog fill_commit(share::SCN::base_scn(),
                            TEST_CHECKSUM,
                            TEST_CHECKSUM_SIGNATURE_ARRAY,
                            TEST_TX_BUFFER_NODE_ARRAY,
                            TEST_LOG_OFFSET,
                            TEST_PREV_LOG_TYPE);
  ObTxClearLog fill_clear;
  ObTxAbortLog fill_abort(TEST_TX_BUFFER_NODE_ARRAY);
  ObTxRecordLog fill_record(TEST_LOG_OFFSET, TEST_LOG_OFFSET_ARRY);

  ObTxLogBlockHeader &header = fill_block.get_header();
  header.init(TEST_LOG_ENTRY_NO, ObTransID(TEST_TX_ID));
  ASSERT_EQ(OB_SUCCESS, fill_block.init_for_fill());
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit_state));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_clear));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_abort));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_record));
  fill_block.seal(TEST_TX_ID);
  ObTxLogHeader tx_log_header;
  ASSERT_EQ(OB_SUCCESS, replay_block.init_for_replay(fill_block.get_buf(), fill_block.get_size()));

  ObTxCommitInfoLogTempRef commit_state_temp_ref;
  ObTxCommitInfoLog replay_commit_state(commit_state_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_INFO_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_commit_state));

  ObTxCommitLogTempRef commit_temp_ref;
  ObTxCommitLog replay_commit(commit_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_commit));
  EXPECT_EQ(TEST_PREV_LOG_TYPE.prev_log_type_, replay_commit.get_prev_log_type().prev_log_type_);

  ObTxClearLogTempRef clear_temp_ref;
  ObTxClearLog replay_clear(clear_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_CLEAR_LOG, tx_log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_clear));

  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_ABORT_LOG, tx_log_header.get_tx_log_type());

  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(tx_log_header));
  EXPECT_EQ(ObTxLogType::TX_RECORD_LOG, tx_log_header.get_tx_log_type());

  ASSERT_EQ(OB_ITER_END, replay_block.get_next_log(tx_log_header)); // ITER_END
}

TEST_F(TestObTxLog, tx_log_body_redo)
{
  TRANS_LOG(INFO, "called", "func", test_info_->name());
  ObTxLogBlock fill_block;
  ObTxLogBlock replay_block;
  ObTxLogBlock replay_block_2;

  ObRedoLSNArray TEST_LOG_OFFSET_ARRY;
  TEST_LOG_OFFSET_ARRY.push_back(TEST_LOG_OFFSET);
  ObTxBufferNodeArray TEST_TX_BUFFER_NODE_ARRAY;
  ObString str("TEST CASE");
  ObTxBufferNode node;
  node.init(ObTxDataSourceType::LS_TABLE, str, share::SCN(), transaction::ObTxSEQ(100, 0), nullptr);
  TEST_TX_BUFFER_NODE_ARRAY.push_back(node);

  ObTxCommitInfoLog fill_commit_state(TEST_CAN_ELR,
                                       TEST_TRACE_ID_STR,
                                       TEST_LOG_OFFSET,
                                       TEST_LOG_OFFSET_ARRY);
  ObTxCommitLog fill_commit(share::SCN::base_scn(),
                            TEST_CHECKSUM,
                            TEST_CHECKSUM_SIGNATURE_ARRAY,
                            TEST_TX_BUFFER_NODE_ARRAY,
                            TEST_LOG_OFFSET,
                            TEST_PREV_LOG_TYPE);

  ObTxLogBlockHeader &fill_block_header = fill_block.get_header();
  fill_block_header.init(TEST_LOG_ENTRY_NO, ObTransID(TEST_TX_ID));
  ASSERT_EQ(OB_SUCCESS, fill_block.init_for_fill());

  ObString TEST_MUTATOR_BUF("FFF");
  int64_t mutator_pos = 0;
  ObTxRedoLog fill_redo;
  ASSERT_EQ(OB_SUCCESS, fill_block.prepare_mutator_buf(fill_redo));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(fill_redo.get_mutator_buf(),
                                              fill_redo.get_mutator_size(),
                                              mutator_pos,
                                              TEST_MUTATOR_BUF));
  ASSERT_EQ(OB_SUCCESS, fill_block.finish_mutator_buf(fill_redo, mutator_pos));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit_state));
  ASSERT_EQ(OB_SUCCESS, fill_block.add_new_log(fill_commit));
  fill_block.seal(TEST_TX_ID);
  mutator_pos = 0;
  TxID id = 0;
  ObTxLogHeader log_header;
  ObString replay_mutator_buf;
  ObTxRedoLog replay_redo;

  ObTxLogBlockHeader &replay_block_header = replay_block.get_header();
  ASSERT_EQ(OB_SUCCESS, replay_block.init_for_replay(fill_block.get_buf(), fill_block.get_size()));


  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_REDO_LOG, log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_redo));
  EXPECT_EQ(fill_redo.get_mutator_size(), replay_redo.get_mutator_size());
  TRANS_LOG(INFO,
            "Mutator Info",
            K(fill_redo.get_mutator_buf()),
            K(replay_redo.get_replay_mutator_buf()),
            K(replay_redo.get_mutator_size()));
  ASSERT_EQ(OB_SUCCESS, serialization::decode(replay_redo.get_replay_mutator_buf(),
                                              replay_redo.get_mutator_size(),
                                              mutator_pos,
                                              replay_mutator_buf));
  EXPECT_EQ(TEST_MUTATOR_BUF, replay_mutator_buf);
  // EXPECT_EQ(TEST_CLOG_ENCRYPT_INFO,replay_redo.get_clog_encrypt_info());
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_INFO_LOG, log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_LOG, log_header.get_tx_log_type());


  //ignore replay log, only need commit log
  ObTxLogBlockHeader &replay_block_header_2 = replay_block_2.get_header();
  ASSERT_EQ(OB_SUCCESS, replay_block_2.init_for_replay(fill_block.get_buf(), fill_block.get_size()));

  ASSERT_EQ(OB_SUCCESS, replay_block_2.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_REDO_LOG, log_header.get_tx_log_type());
  // ASSERT_EQ(OB_SUCCESS, replay_block.deserialize_log_body(replay_redo));
  // ASSERT_EQ(OB_SUCCESS,
  //           serialization::decode(replay_redo.get_replay_mutator_buf(),
  //                                 replay_redo.get_mutator_size(),
  //                                 mutator_pos,
  //                                 replay_mutator_buf));
  // EXPECT_EQ(TEST_MUTATOR_BUF, replay_mutator_buf);
  // EXPECT_EQ(TEST_CLOG_ENCRYPT_INFO,replay_redo.get_clog_encrypt_info());
  ASSERT_EQ(OB_SUCCESS, replay_block_2.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_INFO_LOG, log_header.get_tx_log_type());
  ASSERT_EQ(OB_SUCCESS, replay_block_2.get_next_log(log_header));
  EXPECT_EQ(ObTxLogType::TX_COMMIT_LOG, log_header.get_tx_log_type());
  ObTxCommitLogTempRef commit_temp_ref;
  ObTxCommitLog replay_commit(commit_temp_ref);
  ASSERT_EQ(OB_SUCCESS, replay_block_2.deserialize_log_body(replay_commit));
  EXPECT_EQ(share::SCN::base_scn(), replay_commit.get_commit_version());

}

TEST_F(TestObTxLog, test_commit_log_with_checksum_signature)
{
  uint64_t checksum = 0;
  uint8_t sig[64];
  ObArrayHelper<uint8_t> checksum_signatures(64, sig);
  for(int i = 0; i< 64; i++) {
    uint64_t checksum_i = ObRandom::rand(1, 99999);
    checksum = ob_crc64(checksum, &checksum_i, sizeof(checksum_i));
    checksum_signatures.push_back((uint8_t)(checksum_i & 0xFF));
  }
  ObTxBufferNodeArray tx_buffer_node_array;
  ObTxBufferNode node;
  ObString str("hello,world");
  node.init(ObTxDataSourceType::LS_TABLE, str, share::SCN(), transaction::ObTxSEQ(100, 0), nullptr);
  tx_buffer_node_array.push_back(node);
  share::SCN scn;
  scn.convert_for_tx(101010101010101);
  ObTxCommitLog log0(scn,
                     checksum,
                     checksum_signatures,
                     tx_buffer_node_array,
                     LogOffSet(100),
                     TEST_PREV_LOG_TYPE);
  int64_t size = log0.get_serialize_size();
  char *buf = new char[size];
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, log0.serialize(buf, size, pos));
  ObTxCommitLogTempRef ref;
  ObTxCommitLog log1(ref);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, log1.deserialize(buf, size, pos));
  ASSERT_EQ(log1.checksum_, log0.checksum_);
  ASSERT_EQ(log1.checksum_, checksum);
  ASSERT_EQ(log1.checksum_sig_.count(), 64);
  for(int i = 0; i < log1.checksum_sig_.count(); i++) {
    ASSERT_EQ(log1.checksum_sig_.at(i), sig[i]);
  }
}

TEST_F(TestObTxLog, test_tx_block_header_serialize)
{
  ObTransID tx_id(1024);
  ObTxLogBlockHeader header(103, tx_id);
  const int64_t ser_size = header.get_serialize_size();
  EXPECT_GT(ser_size, 0);
  char buf[256];
  MEMSET(buf, 0, 256);
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, header.serialize(buf, 256, pos));
  EXPECT_EQ(pos, ser_size);

  // test deserialize ok
  ObTxLogBlockHeader header2;
  int64_t pos0 = 0;
  EXPECT_EQ(OB_SUCCESS, header2.deserialize(buf, pos, pos0));
  EXPECT_EQ(pos0, pos);
  EXPECT_EQ(header2.tx_id_, tx_id);
  EXPECT_EQ(header2.log_entry_no_, 103);

  // The log entry number uses fixed-width encoding because the header is reserved
  // before the final entry number is known.
  header.set_log_entry_no(INT64_MAX);
  EXPECT_EQ(ser_size, header.get_serialize_size());
  MEMSET(buf, 0, 256);
  pos = 0;
  EXPECT_EQ(OB_SUCCESS, header.serialize(buf, 256, pos));
  EXPECT_EQ(pos, ser_size);
}

} // namespace unittest
} // namespace oceanbase

using namespace oceanbase;
