// owner: zjf225077
// owner group: log

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

#define private public
#define protected public
#include "env/ob_simple_log_cluster_env.h"
#undef private
#undef protected

const std::string TEST_NAME = "single_replica";

using namespace oceanbase::common;
using namespace oceanbase;
namespace oceanbase
{
using namespace logservice;

namespace logservice
{
int ObLogService::start()
{
  int ret = OB_SUCCESS;
  // palf_env has been started in log_server.init()
  if (OB_FAIL(apply_service_.start())) {
    CLOG_LOG(WARN, "failed to start apply_service_", K(ret));
  } else if (OB_FAIL(replay_service_.start())) {
    CLOG_LOG(WARN, "failed to start replay_service_", K(ret));
  } else if (OB_FAIL(role_change_service_.start())) {
    CLOG_LOG(WARN, "failed to start role_change_service_", K(ret));
  } else if (OB_FAIL(cdc_service_.start())) {
    CLOG_LOG(WARN, "failed to start cdc_service_", K(ret));
  } else if (OB_FAIL(restore_service_.start())) {
    CLOG_LOG(WARN, "failed to start restore_service_", K(ret));
#ifdef OB_BUILD_ARBITRATION
  } else if (OB_FAIL(arb_service_.start())) {
    CLOG_LOG(WARN, "failed to start arb_service_", K(ret));
#endif
  } else {
    is_running_ = true;
    FLOG_INFO("ObLogService is started");
  }
  return ret;
}
}
namespace unittest
{
class TestObSimpleLogClusterSingleReplica : public ObSimpleLogClusterTestEnv
{
public:
  TestObSimpleLogClusterSingleReplica() : ObSimpleLogClusterTestEnv()
  {
    int ret = init();
    if (OB_SUCCESS != ret) {
      throw std::runtime_error("TestObSimpleLogClusterLogEngine init failed");
    }
  }
  ~TestObSimpleLogClusterSingleReplica()
  {
    destroy();
  }
  int init()
  {
    return OB_SUCCESS;
  }
  void destroy()
  {}
  int64_t id_;
  PalfHandleImplGuard leader_;
};

int64_t ObSimpleLogClusterTestBase::member_cnt_ = 1;
int64_t ObSimpleLogClusterTestBase::node_cnt_ = 1;
std::string ObSimpleLogClusterTestBase::test_name_ = TEST_NAME;
bool ObSimpleLogClusterTestBase::need_add_arb_server_  = false;
bool ObSimpleLogClusterTestBase::need_shared_storage_ = false;
constexpr int64_t timeout_ts_us = 3 * 1000 * 1000;
int64_t log_entry_size = 2 * 1024 * 1024 + 16 * 1024;

void read_padding_entry(PalfHandleImplGuard &leader, SCN padding_scn, LSN padding_log_lsn)
{
  // Start reading from the padding group entry
  {
    PalfBufferIterator iterator;
    EXPECT_EQ(OB_SUCCESS, leader.get_palf_handle_impl()->alloc_palf_buffer_iterator(padding_log_lsn, iterator));
    EXPECT_EQ(OB_SUCCESS, iterator.next());
    LogEntry padding_log_entry;
    LSN check_lsn;
    EXPECT_EQ(OB_SUCCESS, iterator.get_entry(padding_log_entry, check_lsn));
    EXPECT_EQ(true, padding_log_entry.header_.is_padding_log_());
    EXPECT_EQ(true, padding_log_entry.check_integrity());
    EXPECT_EQ(padding_scn, padding_log_entry.get_scn());
  }
  // Start reading from the padding log entry
  {
    PalfBufferIterator iterator;
    EXPECT_EQ(OB_SUCCESS, leader.get_palf_handle_impl()->alloc_palf_buffer_iterator(padding_log_lsn+LogGroupEntryHeader::HEADER_SER_SIZE, iterator));
    EXPECT_EQ(OB_SUCCESS, iterator.next());
    LogEntry padding_log_entry;
    LSN check_lsn;
    EXPECT_EQ(OB_SUCCESS, iterator.get_entry(padding_log_entry, check_lsn));
    EXPECT_EQ(true, padding_log_entry.header_.is_padding_log_());
    EXPECT_EQ(true, padding_log_entry.check_integrity());
    EXPECT_EQ(padding_scn, padding_log_entry.get_scn());
  }

}

TEST_F(TestObSimpleLogClusterSingleReplica, delete_paxos_group)
{
  update_server_log_disk(10*1024*1024*1024ul);
  update_disk_options(10*1024*1024*1024ul/palf::PALF_PHY_BLOCK_SIZE);
  SET_CASE_LOG_FILE(TEST_NAME, "delete_paxos_group");
  const int64_t id = ATOMIC_AAF(&palf_id_, 1);
  PALF_LOG(INFO, "start test delete_paxos_group", K(id));
  int64_t leader_idx = 0;
  {
    unittest::PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 100, leader_idx));
  }
  sleep(1);
  // EXPECT_EQ(OB_SUCCESS, delete_paxos_group(id));
  // TODO by yunlong: check log sync
  PALF_LOG(INFO, "end test delete_paxos_group", K(id));
}

TEST_F(TestObSimpleLogClusterSingleReplica, single_replica_flashback)
{
  SET_CASE_LOG_FILE(TEST_NAME, "single_replica_flashback");
  OB_LOGGER.set_log_level("INFO");
  const int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  PALF_LOG(INFO, "start test single replica flashback", K(id));
  SCN max_scn;
  unittest::PalfHandleImplGuard leader;
  int64_t mode_version = INVALID_PROPOSAL_ID;
  SCN ref_scn;
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
  {
    SCN tmp_scn;
    LSN tmp_lsn;
    // Submit 1 log entry before flashback
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, 100));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn()));
    tmp_scn = leader.palf_handle_impl_->get_max_scn();
    switch_append_to_flashback(leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, SCN::minus(tmp_scn, 10), timeout_ts_us));
    // Expected log start point is LSN(0)
    EXPECT_EQ(LSN(0), leader.palf_handle_impl_->get_max_lsn());
    EXPECT_EQ(SCN::minus(tmp_scn, 10), leader.palf_handle_impl_->get_max_scn());
    EXPECT_EQ(LSN(0), leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_);
    // flashback to PADDING log
    switch_flashback_to_append(leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 31, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn()));
    EXPECT_EQ(OB_ITER_END, read_log(leader));
    EXPECT_GT(LSN(PALF_BLOCK_SIZE), leader.palf_handle_impl_->sw_.get_max_lsn());
    int remained_log_size = LSN(PALF_BLOCK_SIZE) - leader.palf_handle_impl_->sw_.get_max_lsn();
    EXPECT_LT(remained_log_size, log_entry_size);
    int need_log_size = remained_log_size - 5*1024;
    PALF_LOG(INFO, "runlin trace print sw1", K(leader.palf_handle_impl_->sw_));
    // Ensure that only less than 1KB of space remains at the end
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, need_log_size));
    PALF_LOG(INFO, "runlin trace print sw2", K(leader.palf_handle_impl_->sw_));
    SCN mid_scn;
    LogEntryHeader header;
    // At this point, there are a total of 32 logs
    EXPECT_EQ(OB_SUCCESS, get_middle_scn(32, leader, mid_scn, header));
    EXPECT_EQ(OB_ITER_END, get_middle_scn(33, leader, mid_scn, header));
    EXPECT_GT(LSN(PALF_BLOCK_SIZE), leader.palf_handle_impl_->sw_.get_max_lsn());
    remained_log_size = LSN(PALF_BLOCK_SIZE) - leader.palf_handle_impl_->sw_.get_max_lsn();
    EXPECT_LT(remained_log_size, 5*1024);
    EXPECT_GT(remained_log_size, 0);
    // Write a log of size 5KB
    LSN padding_log_lsn = leader.get_palf_handle_impl()->get_max_lsn();
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, 5*1024));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.get_palf_handle_impl()->get_max_lsn()));
    // Verify if reading padding was successful
    {
       share::SCN padding_scn = leader.get_palf_handle_impl()->get_max_scn();
       padding_scn = padding_scn.minus(padding_scn, 1);
       read_padding_entry(leader, padding_scn, padding_log_lsn);
    }
    PALF_LOG(INFO, "runlin trace print sw3", K(leader.palf_handle_impl_->sw_));
    // Padding log occupies the number of log entries, therefore there are 34 logs
    EXPECT_EQ(OB_SUCCESS, get_middle_scn(33, leader, mid_scn, header));
    EXPECT_EQ(OB_SUCCESS, get_middle_scn(34, leader, mid_scn, header));
    EXPECT_EQ(OB_ITER_END, get_middle_scn(35, leader, mid_scn, header));
    EXPECT_LT(LSN(PALF_BLOCK_SIZE), leader.palf_handle_impl_->sw_.get_max_lsn());
    max_scn = leader.palf_handle_impl_->sw_.get_max_scn();
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
    switch_append_to_flashback(leader, mode_version);
    // flashback to padding log tail
    tmp_scn = leader.palf_handle_impl_->get_max_scn();
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, SCN::minus(tmp_scn, 1), timeout_ts_us));
    PALF_LOG(INFO, "flashback to padding tail");
    EXPECT_EQ(leader.palf_handle_impl_->get_max_lsn(), LSN(PALF_BLOCK_SIZE));
    EXPECT_EQ(OB_ITER_END, read_log(leader));
    // flashback after there are 33 logs (including padding logs)
    EXPECT_EQ(OB_SUCCESS, get_middle_scn(33, leader, mid_scn, header));
    EXPECT_EQ(OB_ITER_END, get_middle_scn(34, leader, mid_scn, header));
    // Verify if reading padding was successful
    {
       share::SCN padding_scn = leader.get_palf_handle_impl()->get_max_scn();
       padding_scn.minus(padding_scn, 1);
       PALF_LOG(INFO, "begin read_padding_entry", K(padding_scn), K(padding_log_lsn));
       read_padding_entry(leader, padding_scn, padding_log_lsn);
    }
    // flashback to padding log header, there are still 32 logs on disk
    tmp_scn = leader.palf_handle_impl_->get_max_scn();
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, SCN::minus(tmp_scn, 1), timeout_ts_us));
    EXPECT_LT(leader.palf_handle_impl_->get_max_lsn(), LSN(PALF_BLOCK_SIZE));
    EXPECT_EQ(OB_SUCCESS, get_middle_scn(32, leader, mid_scn, header));
    EXPECT_EQ(OB_ITER_END, get_middle_scn(33, leader, mid_scn, header));
    EXPECT_EQ(padding_log_lsn, leader.palf_handle_impl_->get_max_lsn());
    switch_flashback_to_append(leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, leader_idx, 1000));
    EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(LSN(PALF_BLOCK_SIZE), leader));
    EXPECT_EQ(OB_ITER_END, read_log(leader));

    switch_append_to_flashback(leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, SCN::min_scn(), timeout_ts_us));
    EXPECT_EQ(LSN(0), leader.palf_handle_impl_->get_max_lsn());
    switch_flashback_to_append(leader, mode_version);

    ref_scn.convert_for_tx(10000);
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, ref_scn, tmp_lsn, tmp_scn));
    LSN tmp_lsn1 = leader.palf_handle_impl_->get_max_lsn();
    ref_scn.convert_for_tx(50000);
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, ref_scn, tmp_lsn, tmp_scn));
    sleep(1);
    wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
    switch_append_to_flashback(leader, mode_version);
    ref_scn.convert_for_tx(30000);
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, ref_scn, timeout_ts_us));
    // Validate duplicate flashback tasks
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->inner_flashback(ref_scn));
    EXPECT_EQ(tmp_lsn1, leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_);
    // Validate flashback timestamp is not too small
    ref_scn.convert_from_ts(1);
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->inner_flashback(ref_scn));
    EXPECT_GT(tmp_lsn1, leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_);
    CLOG_LOG(INFO, "runlin trace 3");
  }
  switch_flashback_to_append(leader, mode_version);
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 300, leader_idx));
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  // flashback to a middle log entry
	// 1. Compare log_storage and log position and sliding window whether they are the same

  switch_append_to_flashback(leader, mode_version);
  LogEntryHeader header_origin;
	EXPECT_EQ(OB_SUCCESS, get_middle_scn(200, leader, max_scn, header_origin));
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
  LogEntryHeader header_new;
  SCN new_scn;
	EXPECT_EQ(OB_SUCCESS, get_middle_scn(200, leader, new_scn, header_new));
  EXPECT_EQ(new_scn, max_scn);
  EXPECT_EQ(header_origin.data_checksum_, header_origin.data_checksum_);
  switch_flashback_to_append(leader, mode_version);
  LSN new_log_tail = leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_;
  EXPECT_EQ(new_log_tail, leader.palf_handle_impl_->sw_.committed_end_lsn_);
  EXPECT_EQ(max_scn, leader.palf_handle_impl_->sw_.last_slide_scn_);
  EXPECT_EQ(OB_ITER_END, read_log(leader));
	// Verify if log submission can continue after flashback
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 500, leader_idx));
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  // Again execute flashback to the last flashback point
  switch_append_to_flashback(leader, mode_version);
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
  switch_flashback_to_append(leader, mode_version);
  EXPECT_EQ(new_log_tail, leader.palf_handle_impl_->sw_.committed_end_lsn_);
  EXPECT_EQ(max_scn, leader.palf_handle_impl_->sw_.last_slide_scn_);
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 500, leader_idx));
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  // Again execute flashback to the time point of a log submission after the previous flashback
	EXPECT_EQ(OB_SUCCESS, get_middle_scn(634, leader, max_scn, header_origin));
  switch_append_to_flashback(leader, mode_version);
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
  switch_flashback_to_append(leader, mode_version);
  new_log_tail = leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_;
  EXPECT_EQ(max_scn, leader.palf_handle_impl_->sw_.last_slide_scn_);
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 300, leader_idx));
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  PALF_LOG(INFO, "flashback to middle success");
  // flashback to a larger point in time
  max_scn = leader.palf_handle_impl_->get_end_scn();
  new_log_tail = leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_;
  switch_append_to_flashback(leader, mode_version);
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, SCN::plus(max_scn, 1000000000000), timeout_ts_us));
  switch_flashback_to_append(leader, mode_version);
  new_log_tail = leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_;
  EXPECT_EQ(new_log_tail.val_, leader.palf_handle_impl_->sw_.committed_end_lsn_.val_);
  EXPECT_EQ(max_scn, leader.palf_handle_impl_->sw_.last_slide_scn_);
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  PALF_LOG(INFO, "flashback to greater success");

  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 300, leader_idx));
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  new_log_tail = leader.palf_handle_impl_->get_max_lsn();
  max_scn = leader.palf_handle_impl_->get_max_scn();
  PALF_LOG(INFO, "runlin trace 3", K(new_log_tail), K(max_scn));
  switch_append_to_flashback(leader, mode_version);
  LSN new_log_tail_1 = leader.palf_handle_impl_->get_end_lsn();
  SCN max_scn1 = leader.palf_handle_impl_->get_end_scn();
  PALF_LOG(INFO, "runlin trace 4", K(new_log_tail), K(max_scn), K(new_log_tail_1), K(max_scn1));
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
  LSN log_tail_after_flashback = leader.palf_handle_impl_->get_end_lsn();
  SCN max_ts_after_flashback = leader.palf_handle_impl_->get_end_scn();
  PALF_LOG(INFO, "runlin trace 5", K(log_tail_after_flashback), K(max_ts_after_flashback));
  switch_flashback_to_append(leader, mode_version);
  EXPECT_EQ(new_log_tail, leader.palf_handle_impl_->sw_.committed_end_lsn_);
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  PALF_LOG(INFO, "flashback to max_scn success");
  // Again execute flashback to max_scn before the submission log
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 300, leader_idx));
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  LSN curr_lsn = leader.palf_handle_impl_->get_end_lsn();
  EXPECT_NE(curr_lsn, new_log_tail);
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  switch_append_to_flashback(leader, mode_version);
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
  switch_flashback_to_append(leader, mode_version);
  EXPECT_EQ(new_log_tail, leader.palf_handle_impl_->get_end_lsn());
  EXPECT_EQ(OB_ITER_END, read_log(leader));

  // flashback reconfirming leader
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 100, leader_idx));
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  SCN flashback_scn = leader.palf_handle_impl_->get_max_scn();
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 100, leader_idx));
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  switch_append_to_flashback(leader, mode_version);

  dynamic_cast<palf::PalfEnvImpl*>(get_cluster()[0]->get_palf_env())->log_loop_thread_.stop();
  dynamic_cast<palf::PalfEnvImpl*>(get_cluster()[0]->get_palf_env())->log_loop_thread_.wait();
  leader.palf_handle_impl_->state_mgr_.role_ = LEADER;
  leader.palf_handle_impl_->state_mgr_.state_ = RECONFIRM;

  EXPECT_EQ(OB_EAGAIN, leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
  EXPECT_GT(leader.palf_handle_impl_->sw_.get_max_scn(), flashback_scn);

  leader.palf_handle_impl_->state_mgr_.role_ = FOLLOWER;
  leader.palf_handle_impl_->state_mgr_.state_ = ObReplicaState::ACTIVE;

  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
  EXPECT_LT(leader.palf_handle_impl_->sw_.get_max_scn(), flashback_scn);

  EXPECT_EQ(new_log_tail, leader.palf_handle_impl_->get_end_lsn());
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  leader.palf_handle_impl_->state_mgr_.role_ = LEADER;
  leader.palf_handle_impl_->state_mgr_.state_ = ObReplicaState::ACTIVE;
  dynamic_cast<palf::PalfEnvImpl*>(get_cluster()[0]->get_palf_env())->log_loop_thread_.start();
  switch_flashback_to_append(leader, mode_version);
  // All data cleared
  wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
  switch_append_to_flashback(leader, mode_version);
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, SCN::min_scn(), timeout_ts_us));
  EXPECT_EQ(LSN(0), leader.palf_handle_impl_->get_max_lsn());
  EXPECT_EQ(SCN::min_scn(), leader.palf_handle_impl_->get_max_scn());
  switch_flashback_to_append(leader, mode_version);
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  PALF_LOG(INFO, "flashback to 0 success");
  leader.reset();
  delete_paxos_group(id);

}

TEST_F(TestObSimpleLogClusterSingleReplica, single_replica_flashback_restart)
{
  SET_CASE_LOG_FILE(TEST_NAME, "single_replica_flashback_restart");
  OB_LOGGER.set_log_level("INFO");
  const int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  SCN max_scn = SCN::min_scn();
  SCN ref_scn;
  int64_t mode_version = INVALID_PROPOSAL_ID;
  {
    unittest::PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1000, leader_idx, 1000));
    LogEntryHeader header_origin;
		EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
		EXPECT_EQ(OB_SUCCESS, get_middle_scn(323, leader, max_scn, header_origin));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 100, leader_idx, 1000));
		wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
    EXPECT_EQ(OB_ITER_END, read_log(leader));
    switch_append_to_flashback(leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
    LogEntryHeader header_new;
    SCN new_scn;
		EXPECT_EQ(OB_SUCCESS, get_middle_scn(323, leader, new_scn, header_new));
    EXPECT_EQ(new_scn, max_scn);
    EXPECT_EQ(header_origin.data_checksum_, header_new.data_checksum_);
		EXPECT_EQ(OB_ITER_END, get_middle_scn(324, leader, new_scn, header_new));
    switch_flashback_to_append(leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1000, leader_idx, 1000));
		wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn());
		EXPECT_EQ(OB_SUCCESS, get_middle_scn(1323, leader, new_scn, header_new));
		EXPECT_EQ(OB_ITER_END, get_middle_scn(1324, leader, new_scn, header_new));
    EXPECT_EQ(OB_ITER_END, read_log(leader));
  }
  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());
  {
  // Validate restart scenario
  PalfHandleImplGuard new_leader;
  int64_t curr_mode_version = INVALID_PROPOSAL_ID;
  AccessMode curr_access_mode = AccessMode::INVALID_ACCESS_MODE;
  EXPECT_EQ(OB_SUCCESS, get_leader(id, new_leader, leader_idx));
  EXPECT_EQ(OB_SUCCESS, new_leader.palf_handle_impl_->get_access_mode(curr_mode_version, curr_access_mode));
  EXPECT_EQ(curr_mode_version, mode_version);
  EXPECT_EQ(OB_SUCCESS, submit_log(new_leader, 1000, leader_idx, 1000));
	wait_until_has_committed(new_leader, new_leader.palf_handle_impl_->sw_.get_max_lsn());
  EXPECT_EQ(OB_ITER_END, read_log(new_leader));
    ref_scn.convert_for_tx(1000);
  LogEntryHeader header_new;
  LogStorage *log_storage = &new_leader.palf_handle_impl_->log_engine_.log_storage_;
  block_id_t max_block_id = log_storage->block_mgr_.max_block_id_;
	EXPECT_EQ(OB_SUCCESS, get_middle_scn(1329, new_leader, max_scn, header_new));
  // flashback cross-file scene restart
  EXPECT_EQ(OB_SUCCESS, submit_log(new_leader, 33, leader_idx, MAX_LOG_BODY_SIZE));
	wait_until_has_committed(new_leader, new_leader.palf_handle_impl_->sw_.get_max_lsn());
  EXPECT_LE(max_block_id, log_storage->block_mgr_.max_block_id_);
  switch_append_to_flashback(new_leader, mode_version);
  EXPECT_EQ(OB_SUCCESS, new_leader.palf_handle_impl_->flashback(mode_version, max_scn, timeout_ts_us));
  EXPECT_GE(max_block_id, log_storage->block_mgr_.max_block_id_);
  switch_flashback_to_append(new_leader, mode_version);
  }
  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());
  {
  PalfHandleImplGuard new_leader;
  int64_t curr_mode_version = INVALID_PROPOSAL_ID;
  AccessMode curr_access_mode = AccessMode::INVALID_ACCESS_MODE;
  EXPECT_EQ(OB_SUCCESS, get_leader(id, new_leader, leader_idx));
  EXPECT_EQ(OB_SUCCESS, new_leader.palf_handle_impl_->get_access_mode(curr_mode_version, curr_access_mode));
  // flashback to the end of a certain file
  EXPECT_EQ(OB_SUCCESS, submit_log(new_leader, 65, leader_idx, MAX_LOG_BODY_SIZE));
	wait_until_has_committed(new_leader, new_leader.palf_handle_impl_->sw_.get_max_lsn());
  switch_append_to_flashback(new_leader, mode_version);
  LSN lsn(PALF_BLOCK_SIZE);
  LogStorage *log_storage = &new_leader.palf_handle_impl_->log_engine_.log_storage_;
  SCN block_end_scn;
  {
    PalfGroupBufferIterator iterator;
    auto get_file_end_lsn = [](){
      return LSN(PALF_BLOCK_SIZE);
    };
    EXPECT_EQ(OB_SUCCESS, iterator.init(LSN(0), get_file_end_lsn, log_storage));
    LogGroupEntry entry;
    LSN lsn;
    while (OB_SUCCESS == iterator.next()) {
      EXPECT_EQ(OB_SUCCESS, iterator.get_entry(entry, lsn));
    }
    block_end_scn = entry.get_scn();
  }
  EXPECT_EQ(OB_SUCCESS, new_leader.palf_handle_impl_->flashback(mode_version, block_end_scn, timeout_ts_us));
  EXPECT_EQ(lsn, log_storage->log_tail_);
  EXPECT_EQ(OB_ITER_END, read_log(new_leader));
  }
  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());
  // Restart and continue submitting logs
  {
    PalfHandleImplGuard new_leader;
    EXPECT_EQ(OB_SUCCESS, get_leader(id, new_leader, leader_idx));
    switch_flashback_to_append(new_leader, mode_version);
    EXPECT_EQ(true, 0 == lsn_2_offset(new_leader.get_palf_handle_impl()->get_max_lsn(), PALF_BLOCK_SIZE));
    share::SCN padding_scn = new_leader.get_palf_handle_impl()->get_max_scn();
    EXPECT_EQ(OB_SUCCESS, submit_log(new_leader, 100, leader_idx));
	  wait_until_has_committed(new_leader, new_leader.palf_handle_impl_->sw_.get_max_lsn());
    EXPECT_EQ(OB_ITER_END, read_log(new_leader));
    switch_append_to_flashback(new_leader, mode_version);
    // flashback to padding log header and restart
    EXPECT_EQ(OB_SUCCESS, new_leader.palf_handle_impl_->flashback(mode_version, padding_scn.minus(padding_scn, 1), timeout_ts_us));
    EXPECT_EQ(true, 0 != lsn_2_offset(new_leader.get_palf_handle_impl()->get_max_lsn(), PALF_BLOCK_SIZE));
    new_leader.reset();
  }
  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());
  // Restart submission log, do not generate padding log
  {
    PalfHandleImplGuard new_leader;
    EXPECT_EQ(OB_SUCCESS, get_leader(id, new_leader, leader_idx));
    LSN padding_start_lsn = new_leader.get_palf_handle_impl()->get_max_lsn();
    EXPECT_EQ(true, 0 != lsn_2_offset(new_leader.get_palf_handle_impl()->get_max_lsn(), PALF_BLOCK_SIZE));
    const int64_t remained_size = PALF_BLOCK_SIZE - lsn_2_offset(new_leader.get_palf_handle_impl()->get_max_lsn(), PALF_BLOCK_SIZE);
    EXPECT_GE(remained_size, 0);
    const int64_t group_entry_body_size = remained_size - LogGroupEntryHeader::HEADER_SER_SIZE - LogEntryHeader::HEADER_SER_SIZE;
    PALF_LOG(INFO, "runlin trace print remained_size", K(remained_size), K(group_entry_body_size));
    switch_flashback_to_append(new_leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, submit_log(new_leader, 1, leader_idx, group_entry_body_size));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(new_leader, new_leader.get_palf_handle_impl()->get_max_lsn()));
    PalfBufferIterator iterator;
    EXPECT_EQ(OB_SUCCESS, new_leader.get_palf_handle_impl()->alloc_palf_buffer_iterator(padding_start_lsn, iterator));
    EXPECT_EQ(OB_SUCCESS, iterator.next());
    LogEntry log_entry;
    LSN check_lsn;
    EXPECT_EQ(OB_SUCCESS, iterator.get_entry(log_entry, check_lsn));
    EXPECT_EQ(check_lsn, padding_start_lsn + LogGroupEntryHeader::HEADER_SER_SIZE);
    EXPECT_EQ(false, log_entry.header_.is_padding_log_());
    EXPECT_EQ(true, log_entry.check_integrity());
    new_leader.reset();
  }
  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());
  // Restart and continue submitting logs
  {
    PalfHandleImplGuard new_leader;
    EXPECT_EQ(OB_SUCCESS, get_leader(id, new_leader, leader_idx));
    EXPECT_EQ(true, 0 == lsn_2_offset(new_leader.get_palf_handle_impl()->get_max_lsn(), PALF_BLOCK_SIZE));
    EXPECT_EQ(OB_SUCCESS, submit_log(new_leader, 100, leader_idx, 1000));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(new_leader, new_leader.get_palf_handle_impl()->get_max_lsn()));
    EXPECT_EQ(OB_ITER_END, read_log(new_leader));
  }
  delete_paxos_group(id);
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_truncate_failed)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_truncate_failed");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  char block_path[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  int64_t file_size = 0;
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, id, 1000));
    wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader);
    LSN max_lsn = leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_;
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, id, 1000));
    wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader);
    int64_t fd = leader.palf_handle_impl_->log_engine_.log_storage_.block_mgr_.curr_writable_handler_.io_fd_.second_id_;
    block_id_t block_id = leader.palf_handle_impl_->log_engine_.log_storage_.block_mgr_.curr_writable_block_id_;
    char *log_dir = leader.palf_handle_impl_->log_engine_.log_storage_.block_mgr_.log_dir_;
    convert_to_normal_block(log_dir, block_id, block_path, OB_MAX_FILE_NAME_LENGTH);
    EXPECT_EQ(OB_ITER_END, read_log(leader));
    PALF_LOG_RET(ERROR, OB_SUCCESS, "truncate pos", K(max_lsn));
    EXPECT_EQ(0, ftruncate(fd, max_lsn.val_+MAX_INFO_BLOCK_SIZE));
    FileDirectoryUtils::get_file_size(block_path, file_size);
    EXPECT_EQ(file_size, max_lsn.val_+MAX_INFO_BLOCK_SIZE);
  }
  PalfHandleImplGuard leader;
  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());;
  FileDirectoryUtils::get_file_size(block_path, file_size);
  EXPECT_EQ(file_size, PALF_PHY_BLOCK_SIZE);
  get_leader(id, leader, leader_idx);
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 32, id, MAX_LOG_BODY_SIZE));
  wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader);
  EXPECT_EQ(OB_ITER_END, read_log(leader));
  // Validate file tail after truncate and restart
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->log_engine_.truncate(LSN(PALF_BLOCK_SIZE)));
  EXPECT_EQ(LSN(PALF_BLOCK_SIZE), leader.palf_handle_impl_->log_engine_.log_storage_.log_tail_);
  leader.reset();
  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_meta)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_meta");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  LSN upper_aligned_log_tail;
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    sleep(1);
    // Test the restart scenario where the meta file is exactly full
    LogEngine *log_engine = &leader.palf_handle_impl_->log_engine_;
    LogStorage *log_meta_storage = &log_engine->log_meta_storage_;
    LSN log_meta_tail = log_meta_storage->log_tail_;
    upper_aligned_log_tail.val_ = (lsn_2_block(log_meta_tail, PALF_META_BLOCK_SIZE) + 1) * PALF_META_BLOCK_SIZE;
    int64_t delta = upper_aligned_log_tail - log_meta_tail;
    int64_t delta_cnt = delta / MAX_META_ENTRY_SIZE;
    while (delta_cnt-- > 0) {
      log_engine->append_log_meta_(log_engine->log_meta_);
    }
    EXPECT_EQ(upper_aligned_log_tail, log_meta_storage->log_tail_);
    PALF_LOG_RET(ERROR, OB_SUCCESS, "runlin trace before restart", K(upper_aligned_log_tail), KPC(log_meta_storage));
  }

  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());

  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, get_leader(id, leader, leader_idx));
    LogEngine *log_engine = &leader.palf_handle_impl_->log_engine_;
    LogStorage *log_meta_storage = &log_engine->log_meta_storage_;
    LSN log_meta_tail = log_meta_storage->log_tail_;
    upper_aligned_log_tail.val_ = (lsn_2_block(log_meta_tail, PALF_META_BLOCK_SIZE) + 1) * PALF_META_BLOCK_SIZE;
    int64_t delta = upper_aligned_log_tail - log_meta_tail;
    int64_t delta_cnt = delta / MAX_META_ENTRY_SIZE;
    while (delta_cnt-- > 0) {
      log_engine->append_log_meta_(log_engine->log_meta_);
    }
    EXPECT_EQ(upper_aligned_log_tail, log_meta_storage->log_tail_);
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 32, id, MAX_LOG_BODY_SIZE));
    sleep(1);
    wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader);
    block_id_t min_block_id, max_block_id;
    EXPECT_EQ(OB_SUCCESS, log_meta_storage->get_block_id_range(min_block_id, max_block_id));
    EXPECT_EQ(min_block_id, max_block_id);
  }
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_iterator)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_iterator");
  OB_LOGGER.set_log_level("TRACE");
  const int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  int64_t mode_version_v = 1;
  int64_t *mode_version = &mode_version_v;
  LSN end_lsn_v = LSN(100000000);
  LSN *end_lsn = &end_lsn_v;
  {
    SCN max_scn_case1, max_scn_case2, max_scn_case3;
    PalfHandleImplGuard leader;
    PalfHandleImplGuard raw_write_leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    PalfHandleImpl *palf_handle_impl = leader.palf_handle_impl_;
    const int64_t id_raw_write = ATOMIC_AAF(&palf_id_, 1);
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id_raw_write, leader_idx, raw_write_leader));
    EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(raw_write_leader));
    int64_t count = 5;
    // Submit 1024 logs, record max_scn, used for subsequent next iteration verification, case1
    for (int i = 0; i < count; i++) {
      EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, id, 4*1024));
      EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));
    }
    max_scn_case1 = palf_handle_impl->get_max_scn();
    // Submit 5 logs, case1 successful after, execute case2
    for (int i = 0; i < count; i++) {
      EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, id, 4*1024));
      EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));
    }
    max_scn_case2 = palf_handle_impl->get_max_scn();
    // Submit 5 logs, case3, verify next(replayable_point_scn, &next_log_min_scn, &bool)
    std::vector<LSN> lsns;
    std::vector<SCN> logts;
    const int64_t log_size = 500;
    auto submit_log_private =[this](PalfHandleImplGuard &leader,
                              const int64_t count,
                              const int64_t id,
                              const int64_t wanted_data_size,
                              std::vector<LSN> &lsn_array,
                              std::vector<SCN> &scn_array)-> int{
      int ret = OB_SUCCESS;
      lsn_array.resize(count);
      scn_array.resize(count);
      for (int i = 0; i < count && OB_SUCC(ret); i++) {
        SCN ref_scn;
        ref_scn.convert_from_ts(ObTimeUtility::current_time() + 10000000);
        std::vector<LSN> tmp_lsn_array;
        std::vector<SCN> tmp_log_scn_array;
        if (OB_FAIL(submit_log_impl(leader, 1, id, wanted_data_size, ref_scn, tmp_lsn_array, tmp_log_scn_array))) {
        } else {
          lsn_array[i] = tmp_lsn_array[0];
          scn_array[i] = tmp_log_scn_array[0];
          wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader);
          CLOG_LOG(INFO, "submit_log_private success", K(i), "scn", tmp_log_scn_array[0], K(ref_scn));
        }
      }
      return ret;
    };
    EXPECT_EQ(OB_SUCCESS, submit_log_private(leader, count, id, log_size, lsns, logts));

    max_scn_case3 = palf_handle_impl->get_max_scn();
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, palf_handle_impl->get_end_lsn()));
    EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(leader, raw_write_leader));
    PalfHandleImpl *raw_write_palf_handle_impl = raw_write_leader.palf_handle_impl_;
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(raw_write_leader, raw_write_palf_handle_impl->get_end_lsn()));


    PalfBufferIterator iterator;
    auto get_file_end_lsn = [&end_lsn]() -> LSN { return *end_lsn; };
    auto get_mode_version = [&mode_version, &mode_version_v]() -> int64_t {
      PALF_LOG(INFO, "runlin trace", K(*mode_version), K(mode_version_v));
      return *mode_version;
    };
    EXPECT_EQ(OB_SUCCESS,
        iterator.init(LSN(0), get_file_end_lsn, get_mode_version, &raw_write_palf_handle_impl->log_engine_.log_storage_));
    EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn_case1)); count--;
    EXPECT_EQ(OB_ITER_END, iterator.next(SCN::base_scn()));
    // case0: validate group iterator log iteration functionality
    EXPECT_EQ(OB_ITER_END, read_group_log(raw_write_leader, LSN(0)));

    LSN curr_lsn = iterator.iterator_impl_.get_curr_read_lsn();
    // case1:
    // - Verify that the cache is cleared after mode_version changes
    // - whether replayable_point_scn is effective
    // When mode version changes, the expected cache should be cleared
    // raw mode, when replayable_point_scn is very small, directly return OB_ITER_END
    PALF_LOG(INFO, "runlin trace case1", K(mode_version_v), K(*mode_version), K(max_scn_case1));
    // mode_version_v is an invalid value, the expected behavior is not to clear
    mode_version_v = INVALID_PROPOSAL_ID;
    end_lsn_v = curr_lsn;
    EXPECT_FALSE(curr_lsn == iterator.iterator_storage_.end_lsn_);
    EXPECT_FALSE(curr_lsn == iterator.iterator_storage_.start_lsn_);
    EXPECT_EQ(OB_ITER_END, iterator.next(SCN::base_scn()));
    //  mode_version_v is smaller than inital_mode_version, expectation is not to clear
    mode_version_v = -1;
    EXPECT_FALSE(curr_lsn == iterator.iterator_storage_.end_lsn_);
    EXPECT_FALSE(curr_lsn == iterator.iterator_storage_.start_lsn_);
    EXPECT_EQ(OB_ITER_END, iterator.next(SCN::base_scn()));
    // reasonable mode_version_v, clear cache
    mode_version_v = 100;
    end_lsn_v = curr_lsn;
    EXPECT_EQ(OB_ITER_END, iterator.next(SCN::base_scn()));
    // cache clear, depends on the last next operation
    EXPECT_EQ(curr_lsn, iterator.iterator_storage_.start_lsn_);
    EXPECT_EQ(curr_lsn, iterator.iterator_storage_.end_lsn_);

    PALF_LOG(INFO, "runlin trace", K(iterator), K(max_scn_case1), K(curr_lsn));

    end_lsn_v = LSN(1000000000);
    // When replayable_point_scn is max_log_ts, expect 5 logs to be output before max_log_ts
    EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn_case1)); count--;
    while (count > 0) {
      EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn_case1));
      count--;
    }
    EXPECT_EQ(OB_ITER_END, iterator.next(max_scn_case1));
    // case2: next function is normal or not
    // Try to read the next 5 logs
    count = 5;

    PALF_LOG(INFO, "runlin trace case2", K(iterator), K(max_scn_case2));
    while (count > 0) {
      EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn_case2));
      count--;
    }
    // At this point, curr_entry is already the first log (first_log) of the third commit log
    // Since the timestamp of this log is greater than max_scn_case2, it will not be output
    // NB: Here, during testing, we encountered the following situation: case3's first next after EXPECT_EQ:
    // curr_entry becomes first_log after that, in subsequent tests, try setting file_end_lsn to
    // before fisrt_log, then a situation occurred, at this point calling next(fist_log_ts, next_log_min_scn) afterwards,
    // next_log_min_scn is set to first_scn+1, externally it appears as: although first_log exists, the external party sees that
    // Before seeing first_log, next_log_min_scn must be greater than first_scn
    //
    // Actually, this situation will not occur because file_end_lsn will not retreat
    EXPECT_EQ(OB_ITER_END, iterator.next(max_scn_case2));

    //case3: next(replayable_point_scn, &next_log_min_scn)
    PALF_LOG(INFO, "runlin trace case3", K(iterator), K(max_scn_case3), K(end_lsn_v), K(max_scn_case2));
    SCN first_scn = logts[0];
    // When using the next(replayable_point_scn, &next_log_min_scn) interface
    // We prohibit using the head of LogEntry as the iterator endpoint
    LSN first_log_start_lsn = lsns[0] - sizeof(LogGroupEntryHeader);
    LSN first_log_end_lsn = lsns[0]+log_size+sizeof(LogEntryHeader);
    SCN next_log_min_scn;
    bool iterate_end_by_replayable_point = false;
    count = 5;
    // Simulate reaching the end of the file in advance, no new logs have been read, therefore next_log_min_scn is prev_entry_scn_ + 1
    end_lsn_v = first_log_start_lsn - 1;
    CLOG_LOG(INFO, "runlin trace 1", K(iterator), K(end_lsn_v), KPC(end_lsn), K(max_scn_case2), K(first_scn));
    EXPECT_EQ(OB_ITER_END, iterator.next(SCN::plus(first_scn, 10000), next_log_min_scn, iterate_end_by_replayable_point));
    // Although file_end_lsn has been rolled back, curr_entry_ has not been read, so next_log_min_scn is still first_scn
    EXPECT_EQ(SCN::plus(iterator.iterator_impl_.prev_entry_scn_, 1), next_log_min_scn);
    EXPECT_EQ(iterate_end_by_replayable_point, false);
    CLOG_LOG(INFO, "runlin trace 3.1", K(iterator), K(end_lsn_v), KPC(end_lsn));
    EXPECT_EQ(first_log_start_lsn,
    iterator.iterator_impl_.log_storage_->get_lsn(iterator.iterator_impl_.curr_read_pos_));
    // Read a log successfully, next_log_min_scn will be reset
    // curr_entry is the log corresponding to fisrt_log_ts
    end_lsn_v = first_log_end_lsn;
    CLOG_LOG(INFO, "runlin trace 2", K(iterator), K(end_lsn_v), KPC(end_lsn));
    EXPECT_EQ(OB_SUCCESS, iterator.next(first_scn, next_log_min_scn, iterate_end_by_replayable_point)); count--;
    // iterator returns successfully, next_log_min_scn should be OB_INVALID_TIMESTAMP
    EXPECT_EQ(next_log_min_scn.is_valid(), false);
    // iterator's prev_entry_scn_ is set to first_scn
    EXPECT_EQ(iterator.iterator_impl_.prev_entry_scn_, first_scn);

    CLOG_LOG(INFO, "runlin trace 3", K(iterator), K(end_lsn_v), KPC(end_lsn));
    {
      // Simulate reaching the end of the file in advance, at which point the end of the file is file_log_end_lsn
      // Expected next_log_min_scn to be the log corresponding to first_scn + 1
      SCN second_scn = logts[1];
      EXPECT_EQ(OB_ITER_END, iterator.next(second_scn, next_log_min_scn, iterate_end_by_replayable_point));
      // iterator returns OB_ITER_END, next_log_min_scn is first_scn + 1
      EXPECT_EQ(next_log_min_scn, SCN::plus(first_scn, 1));
      EXPECT_EQ(iterate_end_by_replayable_point, false);
      CLOG_LOG(INFO, "runlin trace 3", K(iterator), K(end_lsn_v), KPC(end_lsn), K(first_scn), K(second_scn));
      // Call next again, expect next_log_min_scn to still be first_scn + 1
      EXPECT_EQ(OB_ITER_END, iterator.next(second_scn, next_log_min_scn, iterate_end_by_replayable_point));
      // iterator returns OB_ITER_END, next_log_min_scn is first_scn + 1
      EXPECT_EQ(next_log_min_scn, SCN::plus(first_scn, 1));
    }

    CLOG_LOG(INFO, "runlin trace 4", K(iterator), K(end_lsn_v), KPC(end_lsn));
    SCN prev_next_success_scn;
    // Simulate reaching replayable_point_scn, at this time the file end is second log, expected next_log_min_scn to be replayable_point_scn+1
    // simultaneously replayable_point_scn < cached log timestamp
    {
      SCN second_scn = logts[1];
      SCN replayable_point_scn = SCN::minus(second_scn, 1);
      end_lsn_v = lsns[1]+log_size+sizeof(LogEntryHeader);
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(iterate_end_by_replayable_point, true);
      // iterator returns OB_ITER_END, next_log_min_scn is replayable_point_scn + 1
      PALF_LOG(INFO, "runliun trace 4.1", K(replayable_point_scn), K(next_log_min_scn),
      K(iterator));
      EXPECT_EQ(next_log_min_scn, SCN::plus(replayable_point_scn, 1));
      // Call next again, expect next_log_min_scn to still be replayable_point_scn+1
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      // iterator returns OB_ITER_END, next_log_min_scn is replayable_point_scn + 1
      EXPECT_EQ(next_log_min_scn, SCN::plus(replayable_point_scn, 1));
      EXPECT_EQ(iterate_end_by_replayable_point, true);
      EXPECT_EQ(OB_SUCCESS, iterator.next(second_scn, next_log_min_scn, iterate_end_by_replayable_point)); count--;
      EXPECT_EQ(next_log_min_scn.is_valid(), false);
      prev_next_success_scn = iterator.iterator_impl_.prev_entry_scn_;
      EXPECT_EQ(prev_next_success_scn, second_scn);
    }
    // Simulate file end lsn is not the endpoint of group entry
   {
     // Set the endpoint to the start point of the third LogEntry
     end_lsn_v = lsns[2]+10;
     // Set timestamp to the third log
     SCN third_scn = logts[2];
     SCN replayable_point_scn = SCN::plus(third_scn, 10);
     CLOG_LOG(INFO, "runlin trace 5.1", K(iterator), K(end_lsn_v), KPC(end_lsn), K(replayable_point_scn));
     // At this time, the log cached in memory is the third log, iterator has read new logs, but this log is unreadable due to end_lsn (at this point, since the log is not controlled replay, curr_read_pos_ will be advanced by 56)
     // Therefore next_log_min_scn will be set to third_scn
     EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
     CLOG_LOG(INFO, "runlin trace 5.1.1", K(iterator), K(next_log_min_scn), K(replayable_point_scn));
     EXPECT_EQ(next_log_min_scn, third_scn);
     EXPECT_EQ(iterate_end_by_replayable_point, false);
     // Validate the third log entry as controlled replay cannot output (replayable_point_scn rollback should not occur, simulated intentionally for testing)
     replayable_point_scn = SCN::minus(third_scn, 4);
     EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
     // Since there can be no logs between replayable_point_scn and curr_entry_, and replayable_point_scn < curr_entry_,
     // Since prev_entry_scn_ is the timestamp of the second log entry at this point, it is less than replayable_point_scn, therefore
     // next_min_scn will be set to the minimum value of curr_entry_scn and replayable_point_scn + 1,
     // Therefore prev_entry_scn_ will be pushed to replayable_point_scn + 1
     // At the same time, since prev_entry_scn_ is less than replayable_point_scn, there are no logs between replayable_point_scn and prev_entry_scn_
     // Therefore, deduce prev_entry_scn_ to replayable_point_scn_
     EXPECT_EQ(next_log_min_scn, SCN::plus(replayable_point_scn, 1));
     EXPECT_EQ(iterate_end_by_replayable_point, true);
     EXPECT_EQ(iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
     prev_next_success_scn = replayable_point_scn;
     EXPECT_EQ(replayable_point_scn, iterator.iterator_impl_.prev_entry_scn_);

     CLOG_LOG(INFO, "runlin trace 5.2", K(iterator), K(end_lsn_v), KPC(end_lsn), K(replayable_point_scn));
     // Reduce replayable_point_scn, at this time iterator will set next_min_scn to prev_next_success_scn + 1
     replayable_point_scn = SCN::minus(replayable_point_scn, 2);
     EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
     EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
   }

    end_lsn_v = LSN(1000000000);
    while (count > 0) {
      EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn_case3, next_log_min_scn, iterate_end_by_replayable_point));
      prev_next_success_scn = iterator.iterator_impl_.prev_entry_scn_;
      EXPECT_EQ(false, next_log_min_scn.is_valid());
      count--;
    }

    CLOG_LOG(INFO, "runlin trace 6.1", K(iterator), K(end_lsn_v), K(max_scn_case3));
    // There is no readable log on disk and after the controlled replay point, at this time, controlled replay point + 1 should be returned
    EXPECT_EQ(OB_ITER_END, iterator.next(max_scn_case3, next_log_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(SCN::plus(max_scn_case3, 1), next_log_min_scn);
    EXPECT_EQ(max_scn_case3, prev_next_success_scn);
    CLOG_LOG(INFO, "runlin trace 6.2", K(iterator), K(end_lsn_v), K(max_scn_case3), "end_lsn_of_leader",
        raw_write_leader.palf_handle_impl_->get_max_lsn());
    // raw write becomes Append after, then writes some logs
    // Test raw write change to append, whether the iterative log is normal
    {
      std::vector<SCN> logts_append;
      std::vector<LSN> lsns_append;
      int count_append = 5;

      EXPECT_EQ(OB_SUCCESS, change_access_mode_to_append(raw_write_leader));
      PALF_LOG(INFO, "runlin trace 6.3", "raw_write_leader_lsn", raw_write_leader.palf_handle_impl_->get_max_lsn(),
          "new_leader_lsn", leader.palf_handle_impl_->get_max_lsn());
      EXPECT_EQ(OB_SUCCESS, submit_log_private(leader, count_append, id, log_size, lsns_append, logts_append));
      EXPECT_EQ(OB_SUCCESS, submit_log_private(raw_write_leader, count_append, id, log_size, lsns_append, logts_append));
      EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));
      EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(raw_write_leader.palf_handle_impl_->get_max_lsn(), raw_write_leader));
      PALF_LOG(INFO, "runlin trace 6.4", "raw_write_leader_lsn", raw_write_leader.palf_handle_impl_->get_max_lsn(),
          "new_leader_lsn", leader.palf_handle_impl_->get_max_lsn());
      // case 7 after end_lsn_v becomes a very large value, let there be 2M of data in memory, expect iterator next to fail due to controlled replay failure, prev_entry_scn_ remains unchanged
      // replayable_point_scn is the timestamp of the first log minus 2, next_log_min_scn is the timestamp when the first LogEntry is appended
      // NB: If the data is not read into memory, there may be an error reading data with the issue OB_NEED_RETRY.
      end_lsn_v = LSN(1000000000);
      SCN replayable_point_scn = SCN::minus(logts_append[0], 2);
      EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point)); count_append--;
      prev_next_success_scn = iterator.iterator_impl_.prev_entry_scn_;

      end_lsn_v = lsns_append[1]+2;
      // At this point curr_entry_ is the second log, curr_entry is valid but file end lsn is unreadable
      // For append log controlled replay is invalid
      replayable_point_scn = SCN::plus(raw_write_leader.palf_handle_impl_->get_max_scn(), 1000000);
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      PALF_LOG(INFO, "runlin trace 7.1", K(iterator), K(replayable_point_scn), K(end_lsn_v), K(logts_append[1]), K(replayable_point_scn));
      EXPECT_EQ(next_log_min_scn, logts_append[1]);
      EXPECT_EQ(prev_next_success_scn, iterator.iterator_impl_.prev_entry_scn_);
      EXPECT_EQ(iterate_end_by_replayable_point, false);

      PALF_LOG(INFO, "runlin trace 7.1.1", K(iterator), K(replayable_point_scn), K(end_lsn_v), K(logts_append[1]));
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(next_log_min_scn, logts_append[1]);
      EXPECT_EQ(prev_next_success_scn, iterator.iterator_impl_.prev_entry_scn_);
      // replayable_point_scn rollback is an impossible situation, but from the iterator perspective, we cannot rely on this
      // Verify replayable_point_scn rolls back to a very small value, expect next_log_min_scn to be prev_next_success_scn+1
      // Simulate replayable_point_scn less than prev_entry_
      replayable_point_scn.convert_for_tx(100);
      PALF_LOG(INFO, "runlin trace 7.2", K(iterator), K(replayable_point_scn), K(end_lsn_v), K(logts_append[0]));
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
      EXPECT_EQ(prev_next_success_scn, iterator.iterator_impl_.prev_entry_scn_);
      EXPECT_EQ(iterate_end_by_replayable_point, false);
      // Iterate once more, the result is the same
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
      EXPECT_EQ(prev_next_success_scn, iterator.iterator_impl_.prev_entry_scn_);
      // Verify that the value of replayable_point_scn is between prev_next_success_scn and the log of the second append,
      // Expected next_log_min_scn to be replayable_point_scn + 1
      // Simulate replayable_point_scn located between [prev_entry_, curr_entry_]
      replayable_point_scn = SCN::minus(logts_append[1], 4);
      PALF_LOG(INFO, "runlin trace 7.3", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[1]), K(prev_next_success_scn));
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(next_log_min_scn, SCN::plus(replayable_point_scn, 1));
      // Since there are no logs between replayable_point_scn and curr_entry_, prev_entry_scn_ will be pushed to replayable_point_scn
      EXPECT_EQ(replayable_point_scn, iterator.iterator_impl_.prev_entry_scn_);
      // In one iteration
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(next_log_min_scn, SCN::plus(replayable_point_scn, 1));
      // Since there are no logs between replayable_point_scn and curr_entry_, prev_entry_scn_ will be pushed to replayable_point_scn
      EXPECT_EQ(replayable_point_scn, iterator.iterator_impl_.prev_entry_scn_);
      // Verify that the iterative append log is successful,
      end_lsn_v = lsns_append[2]+2;
      replayable_point_scn = logts_append[0];
      EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(false, next_log_min_scn.is_valid());
      EXPECT_EQ(logts_append[1], iterator.iterator_impl_.prev_entry_scn_); count_append--;
      prev_next_success_scn = logts_append[1];
      // replayable_point_scn is large, expect next_log_min_scn to be logts_append[2]
      replayable_point_scn.convert_from_ts(ObTimeUtility::current_time() + 100000000);
      PALF_LOG(INFO, "runlin trace 7.4", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[2]), K(prev_next_success_scn));
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(next_log_min_scn, logts_append[2]);
      // Iterate once more, the result is the same
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(next_log_min_scn, logts_append[2]);
      EXPECT_EQ(iterate_end_by_replayable_point, false);
      // Rollback replayable_point_scn, expect next_log_min_scn to be prev_next_success_scn+1
      replayable_point_scn.convert_for_tx(100);
      EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
      EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
      EXPECT_EQ(iterate_end_by_replayable_point, false);

      end_lsn_v = LSN(1000000000);
      replayable_point_scn.convert_from_ts(ObTimeUtility::current_time() + 100000000);
      // Log a message
      while (count_append > 1) {
        EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(false, next_log_min_scn.is_valid());
        prev_next_success_scn = iterator.iterator_impl_.prev_entry_scn_;
        count_append--;
      }
      // Verify that append switches back to raw and works normally
      {
        int64_t id3 = ATOMIC_AAF(&palf_id_, 1);
        std::vector<SCN> logts_append;
        std::vector<LSN> lsns_append;
        int count_append = 5;
        PALF_LOG(INFO, "runlin trace 8.1.0", "raw_write_leader_lsn", raw_write_leader.palf_handle_impl_->get_max_lsn(),
            "new_leader_lsn", leader.palf_handle_impl_->get_max_lsn());
        EXPECT_EQ(OB_SUCCESS, submit_log_private(leader, count_append, id, log_size, lsns_append, logts_append));
        SCN max_scn_case4 = leader.palf_handle_impl_->get_max_scn();
        EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
        EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(raw_write_leader));
        PALF_LOG(INFO, "runlin trace 8.1", "raw_write_leader_lsn", raw_write_leader.palf_handle_impl_->get_max_lsn(),
            "new_leader_lsn", leader.palf_handle_impl_->get_max_lsn());
        EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(leader,
                                                         raw_write_leader,
                                                         raw_write_leader.palf_handle_impl_->get_max_lsn()));
        EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(raw_write_leader.palf_handle_impl_->get_max_lsn(), raw_write_leader));
        PALF_LOG(INFO, "runlin trace 8.2", "raw_write_leader_lsn", raw_write_leader.palf_handle_impl_->get_max_lsn(),
            "new_leader_lsn", leader.palf_handle_impl_->get_max_lsn());
        // replayable_point_scn is too small
        SCN replayable_point_scn;
        replayable_point_scn.convert_for_tx(100);
        PALF_LOG(INFO, "runlin trace 8.3", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[0]), K(prev_next_success_scn));
        // Log from the previous round, no need to decrement count_append
        EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        prev_next_success_scn = iterator.iterator_impl_.prev_entry_scn_;
        // Since the controlled replay point is unreadable, next_log_min_scn should be prev_next_success_scn + 1
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);

        PALF_LOG(INFO, "runlin trace 8.3.1", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[0]), K(prev_next_success_scn));
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
        // Push the controlled replay point to the first log, but end_lsn_v also becomes the start of the first log, at which point end_lsn_v will become unreadable
        // Expected next_min_scn to be replayable_point_scn.
        // Since this log was not controlled replay in the previous next, it will push curr_read_pos_ to the LogEntry header, the next next does not need to read data and will directly return OB_ITER_END
        end_lsn_v = lsns_append[0]+10;
        replayable_point_scn = logts_append[0];
        PALF_LOG(INFO, "runlin trace 8.4", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[0]), K(prev_next_success_scn));
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(replayable_point_scn, next_log_min_scn);
        EXPECT_EQ(iterate_end_by_replayable_point, false);

        PALF_LOG(INFO, "runlin trace 8.4.1", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[0]), K(prev_next_success_scn));
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(replayable_point_scn, next_log_min_scn);
        EXPECT_EQ(iterate_end_by_replayable_point, false);

        PALF_LOG(INFO, "runlin trace 8.4.2", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[0]), K(prev_next_success_scn));
        // Simulate no log after prev_entry_, replayable_point_scn less than prev_entry_scn_, subsequent logs need to be controlled replay
        // replayable_point_scn rollback will not happen, at this time next_min_scn will return prev_entry_scn_ + 1
        replayable_point_scn = SCN::minus(prev_next_success_scn, 100);
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
        EXPECT_EQ(true, iterate_end_by_replayable_point);

        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
        // Simulate logs after prev_entry
        // Push end_lsn_v to the start of the second log
        end_lsn_v = lsns_append[1]+2;
        replayable_point_scn = logts_append[1];
        PALF_LOG(INFO, "runlin trace 8.5", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[1]), K(prev_next_success_scn));
        EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(next_log_min_scn.is_valid(), false);
        prev_next_success_scn = iterator.iterator_impl_.prev_entry_scn_;
        EXPECT_EQ(prev_next_success_scn, logts_append[0]);

        PALF_LOG(INFO, "runlin trace 8.6", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[1]), K(prev_next_success_scn));
        // Simulate the situation where there are logs after prev_entry_, but they are not visible
        // At this point, the second log will not be output due to replayable_point_scn
        // Simulate the situation where replayable_point_scn is before prev_entry_
        replayable_point_scn.convert_for_tx(100);
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);

        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
        // Simulate the situation where replayable_point_scn is after prev_entry_, since there are logs after prev_entry_, therefore
        // there cannot be any unread logs between prev_entry_ and replayable_point_scn,
        // Therefore next_log_min_scn is replayable_point_scn + 1.
        replayable_point_scn = SCN::plus(prev_next_success_scn , 2);
        PALF_LOG(INFO, "runlin trace 8.7", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[1]), K(prev_next_success_scn));
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(replayable_point_scn, 1), next_log_min_scn);

        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(replayable_point_scn, 1), next_log_min_scn);
        // Simulate the situation after curr_entry for replayable_point_scn
        replayable_point_scn.convert_from_ts(ObTimeUtility::current_time() + 100000000);
        PALF_LOG(INFO, "runlin trace 8.8", K(iterator), K(replayable_point_scn), K(end_lsn_v),
          K(logts_append[1]), K(prev_next_success_scn));
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(logts_append[1], next_log_min_scn);

        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(logts_append[1], next_log_min_scn);

        end_lsn_v = LSN(1000000000);
        EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(next_log_min_scn.is_valid(), false);
        EXPECT_EQ(iterator.iterator_impl_.prev_entry_scn_, logts_append[1]);
        prev_next_success_scn = iterator.iterator_impl_.prev_entry_scn_;
        // Validate controlled replay
        replayable_point_scn.convert_for_tx(100);
        EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_log_min_scn, iterate_end_by_replayable_point));
        EXPECT_EQ(SCN::plus(prev_next_success_scn, 1), next_log_min_scn);
        EXPECT_EQ(true, iterate_end_by_replayable_point);
      }
    }
  }
  // Validate restart
  restart_paxos_groups();
  {
    PalfHandleImplGuard raw_write_leader;
    PalfBufferIterator iterator;
    EXPECT_EQ(OB_SUCCESS, get_leader(id, raw_write_leader, leader_idx));
    PalfHandleImpl *raw_write_palf_handle_impl = raw_write_leader.palf_handle_impl_;
    auto get_file_end_lsn = []() -> LSN { return LSN(1000000000); };
    auto get_mode_version = [&mode_version, &mode_version_v]() -> int64_t {
      PALF_LOG(INFO, "runlin trace", K(*mode_version), K(mode_version_v));
      return *mode_version;
    };
    EXPECT_EQ(OB_SUCCESS,
        iterator.init(LSN(0), get_file_end_lsn, get_mode_version, &raw_write_palf_handle_impl->log_engine_.log_storage_));
    SCN max_scn = raw_write_leader.palf_handle_impl_->get_max_scn();
    int64_t count = 5 + 5 + 5 + 5 + 5;
    while (count > 0) {
      EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn));
      count--;
    }
    EXPECT_EQ(OB_ITER_END, iterator.next(max_scn));
    EXPECT_EQ(OB_ITER_END, read_log_from_memory(raw_write_leader));
  }
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_gc_block)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_gc_block");
  OB_LOGGER.set_log_level("TRACE");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  LSN upper_aligned_log_tail;
  PalfHandleImplGuard leader;
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
  LogEngine *log_engine = &leader.palf_handle_impl_->log_engine_;
  LogStorage *log_meta_storage = &log_engine->log_meta_storage_;
  block_id_t min_block_id;
  share::SCN min_block_scn;
  EXPECT_EQ(OB_ENTRY_NOT_EXIST, log_engine->get_min_block_info_for_gc(min_block_id, min_block_scn));
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 31, leader_idx, log_entry_size));
  EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));
  EXPECT_EQ(OB_ERR_OUT_OF_UPPER_BOUND, log_engine->get_min_block_info_for_gc(min_block_id, min_block_scn));
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
  EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));
  block_id_t expect_block_id = 1;
  share::SCN expect_scn;
  EXPECT_EQ(OB_SUCCESS, log_engine->get_min_block_info_for_gc(min_block_id, min_block_scn));
  EXPECT_EQ(OB_SUCCESS, log_engine->get_block_min_scn(expect_block_id, expect_scn));
  EXPECT_EQ(expect_scn, min_block_scn);
  EXPECT_EQ(OB_SUCCESS, log_engine->delete_block(0));
  EXPECT_EQ(false, log_engine->min_block_max_scn_.is_valid());

  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 100, leader_idx, log_entry_size));
  EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));
  expect_block_id = 2;
  EXPECT_EQ(OB_SUCCESS, log_engine->get_min_block_info_for_gc(min_block_id, min_block_scn));
  EXPECT_EQ(OB_SUCCESS, log_engine->get_block_min_scn(expect_block_id, expect_scn));
  EXPECT_EQ(expect_scn, min_block_scn);
  EXPECT_EQ(OB_SUCCESS, log_engine->delete_block(1));
  expect_block_id = 3;
  EXPECT_EQ(OB_SUCCESS, log_engine->get_min_block_info_for_gc(min_block_id, min_block_scn));
  EXPECT_EQ(OB_SUCCESS, log_engine->get_block_min_scn(expect_block_id, expect_scn));
  EXPECT_EQ(expect_scn, min_block_scn);
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_iterator_with_flashback)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_iterator_with_flashback");
  OB_LOGGER.set_log_level("TRACE");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  PalfHandleImplGuard leader;
  PalfHandleImplGuard raw_write_leader;
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
  PalfHandleImpl *palf_handle_impl = leader.palf_handle_impl_;
  const int64_t id_raw_write = ATOMIC_AAF(&palf_id_, 1);
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id_raw_write, leader_idx, raw_write_leader));
  EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(raw_write_leader));

  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, 200));
  SCN max_scn1 = leader.palf_handle_impl_->get_max_scn();
  LSN end_pos_of_log1 = leader.palf_handle_impl_->get_max_lsn();
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, 200));
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
  SCN max_scn2 = leader.palf_handle_impl_->get_max_scn();
  LSN end_pos_of_log2 = leader.palf_handle_impl_->get_max_lsn();
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
  // Submit a few logs to raw_write leader
  EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(leader, raw_write_leader));
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(raw_write_leader, raw_write_leader.palf_handle_impl_->get_max_lsn()));

  PalfBufferIterator iterator;
  EXPECT_EQ(OB_SUCCESS, raw_write_leader.palf_handle_impl_->alloc_palf_buffer_iterator(LSN(0), iterator));
  // Iteration of logs before flashback successful
  SCN next_min_scn;
  SCN tmp_scn; tmp_scn.val_ = 1000;
  bool iterate_end_by_replayable_point = false;
  EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn1, next_min_scn, iterate_end_by_replayable_point));
  EXPECT_EQ(false, iterate_end_by_replayable_point);
  EXPECT_EQ(iterator.iterator_impl_.prev_entry_scn_, max_scn1);
  EXPECT_EQ(OB_ITER_END, iterator.next(
    max_scn1, next_min_scn, iterate_end_by_replayable_point));
  EXPECT_EQ(true, iterate_end_by_replayable_point);
  EXPECT_EQ(end_pos_of_log1, iterator.iterator_impl_.log_storage_->get_lsn(iterator.iterator_impl_.curr_read_pos_));
  EXPECT_EQ(SCN::plus(max_scn1, 1), next_min_scn);
  PALF_LOG(INFO, "runlin trace case1", K(iterator), K(end_pos_of_log1));

  EXPECT_EQ(OB_SUCCESS, raw_write_leader.palf_handle_impl_->inner_flashback(max_scn2));

  EXPECT_EQ(max_scn2, raw_write_leader.palf_handle_impl_->get_max_scn());

  int64_t mode_version;
  switch_flashback_to_append(raw_write_leader, mode_version);
  // There are three logs on the disk, one log has been iterated, another log has not been iterated (raw_write), and the last log is Append
  EXPECT_EQ(OB_SUCCESS, submit_log(raw_write_leader, 1, leader_idx, 333));
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(raw_write_leader, raw_write_leader.palf_handle_impl_->get_max_lsn()));

  EXPECT_EQ(OB_ITER_END, read_group_log(raw_write_leader, LSN(0)));
  SCN max_scn3 = raw_write_leader.palf_handle_impl_->get_max_scn();
  PALF_LOG(INFO, "runlin trace case2", K(iterator), K(max_scn3), "end_lsn:", raw_write_leader.palf_handle_impl_->get_end_lsn());

  LSN iterator_end_lsn = iterator.iterator_storage_.end_lsn_;
  // iterator memory contains how many logs, expect to return success, at this time cache will be cleared, the information of the previous log will be cleared (raw_write log)
  // Iterator cursor is expected to still point to the end of the first log, due to controlled replay, return iterate_end
  EXPECT_EQ(OB_ITER_END, iterator.next(
    max_scn1, next_min_scn, iterate_end_by_replayable_point));
  EXPECT_EQ(end_pos_of_log1, iterator.iterator_impl_.log_storage_->get_lsn(iterator.iterator_impl_.curr_read_pos_));
  EXPECT_EQ(true, iterator.iterator_impl_.curr_entry_is_raw_write_);
  // Need to read the last two logs from the disk, but controlled playback will not output
  // EXPECT_FALSE(iterator_end_lsn == iterator.iterator_storage_.end_lsn_);
  EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn2, next_min_scn, iterate_end_by_replayable_point));
  EXPECT_EQ(false, iterate_end_by_replayable_point);
  EXPECT_EQ(true, iterator.iterator_impl_.curr_entry_is_raw_write_);
  EXPECT_EQ(iterator.iterator_impl_.prev_entry_scn_, max_scn2);

  EXPECT_EQ(OB_SUCCESS, iterator.next(max_scn3, next_min_scn, iterate_end_by_replayable_point));
  EXPECT_EQ(false, iterate_end_by_replayable_point);
  EXPECT_EQ(false, iterator.iterator_impl_.curr_entry_is_raw_write_);
  EXPECT_EQ(iterator.iterator_impl_.prev_entry_scn_, max_scn3);
  // raw_write_leader already has three logs, raw_write(1 log entry), raw_write(1), append(1),
  // Simulate a group entry with multiple small logs
  LSN last_lsn = raw_write_leader.palf_handle_impl_->get_max_lsn();
  SCN last_scn = raw_write_leader.palf_handle_impl_->get_max_scn();

  LogIOWorker *io_worker = raw_write_leader.palf_handle_impl_->log_engine_.log_io_worker_;
  IOTaskCond cond(id_raw_write, raw_write_leader.palf_env_impl_->last_palf_epoch_);
  io_worker->submit_io_task(&cond);
  std::vector<LSN> lsns;
  std::vector<SCN> scns;
  EXPECT_EQ(OB_SUCCESS, submit_log(raw_write_leader, 10, 100, id_raw_write, lsns, scns));
  int group_entry_num = 1;
  int first_log_entry_index = 0, last_log_entry_index = 0;
  for (int i = 1; i < 10; i++) {
    if (lsns[i-1]+100+sizeof(LogEntryHeader) == lsns[i]) {
      last_log_entry_index = i;
    } else {
      first_log_entry_index = i;
      group_entry_num++;
      PALF_LOG(INFO, "group entry", K(i-1));
    }
    if (first_log_entry_index - last_log_entry_index > 2) {
      break;
    }
  }
  leader.reset();
  if (first_log_entry_index != 1 && last_log_entry_index != 9) {
    PALF_LOG(INFO, "no group log has more than 2 log entry", K(first_log_entry_index), K(last_log_entry_index));
    return;
  }

  cond.cond_.signal();
  // Verify flashback from a log containing multiple LogEntries, after the iterator iterates to an intermediate LogEntry, how many LogEntries are before the flashback point
  // LogGroup LogGroup LogGroup LogGroup LogGroup(9 small logs)
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(raw_write_leader, raw_write_leader.palf_handle_impl_->get_max_lsn()));
  {
    const int64_t id_new_raw_write = ATOMIC_AAF(&palf_id_, 1);
    PalfHandleImplGuard new_raw_write_leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id_new_raw_write, leader_idx, new_raw_write_leader));
    EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(new_raw_write_leader));

    EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(raw_write_leader, new_raw_write_leader));

    PalfBufferIterator buff_iterator;
    PalfGroupBufferIterator group_iterator;

    EXPECT_EQ(OB_SUCCESS, new_raw_write_leader.palf_handle_impl_->alloc_palf_buffer_iterator(LSN(0), buff_iterator));
    EXPECT_EQ(OB_SUCCESS, new_raw_write_leader.palf_handle_impl_->alloc_palf_group_buffer_iterator(LSN(0), group_iterator));

    SCN replayable_point_scn(SCN::min_scn());
    // Verify replayable_point_scn is min_scn
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_EQ(OB_ITER_END, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    // replayable_point_scn is the first log - 1
    replayable_point_scn = SCN::minus(max_scn1, 1);
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_EQ(OB_ITER_END, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    // replayable_point_scn is the first log entry
    replayable_point_scn = max_scn1;
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(iterate_end_by_replayable_point, false);
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(iterate_end_by_replayable_point, false);
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    // replayable_point_scn is the first log + 1
    replayable_point_scn = SCN::plus(max_scn1, 1);
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_EQ(OB_ITER_END, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    // Successful iteration of the second log, third log
    replayable_point_scn = last_scn;
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn));
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(replayable_point_scn));
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn));
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(replayable_point_scn));
    // The fourth log must be a LogGroupEntry
    replayable_point_scn = scns[0];
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn));
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(replayable_point_scn));
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    // Iterate the first LogEntry of the fifth LogGroupEntry
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_NE(buff_iterator.iterator_impl_.log_storage_->get_lsn(
      buff_iterator.iterator_impl_.curr_read_pos_), lsns[1]);

    EXPECT_EQ(OB_ITER_END, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_NE(group_iterator.iterator_impl_.log_storage_->get_lsn(
      group_iterator.iterator_impl_.curr_read_pos_), lsns[1]);
    // Due to controlled replay, buff_iterator and group_iterator have not advanced curr_read_pos_
    EXPECT_EQ(group_iterator.iterator_impl_.log_storage_->get_lsn(
      group_iterator.iterator_impl_.curr_read_pos_),
              buff_iterator.iterator_impl_.log_storage_->get_lsn(
      buff_iterator.iterator_impl_.curr_read_pos_));
    // Successfully iterated the first LogEntry of the fifth LogGroupEntry
    replayable_point_scn = scns[1];
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_EQ(buff_iterator.iterator_impl_.log_storage_->get_lsn(
      buff_iterator.iterator_impl_.curr_read_pos_), lsns[1]);
    // group iterator is controlled replay, but since the max_scn of the fifth log is greater than the controlled replay point, hence controlled replay
    EXPECT_EQ(OB_ITER_END, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    // Since the min scn and replayable_point_scn of the group entry for controlled replay are the same, next_min_scn will be set to replayable_point_scn
    EXPECT_EQ(next_min_scn, replayable_point_scn);
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, scns[0]);
    EXPECT_NE(group_iterator.iterator_impl_.log_storage_->get_lsn(
      group_iterator.iterator_impl_.curr_read_pos_), lsns[1]);
    // Due to the first LogEntry being controlled replay, group_iterator did not advance curr_read_pos_, but buff_iter advanced curr_read_pos_
    EXPECT_NE(group_iterator.iterator_impl_.log_storage_->get_lsn(
      group_iterator.iterator_impl_.curr_read_pos_),
              buff_iterator.iterator_impl_.log_storage_->get_lsn(
      buff_iterator.iterator_impl_.curr_read_pos_));
    // buff_iterator's cursor is at the first small log of the fifth group_entry
    // grou_iterator's cursor is at the beginning of the fifth group_entry
    // sncs[0] fourth group entry, scns[1] - scns[9] are the second
    // The fifth small log of the fifth group entry was flashback
    EXPECT_EQ(OB_SUCCESS, new_raw_write_leader.palf_handle_impl_->inner_flashback(scns[4]));
    EXPECT_EQ(new_raw_write_leader.palf_handle_impl_->get_max_scn(), scns[4]);
    EXPECT_EQ(OB_SUCCESS, change_access_mode_to_append(new_raw_write_leader));
    // submit a group_entry
    // For buff_iterator, there are two unread group_entry, one raw_rwrite (including 4 small logs, cursor stops at the end of the first small log), one append
    // For group_iterator, there are three unread group_entries, one raw_rwrite (containing 4 small logs, cursor is at the head of group_entry), one append
    EXPECT_EQ(OB_SUCCESS, submit_log(new_raw_write_leader, 1, leader_idx, 100));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(new_raw_write_leader, new_raw_write_leader.palf_handle_impl_->get_max_lsn()));
    // For buff_iterator
    // lsns[2] is the start of the second small log, i.e., the end of the first small log
    // Verify the cursor starting position is at the header of the first small log
    // next returns whether iterate clears the cache
    // Iterate over small logs written by raw_write
    // iteration append write small log
    PALF_LOG(INFO, "rulin trace 1", K(lsns[2]), K(lsns[1]), K(lsns[0]), K(buff_iterator));
    EXPECT_EQ(buff_iterator.iterator_impl_.log_storage_->get_lsn(buff_iterator.iterator_impl_.curr_read_pos_), lsns[1]);
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(SCN::min_scn(), next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(true, iterate_end_by_replayable_point);
    EXPECT_EQ(next_min_scn, SCN::plus(buff_iterator.iterator_impl_.prev_entry_scn_, 1));
    EXPECT_EQ(0, buff_iterator.iterator_impl_.curr_read_pos_);
    // Iterate the second log
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(SCN::max_scn()));
    // Iterate third log
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(SCN::max_scn()));
    // Iterate fourth log
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(SCN::max_scn()));
    // Iterate fifth log (iterate new GroupEntry, non-controlled replay)
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(SCN::min_scn()));
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(SCN::min_scn()));
    // For group_iterator
    // Verify that the cursor start position is at the beginning of the raw_write log
    // next returns whether iterate clears the cache
    // Iterate over large logs written by raw_write
    // iteration append write large log
    PALF_LOG(INFO, "rulin trace 2", K(lsns[2]), K(lsns[1]), K(lsns[0]), K(group_iterator));
    EXPECT_EQ(group_iterator.iterator_impl_.log_storage_->get_lsn(group_iterator.iterator_impl_.curr_read_pos_), lsns[1] - sizeof(LogGroupEntryHeader));
    EXPECT_EQ(OB_ITER_END, group_iterator.next(SCN::min_scn(), next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(true, iterate_end_by_replayable_point);
    EXPECT_EQ(next_min_scn, SCN::plus(group_iterator.iterator_impl_.prev_entry_scn_, 1));
    // Iterate raw_write log
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(SCN::max_scn()));
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(SCN::max_scn()));
    EXPECT_EQ(OB_ITER_END, group_iterator.next(SCN::max_scn()));
  }
  // Verify flashback from a log containing multiple LogEntries, after the iterator iterates to an intermediate LogEntry, there are no LogEntries before the flashback point
  // LogGroup LogGroup LogGroup LogGroup LogGroup(9 small logs)
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(raw_write_leader, raw_write_leader.palf_handle_impl_->get_max_lsn()));
  {
    const int64_t id_new_raw_write = ATOMIC_AAF(&palf_id_, 1);
    PalfHandleImplGuard new_raw_write_leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id_new_raw_write, leader_idx, new_raw_write_leader));
    EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(new_raw_write_leader));

    EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(raw_write_leader, new_raw_write_leader));

    PalfBufferIterator buff_iterator;
    PalfGroupBufferIterator group_iterator;

    EXPECT_EQ(OB_SUCCESS, new_raw_write_leader.palf_handle_impl_->alloc_palf_buffer_iterator(LSN(0), buff_iterator));
    EXPECT_EQ(OB_SUCCESS, new_raw_write_leader.palf_handle_impl_->alloc_palf_group_buffer_iterator(LSN(0), group_iterator));
    // Successfully iterated the first log, second log, third log
    SCN replayable_point_scn(last_scn);
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));

    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn));
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(replayable_point_scn));

    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn));
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(replayable_point_scn));
    // The fourth log must be a LogGroupEntry
    replayable_point_scn = scns[0];
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn));
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(replayable_point_scn));
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    // Iterate the first LogEntry of the fifth LogGroupEntry
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_NE(buff_iterator.iterator_impl_.log_storage_->get_lsn(
      buff_iterator.iterator_impl_.curr_read_pos_), lsns[1]);

    EXPECT_EQ(OB_ITER_END, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(next_min_scn, SCN::plus(replayable_point_scn, 1));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_NE(group_iterator.iterator_impl_.log_storage_->get_lsn(
      group_iterator.iterator_impl_.curr_read_pos_), lsns[1]);
    // Due to controlled replay, buff_iterator and group_iterator have not advanced curr_read_pos_
    EXPECT_EQ(group_iterator.iterator_impl_.log_storage_->get_lsn(
      group_iterator.iterator_impl_.curr_read_pos_),
              buff_iterator.iterator_impl_.log_storage_->get_lsn(
      buff_iterator.iterator_impl_.curr_read_pos_));
    // Successfully iterated the first LogEntry of the fifth LogGroupEntry
    replayable_point_scn = scns[1];
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(buff_iterator.iterator_impl_.prev_entry_scn_, replayable_point_scn);
    EXPECT_EQ(buff_iterator.iterator_impl_.log_storage_->get_lsn(
      buff_iterator.iterator_impl_.curr_read_pos_), lsns[1]);
    // group iterator is controlled replay, but since the max_scn of the fifth log is greater than the controlled replay point, hence controlled replay
    EXPECT_EQ(OB_ITER_END, group_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    // Since the min scn and replayable_point_scn of the group entry for controlled replay are the same, next_min_scn will be set to replayable_point_scn
    EXPECT_EQ(next_min_scn, replayable_point_scn);
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(group_iterator.iterator_impl_.prev_entry_scn_, scns[0]);
    EXPECT_NE(group_iterator.iterator_impl_.log_storage_->get_lsn(
      group_iterator.iterator_impl_.curr_read_pos_), lsns[1]);
    // Due to the first LogEntry being controlled replay, group_iterator did not advance curr_read_pos_, but buff_iter advanced curr_read_pos_
    EXPECT_NE(group_iterator.iterator_impl_.log_storage_->get_lsn(
      group_iterator.iterator_impl_.curr_read_pos_),
              buff_iterator.iterator_impl_.log_storage_->get_lsn(
      buff_iterator.iterator_impl_.curr_read_pos_));
    // Iteration log discovery requires controlled replay
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(scns[1], next_min_scn, iterate_end_by_replayable_point));
    // buff_iterator's cursor reached the end of the first small log in the fifth group_entry
    // grou_iterator's cursor is at the beginning of the fifth group_entry
    // sncs[0] fourth group entry, scns[1] - scns[9] are the second
    // The second small log of the fifth group entry was flashback
    EXPECT_EQ(OB_SUCCESS, new_raw_write_leader.palf_handle_impl_->inner_flashback(scns[2]));
    EXPECT_EQ(new_raw_write_leader.palf_handle_impl_->get_max_scn(), scns[2]);
    EXPECT_EQ(OB_SUCCESS, change_access_mode_to_append(new_raw_write_leader));
    // submit a group_entry
    // For buff_iterator, there are two unread group_entry, one raw_rwrite (including 4 small logs, cursor stops at the end of the first small log), one append
    // For group_iterator, there are three unread group_entries, one raw_rwrite (containing 4 small logs, cursor is at the head of group_entry), one append
    EXPECT_EQ(OB_SUCCESS, submit_log(new_raw_write_leader, 1, leader_idx, 100));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(new_raw_write_leader, new_raw_write_leader.palf_handle_impl_->get_max_lsn()));
    // For buff_iterator
    // lsns[2] is the start of the second small log, i.e., the end of the first small log
    // Verify the cursor starting position is at the header of the first small log
    // next returns whether iterate clears the cache
    // Iterate over small logs written by raw_write
    // iteration append write small log
    PALF_LOG(INFO, "rulin trace 3", K(lsns[2]), K(lsns[1]), K(lsns[0]), K(buff_iterator));
    EXPECT_EQ(buff_iterator.iterator_impl_.log_storage_->get_lsn(buff_iterator.iterator_impl_.curr_read_pos_), lsns[2]);
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(SCN::min_scn(), next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(true, iterate_end_by_replayable_point);
    EXPECT_EQ(next_min_scn, SCN::plus(buff_iterator.iterator_impl_.prev_entry_scn_, 1));
    EXPECT_EQ(0, buff_iterator.iterator_impl_.curr_read_pos_);
    // Iterate the second small log
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(SCN::max_scn()));
    // Iterate over newly written LogGroupEntry, no controlled replay needed
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(SCN::min_scn()));

    EXPECT_EQ(OB_ITER_END, buff_iterator.next(SCN::min_scn()));
    // For group_iterator
    // Verify that the cursor starting position is at the beginning of the raw_write log
    // next returns whether iterate clears the cache
    // Iterate over large logs written by raw_write
    // iteration append write large log
    PALF_LOG(INFO, "rulin trace 4", K(lsns[2]), K(lsns[1]), K(lsns[0]), K(group_iterator));
    EXPECT_EQ(group_iterator.iterator_impl_.log_storage_->get_lsn(group_iterator.iterator_impl_.curr_read_pos_), lsns[1] - sizeof(LogGroupEntryHeader));
    EXPECT_EQ(OB_ITER_END, group_iterator.next(SCN::min_scn(), next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(true, iterate_end_by_replayable_point);
    EXPECT_EQ(next_min_scn, SCN::plus(group_iterator.iterator_impl_.prev_entry_scn_, 1));
    // Iterate raw_write log
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(SCN::max_scn()));
    // Iterate new GroupEntry
    EXPECT_EQ(OB_SUCCESS, group_iterator.next(SCN::min_scn()));
    EXPECT_EQ(OB_ITER_END, group_iterator.next(SCN::min_scn()));
  }
  // Verify that a LogGroupEntry requires controlled replay, buff iterator should not update accumulate_checksum and curr_read_pos_
  // LogGroup LogGroup LogGroup LogGroup LogGroup(9 small logs)
  //                   last_scn scns[0]  scns[1]...
  {
    const int64_t id_new_raw_write = ATOMIC_AAF(&palf_id_, 1);
    PalfHandleImplGuard new_raw_write_leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id_new_raw_write, leader_idx, new_raw_write_leader));
    EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(new_raw_write_leader));
    EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(raw_write_leader, new_raw_write_leader));
    PalfBufferIterator iterator;
    EXPECT_EQ(OB_SUCCESS, new_raw_write_leader.palf_handle_impl_->alloc_palf_buffer_iterator(LSN(0), iterator));
    SCN replayable_point_scn(last_scn);

    EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn));
    EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn));
    EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn));

    EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(iterate_end_by_replayable_point, true);
    EXPECT_EQ(next_min_scn, SCN::plus(last_scn, 1));

    replayable_point_scn = scns[0];
    EXPECT_EQ(OB_SUCCESS, iterator.next(replayable_point_scn));
    // The log corresponding to scns[1] cannot be output
    EXPECT_EQ(OB_ITER_END, iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(next_min_scn, SCN::plus(scns[0], 1));
    EXPECT_EQ(iterator.iterator_impl_.prev_entry_scn_, scns[0]);
    // flashback to scns[0]
    EXPECT_EQ(OB_SUCCESS, new_raw_write_leader.palf_handle_impl_->inner_flashback(scns[0]));
    EXPECT_EQ(OB_SUCCESS, change_access_mode_to_append(new_raw_write_leader));
    EXPECT_EQ(OB_SUCCESS, submit_log(new_raw_write_leader, 1, leader_idx, 100));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(new_raw_write_leader, new_raw_write_leader.palf_handle_impl_->get_max_lsn()));
    // scns[0] corresponding log is raw write, was flashbacked, iterator stops at the end of scns[0]
    // Iteration of newly written logs successful
    EXPECT_EQ(OB_SUCCESS, iterator.next(SCN::min_scn(), next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(OB_ITER_END, iterator.next(SCN::min_scn()));
  }
  // Validate that a padding LogGroupEntry requires controlled replay
  {
    const int64_t append_id = ATOMIC_AAF(&palf_id_, 1);
    PalfHandleImplGuard append_leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(append_id, leader_idx, append_leader));
    EXPECT_EQ(OB_SUCCESS, submit_log(append_leader, 31, leader_idx, log_entry_size));
    const LSN padding_start_lsn = append_leader.get_palf_handle_impl()->get_max_lsn();
    EXPECT_EQ(OB_SUCCESS, submit_log(append_leader, 1, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(append_leader, append_leader.get_palf_handle_impl()->get_max_lsn()));
    SCN padding_scn = append_leader.get_palf_handle_impl()->get_max_scn();
    padding_scn = padding_scn.minus(padding_scn, 1);

    const int64_t raw_write_id = ATOMIC_AAF(&palf_id_, 1);
    PalfHandleImplGuard raw_write_leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(raw_write_id, leader_idx, raw_write_leader));
    EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(raw_write_leader));
    EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(append_leader, raw_write_leader));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(raw_write_leader, raw_write_leader.get_palf_handle_impl()->get_max_lsn()));
    switch_append_to_flashback(raw_write_leader, mode_version);

    PalfBufferIterator buff_iterator;
    PalfGroupBufferIterator group_buff_iterator;
    PalfBufferIterator buff_iterator_padding_start;
    PalfGroupBufferIterator group_buff_iterator_padding_start;
    EXPECT_EQ(OB_SUCCESS, raw_write_leader.get_palf_handle_impl()->alloc_palf_buffer_iterator(LSN(0), buff_iterator));
    EXPECT_EQ(OB_SUCCESS, raw_write_leader.get_palf_handle_impl()->alloc_palf_group_buffer_iterator(LSN(0), group_buff_iterator));
    EXPECT_EQ(OB_SUCCESS, raw_write_leader.get_palf_handle_impl()->alloc_palf_buffer_iterator(LSN(0), buff_iterator_padding_start));
    EXPECT_EQ(OB_SUCCESS, raw_write_leader.get_palf_handle_impl()->alloc_palf_group_buffer_iterator(LSN(0), group_buff_iterator_padding_start));
    SCN next_min_scn;
    bool iterate_end_by_replayable_point = false;
    EXPECT_EQ(OB_ITER_END, buff_iterator.next(share::SCN::min_scn(), next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(true, iterate_end_by_replayable_point);
    EXPECT_EQ(OB_ITER_END, group_buff_iterator.next(share::SCN::min_scn(), next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(true, iterate_end_by_replayable_point);
    // There are a total of 33 logs, including padding
    SCN replayable_point_scn = padding_scn.minus(padding_scn, 1);
    // Until padding log controlled replay
    int ret = OB_SUCCESS;
    while (OB_SUCC(buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point))) {
    }
    ret = OB_SUCCESS;
    while (OB_SUCC(buff_iterator_padding_start.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point))) {
    }
    EXPECT_EQ(OB_ITER_END, ret);
    EXPECT_EQ(true, iterate_end_by_replayable_point);
    EXPECT_EQ(next_min_scn, padding_scn);
    ret = OB_SUCCESS;
    while (OB_SUCC(group_buff_iterator.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point))) {
    }
    ret = OB_SUCCESS;
    while (OB_SUCC(group_buff_iterator_padding_start.next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point))) {
    }
    EXPECT_EQ(OB_ITER_END, ret);
    EXPECT_EQ(true, iterate_end_by_replayable_point);
    EXPECT_EQ(next_min_scn, padding_scn);

    EXPECT_EQ(false, buff_iterator.iterator_impl_.curr_entry_is_padding_);
    EXPECT_EQ(false, group_buff_iterator.iterator_impl_.curr_entry_is_padding_);
    // flashback to padding log tail
    EXPECT_EQ(OB_SUCCESS, raw_write_leader.get_palf_handle_impl()->flashback(mode_version, padding_scn, timeout_ts_us));
    EXPECT_EQ(OB_SUCCESS, buff_iterator.next(padding_scn, next_min_scn, iterate_end_by_replayable_point));
    LogEntry padding_log_entry;
    LSN padding_log_lsn;
    EXPECT_EQ(OB_SUCCESS, buff_iterator.get_entry(padding_log_entry, padding_log_lsn));
    EXPECT_EQ(true, padding_log_entry.check_integrity());
    EXPECT_EQ(true, padding_log_entry.header_.is_padding_log_());
    EXPECT_EQ(padding_scn, padding_log_entry.header_.scn_);
    EXPECT_EQ(false, buff_iterator.iterator_impl_.padding_entry_scn_.is_valid());

    EXPECT_EQ(OB_SUCCESS, group_buff_iterator.next(padding_scn, next_min_scn, iterate_end_by_replayable_point));
    LogGroupEntry padding_group_entry;
    LSN padding_group_lsn;
    EXPECT_EQ(OB_SUCCESS, group_buff_iterator.get_entry(padding_group_entry, padding_group_lsn));
    EXPECT_EQ(true, padding_group_entry.check_integrity());
    EXPECT_EQ(true, padding_group_entry.header_.is_padding_log());
    // For the iterator of LogGruopEntry, after construct_padding_log_entry_, the padding state will not be reset
    EXPECT_EQ(true, group_buff_iterator.iterator_impl_.padding_entry_scn_.is_valid());
    EXPECT_EQ(padding_log_entry.header_.scn_, padding_group_entry.header_.max_scn_);
    // flashback to padding log header
    EXPECT_EQ(OB_SUCCESS, raw_write_leader.get_palf_handle_impl()->flashback(mode_version, padding_scn.minus(padding_scn, 1), timeout_ts_us));
    // The expectation is that OB_ITER_END is caused by the file length
    EXPECT_EQ(OB_ITER_END, buff_iterator_padding_start.next(padding_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(false, iterate_end_by_replayable_point);
    EXPECT_GE(next_min_scn, buff_iterator_padding_start.iterator_impl_.prev_entry_scn_);
    EXPECT_EQ(OB_ITER_END, group_buff_iterator_padding_start.next(padding_scn, next_min_scn, iterate_end_by_replayable_point));
    EXPECT_EQ(false, iterate_end_by_replayable_point);
    EXPECT_GE(next_min_scn, group_buff_iterator_padding_start.iterator_impl_.prev_entry_scn_);
    switch_flashback_to_append(raw_write_leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, submit_log(raw_write_leader, 100, leader_idx, 1000));
    EXPECT_EQ(OB_SUCCESS, buff_iterator_padding_start.next());
    EXPECT_EQ(OB_SUCCESS, group_buff_iterator_padding_start.next());
  }
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_raw_read)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_raw_read");
  OB_LOGGER.set_log_level("TRACE");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  PalfHandleImplGuard leader;
  const int64_t read_buf_ptr_len = PALF_BLOCK_SIZE;
  char *read_buf_ptr = reinterpret_cast<char*>(mtl_malloc_align(
    LOG_DIO_ALIGN_SIZE, PALF_BLOCK_SIZE + 2 * LOG_DIO_ALIGN_SIZE, "mittest"));
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
  PalfOptions opts;
  PalfEnvImpl *palf_env_impl = dynamic_cast<palf::PalfEnvImpl*>(get_cluster()[0]->get_palf_env());
  ASSERT_NE(nullptr, palf_env_impl);
  palf_env_impl->get_options(opts);
  opts.enable_log_cache_ = true;
  palf_env_impl->update_options(opts);
  // Submit 100 logs, each log size is 30K.
  {
    char *read_buf = read_buf_ptr;
    int64_t nbytes = read_buf_ptr_len;
    int64_t out_read_size = 0;
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 100, leader_idx, 1000));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
    const int64_t curr_real_size = leader.palf_handle_impl_->get_max_lsn() - LSN(PALF_INITIAL_LSN_VAL);

    const LSN invalid_lsn(1);
    char *invalid_read_buf = read_buf_ptr + 1;
    const int64_t invalid_nbytes = 1;
    // Non-DIO alignment
    palf::LogIOContext io_ctx(palf::LogIOUser::META_INFO);
    EXPECT_EQ(OB_INVALID_ARGUMENT, leader.palf_handle_impl_->raw_read(
      invalid_lsn, invalid_read_buf, invalid_nbytes, out_read_size, io_ctx));
    EXPECT_EQ(OB_INVALID_ARGUMENT, leader.palf_handle_impl_->raw_read(
      LSN(PALF_INITIAL_LSN_VAL), invalid_read_buf, invalid_nbytes, out_read_size, io_ctx));
    EXPECT_EQ(OB_INVALID_ARGUMENT, leader.palf_handle_impl_->raw_read(
      invalid_lsn, read_buf, invalid_nbytes, out_read_size, io_ctx));
    EXPECT_EQ(OB_INVALID_ARGUMENT, leader.palf_handle_impl_->raw_read(
      invalid_lsn, invalid_read_buf, nbytes, out_read_size, io_ctx));
    EXPECT_EQ(OB_INVALID_ARGUMENT, leader.palf_handle_impl_->raw_read(
      LSN(PALF_INITIAL_LSN_VAL), read_buf, invalid_nbytes, out_read_size, io_ctx));
    PALF_LOG(INFO, "raw read success");
    // Read successful
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->raw_read(LSN(PALF_INITIAL_LSN_VAL), read_buf, PALF_BLOCK_SIZE, out_read_size, io_ctx));
    EXPECT_LE(out_read_size, PALF_BLOCK_SIZE);
    EXPECT_EQ(out_read_size, curr_real_size);
    // Read length exceeds end_lsn
    PALF_LOG(INFO, "raw read return OB_ERR_OUT_OF_UPPER_BOUND");
    LSN out_of_upper_bound(PALF_BLOCK_SIZE);
    EXPECT_EQ(OB_ERR_OUT_OF_UPPER_BOUND, leader.palf_handle_impl_->raw_read(
      out_of_upper_bound, read_buf, PALF_BLOCK_SIZE, out_read_size, io_ctx));
    // Simulate generating 2 files
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 40, leader_idx, MAX_LOG_BODY_SIZE));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
    // Simulate cross-file read
    PALF_LOG(INFO, "raw read cross file");
    LSN curr_read_lsn(lower_align(PALF_BLOCK_SIZE/2, LOG_DIO_ALIGN_SIZE));
    int64_t expected_read_size = LSN(PALF_BLOCK_SIZE) - curr_read_lsn;
    io_ctx.iterator_info_.allow_filling_cache_ = false;
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->raw_read(
      curr_read_lsn, read_buf, PALF_BLOCK_SIZE, out_read_size, io_ctx));
    EXPECT_EQ(out_read_size, expected_read_size);

    //io_ctx.set_allow_filling_cache(true);
    //EXPECT_EQ(OB_BUF_NOT_ENOUGH, leader.palf_handle_impl_->raw_read(
    //  curr_read_lsn, read_buf, expected_read_size, out_read_size, io_ctx));

    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->delete_block(0));
    // simulate lower_bound
    PALF_LOG(INFO, "raw read return OB_ERR_OUT_OF_LOWER_BOUND");
    LSN out_of_lower_bound(PALF_INITIAL_LSN_VAL);
    EXPECT_EQ(OB_ERR_OUT_OF_LOWER_BOUND, leader.palf_handle_impl_->raw_read(out_of_lower_bound, read_buf, PALF_BLOCK_SIZE, out_read_size, io_ctx));
    if (NULL != read_buf_ptr) {
      mtl_free_align(read_buf_ptr);
    }
  }
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_iow_memleak)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_iow");
  OB_LOGGER.set_log_level("INFO");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  // case1: palf epoch has been changed during do_task
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    LogIOWorker *iow = leader.get_palf_handle_impl()->log_engine_.log_io_worker_;
    IPalfEnvImpl *palf_env_impl = leader.get_palf_handle_impl()->palf_env_impl_;
    ObILogAllocator *allocator = palf_env_impl->get_log_allocator();
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 32, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn()));
    LSN end_lsn = leader.get_palf_handle_impl()->get_end_lsn();
    
    IOTaskCond cond(id, leader.palf_env_impl_->last_palf_epoch_);
    EXPECT_EQ(OB_SUCCESS, iow->submit_io_task(&cond));
    sleep(1);
    EXPECT_EQ(OB_SUCCESS, leader.get_palf_handle_impl()->set_base_lsn(end_lsn));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, leader.get_palf_handle_impl()->log_engine_.submit_purge_throttling_task(PurgeThrottlingType::PURGE_BY_GET_MC_REQ));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 2, leader_idx, log_entry_size));
    EXPECT_NE(0, allocator->flying_log_task_);
    EXPECT_NE(0, allocator->flying_meta_task_);
    leader.get_palf_handle_impl()->log_engine_.palf_epoch_++;
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, leader_idx));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, leader_idx));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    leader.get_palf_handle_impl()->log_engine_.palf_epoch_++;
    cond.cond_.signal();
    PALF_LOG(INFO, "runlin trace submit log 1");
    while (iow->queue_.size() > 0) {
      PALF_LOG(INFO, "queue size is not zero", "size", iow->queue_.size());
      sleep(1);
    }
    EXPECT_EQ(0, allocator->flying_log_task_);
    EXPECT_EQ(0, allocator->flying_meta_task_);
  }
  delete_paxos_group(id);

  // case2: palf epoch has been changed during after_consume 
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    LogIOWorker *iow = leader.get_palf_handle_impl()->log_engine_.log_io_worker_;
    IPalfEnvImpl *palf_env_impl = leader.get_palf_handle_impl()->palf_env_impl_;
    ObILogAllocator *allocator = palf_env_impl->get_log_allocator();
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 32, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn()));
    LSN end_lsn = leader.get_palf_handle_impl()->get_end_lsn();
    IOTaskConsumeCond consume_cond(id, leader.palf_env_impl_->last_palf_epoch_);
    EXPECT_EQ(OB_SUCCESS, iow->submit_io_task(&consume_cond));
    sleep(1);
    EXPECT_EQ(OB_SUCCESS, leader.get_palf_handle_impl()->set_base_lsn(end_lsn));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, leader.get_palf_handle_impl()->log_engine_.submit_purge_throttling_task(PurgeThrottlingType::PURGE_BY_GET_MC_REQ));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 2, leader_idx, log_entry_size));
    EXPECT_NE(0, allocator->flying_log_task_);
    EXPECT_NE(0, allocator->flying_meta_task_);
    leader.get_palf_handle_impl()->log_engine_.palf_epoch_++;
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, leader_idx));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, leader_idx));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    leader.get_palf_handle_impl()->log_engine_.palf_epoch_++;
    consume_cond.cond_.signal();
    PALF_LOG(INFO, "runlin trace submit log 2");
    IOTaskVerify verify(id, leader.get_palf_handle_impl()->log_engine_.palf_epoch_);
    EXPECT_EQ(OB_SUCCESS, iow->submit_io_task(&verify));
    while (verify.count_ == 0) {
      PALF_LOG(INFO, "queue size is not zero", "size", iow->queue_.size());
      sleep(1);
    }
    EXPECT_EQ(0, allocator->flying_log_task_);
    EXPECT_EQ(0, allocator->flying_meta_task_);
  }
  delete_paxos_group(id);
  // case3: palf epoch has been changed during do_task when there is no io reduce
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    LogIOWorker *iow = leader.get_palf_handle_impl()->log_engine_.log_io_worker_;
    IPalfEnvImpl *palf_env_impl = leader.get_palf_handle_impl()->palf_env_impl_;
    bool need_stop = false;
    std::thread throttling_th([palf_env_impl, &need_stop](){
      PalfEnvImpl *impl = dynamic_cast<PalfEnvImpl*>(palf_env_impl);
      while (!need_stop) {
        impl->log_io_worker_wrapper_.notify_need_writing_throttling(true);
        usleep(1000);
      }
    });
    ObILogAllocator *allocator = palf_env_impl->get_log_allocator();
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 32, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->sw_.get_max_lsn()));
    LSN end_lsn = leader.get_palf_handle_impl()->get_end_lsn();
    // case2: palf epoch has been changed during after_consume 
    IOTaskConsumeCond consume_cond(id, leader.palf_env_impl_->last_palf_epoch_);
    EXPECT_EQ(OB_SUCCESS, iow->submit_io_task(&consume_cond));
    sleep(3);
    EXPECT_EQ(OB_SUCCESS, leader.get_palf_handle_impl()->set_base_lsn(end_lsn));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, leader.get_palf_handle_impl()->log_engine_.submit_purge_throttling_task(PurgeThrottlingType::PURGE_BY_GET_MC_REQ));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 2, leader_idx, log_entry_size));
    EXPECT_NE(0, allocator->flying_log_task_);
    EXPECT_NE(0, allocator->flying_meta_task_);
    leader.get_palf_handle_impl()->log_engine_.palf_epoch_++;
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, leader_idx));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, leader_idx));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, log_entry_size));
    leader.get_palf_handle_impl()->log_engine_.palf_epoch_++;
    consume_cond.cond_.signal();
    PALF_LOG(INFO, "runlin trace submit log 3");
    IOTaskVerify verify(id, leader.get_palf_handle_impl()->log_engine_.palf_epoch_);
    EXPECT_EQ(OB_SUCCESS, iow->submit_io_task(&verify));
    while (verify.count_ == 0) {
      PALF_LOG(INFO, "queue size is not zero", "size", iow->queue_.size());
      sleep(1);
    }
    EXPECT_EQ(0, allocator->flying_log_task_);
    EXPECT_EQ(0, allocator->flying_meta_task_);
    need_stop = true;
    throttling_th.join();
  }
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_log_service_interface)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_log_service_interface");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  ObSimpleLogServer *log_server = dynamic_cast<ObSimpleLogServer*>(get_cluster()[0]);
  ASSERT_NE(nullptr, log_server);
  ObLogService *log_service = &log_server->log_service_;
  ObTenantRole tenant_role; tenant_role.value_ = ObTenantRole::Role::PRIMARY_TENANT;
  PalfBaseInfo palf_base_info; palf_base_info.generate_by_default();
  ObLogHandler log_handler; ObLogRestoreHandler restore_handler;
  ObLogApplyService *apply_service = &log_service->apply_service_;
  ObReplicaType replica_type;
  ObLSID ls_id(id);
  ObApplyStatus *apply_status = nullptr;
  ASSERT_NE(nullptr, apply_status = static_cast<ObApplyStatus*>(mtl_malloc(sizeof(ObApplyStatus), "mittest")));
  new (apply_status) ObApplyStatus();
  apply_status->inc_ref();
  EXPECT_EQ(OB_SUCCESS, log_service->start());
  EXPECT_EQ(OB_SUCCESS, apply_service->apply_status_map_.insert(ls_id, apply_status));
  apply_service->is_running_ = true;
  EXPECT_EQ(OB_ENTRY_EXIST, log_service->create_ls(ls_id, REPLICA_TYPE_FULL, tenant_role, palf_base_info, true, log_handler, restore_handler));
  bool is_exist = false;
  EXPECT_EQ(OB_SUCCESS, log_service->check_palf_exist(ls_id, is_exist));
  EXPECT_EQ(is_exist, false);
  EXPECT_EQ(OB_ENTRY_NOT_EXIST, apply_service->apply_status_map_.erase(ls_id));
  EXPECT_EQ(OB_SUCCESS, log_service->create_ls(ls_id, REPLICA_TYPE_FULL, tenant_role, palf_base_info, true, log_handler, restore_handler));
  EXPECT_EQ(OB_ENTRY_EXIST, log_service->create_ls(ls_id, REPLICA_TYPE_FULL, tenant_role, palf_base_info, true, log_handler, restore_handler));
  EXPECT_EQ(OB_SUCCESS, log_service->check_palf_exist(ls_id, is_exist));
  EXPECT_EQ(is_exist, true);
  const char *log_dir = log_service->palf_env_->palf_env_impl_.log_dir_;
  bool result = false;
  EXPECT_EQ(OB_SUCCESS, FileDirectoryUtils::is_empty_directory(log_dir, result));
  EXPECT_EQ(false, result);
  EXPECT_EQ(OB_SUCCESS, log_service->remove_ls(ls_id, log_handler, restore_handler));
  EXPECT_EQ(OB_SUCCESS, log_service->check_palf_exist(ls_id, is_exist));
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_flashback_concurrent_with_read)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_flashback_concurrent_with_read");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  OB_LOGGER.set_log_level("TRACE");
  int64_t leader_idx = 0;
  PalfHandleImplGuard leader;
  PalfHandleImplGuard raw_write_leader;
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
  PalfHandleImpl *palf_handle_impl = leader.palf_handle_impl_;
  const int64_t id_raw_write = ATOMIC_AAF(&palf_id_, 1);
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id_raw_write, leader_idx, raw_write_leader));
  EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(raw_write_leader));

  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10, leader_idx, MAX_LOG_BODY_SIZE));
  SCN max_scn1 = leader.palf_handle_impl_->get_max_scn();
  LSN end_pos_of_log1 = leader.palf_handle_impl_->get_max_lsn();
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, leader_idx, MAX_LOG_BODY_SIZE));
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
  ObSimpleLogServer *log_server = dynamic_cast<ObSimpleLogServer*>(get_cluster()[0]);
  EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(leader, raw_write_leader));
  std::thread flashback_thread([&]() {
    int64_t mode_version;
    ObTenantEnv::set_tenant(log_server->tenant_base_);
    switch_append_to_flashback(raw_write_leader, mode_version);
    EXPECT_EQ(OB_SUCCESS, raw_write_leader.get_palf_handle_impl()->flashback(mode_version, max_scn1 , timeout_ts_us));
  });
  std::thread read_thread([&]() {
    ObTenantEnv::set_tenant(log_server->tenant_base_);
    EXPECT_NE(OB_INVALID_DATA, read_log(raw_write_leader));
  });
  flashback_thread.join();
  read_thread.join();
}

TEST_F(TestObSimpleLogClusterSingleReplica, test_raw_write_concurrent_lsn)
{
  SET_CASE_LOG_FILE(TEST_NAME, "test_raw_write_concurrent_lsn");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  OB_LOGGER.set_log_level("TRACE");
  int64_t leader_idx = 0;
  PalfHandleImplGuard leader;
  PalfHandleImplGuard raw_write_leader;
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
  PalfHandleImpl *palf_handle_impl = leader.palf_handle_impl_;
  const int64_t id_raw_write = ATOMIC_AAF(&palf_id_, 1);
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id_raw_write, leader_idx, raw_write_leader));
  EXPECT_EQ(OB_SUCCESS, change_access_mode_to_raw_write(raw_write_leader));

  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 100, leader_idx, MAX_LOG_BASE_TYPE));
  SCN max_scn1 = leader.palf_handle_impl_->get_max_scn();
  LSN end_pos_of_log1 = leader.palf_handle_impl_->get_max_lsn();
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
  ObSimpleLogServer *log_server = dynamic_cast<ObSimpleLogServer*>(get_cluster()[leader_idx]);
  ASSERT_NE(nullptr, log_server);
  std::thread submit_log_t1([&]() {
    ObTenantEnv::set_tenant(log_server->get_tenant_base());
    EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(leader, raw_write_leader));
  });
  std::thread submit_log_t2([&]() {
    ObTenantEnv::set_tenant(log_server->get_tenant_base());
    EXPECT_EQ(OB_ITER_END, read_and_submit_group_log(leader, raw_write_leader));
  });
  submit_log_t1.join();
  submit_log_t2.join();
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  RUN_SIMPLE_LOG_CLUSTER_TEST(TEST_NAME);
}
