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

const std::string TEST_NAME = "log_disk_mgr";

using namespace oceanbase::common;
using namespace oceanbase;
namespace oceanbase
{
using namespace logservice;
namespace unittest
{
class TestObSimpleLogDiskMgr : public ObSimpleLogClusterTestEnv
  {
  public:
    TestObSimpleLogDiskMgr() : ObSimpleLogClusterTestEnv()
    {
      int ret = init();
      if (OB_SUCCESS != ret) {
        throw std::runtime_error("TestObSimpleLogDiskMgr init failed");
      }
    }
    ~TestObSimpleLogDiskMgr()
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
  };

int64_t ObSimpleLogClusterTestBase::member_cnt_ = 1;
int64_t ObSimpleLogClusterTestBase::node_cnt_ = 1;
std::string ObSimpleLogClusterTestBase::test_name_ = TEST_NAME;
bool ObSimpleLogClusterTestBase::need_add_arb_server_  = false;
bool ObSimpleLogClusterTestBase::need_shared_storage_ = false;
int64_t log_entry_size = 2 * 1024 * 1024 + 16 * 1024;

TEST_F(TestObSimpleLogDiskMgr, out_of_disk_space)
{
  update_server_log_disk(10*1024*1024*1024ul);
  SET_CASE_LOG_FILE(TEST_NAME, "out_of_disk_space");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int server_idx = 0;
  PalfEnv *palf_env = NULL;
  int64_t leader_idx = 0;
  PalfHandleImplGuard leader;
  share::SCN create_scn = share::SCN::base_scn();
  EXPECT_EQ(OB_SUCCESS, get_palf_env(server_idx, palf_env));
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, create_scn, leader_idx, leader));
  update_disk_options(leader_idx, MIN_DISK_SIZE_PER_PALF_INSTANCE/PALF_PHY_BLOCK_SIZE + 2);
  sleep(2);
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 8*31+1, id, log_entry_size));
  LogStorage *log_storage = &leader.palf_handle_impl_->log_engine_.log_storage_;
  while (LSN(6*PALF_BLOCK_SIZE) > log_storage->log_tail_) {
    usleep(500);
  }
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 20, id, log_entry_size));
  while (LSN(6*PALF_BLOCK_SIZE + 20 * log_entry_size) > log_storage->log_tail_) {
    usleep(500);
  }
  LSN max_lsn = leader.palf_handle_impl_->get_max_lsn();
  wait_lsn_until_flushed(max_lsn, leader);
  PALF_LOG(INFO, "out of disk max_lsn", K(max_lsn));
  sleep(2);
  EXPECT_EQ(OB_LOG_OUTOF_DISK_SPACE, submit_log(leader, 1, id, MAX_LOG_BODY_SIZE));
  // Continue to stop writing after shrinking
  update_disk_options(leader_idx, MIN_DISK_SIZE_PER_PALF_INSTANCE/PALF_PHY_BLOCK_SIZE);
  EXPECT_EQ(OB_LOG_OUTOF_DISK_SPACE, submit_log(leader, 1, id, MAX_LOG_BODY_SIZE));
  usleep(ObLooper::INTERVAL_US*2);
}

TEST_F(TestObSimpleLogDiskMgr, update_disk_options_basic)
{
  SET_CASE_LOG_FILE(TEST_NAME, "update_disk_options_basic");
  OB_LOGGER.set_log_level("INFO");
  const int64_t id = ATOMIC_AAF(&palf_id_, 1);
  // Set the log disk space to 10GB
  update_disk_options(10*1024*1024*1024ul/PALF_PHY_BLOCK_SIZE);
  sleep(2);
  PALF_LOG(INFO, "start update_disk_options_basic", K(id));
  int64_t leader_idx = 0;
  PalfHandleImplGuard leader;
  PalfEnv *palf_env = NULL;
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
  EXPECT_EQ(OB_SUCCESS, get_palf_env(leader_idx, palf_env));
  // Submit 1G of logs
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 500, id, log_entry_size));
  EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));

  EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, 20));
  sleep(2);
  EXPECT_EQ(PalfDiskOptionsWrapper::Status::SHRINKING_STATUS,
            palf_env->palf_env_impl_.disk_options_wrapper_.status_);
  EXPECT_EQ(palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_recycling_blocks_.log_disk_usage_limit_size_,
            20*PALF_PHY_BLOCK_SIZE);
  // case1: before the last scaling down is completed, you can continue to scale down
  EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, 10));
  sleep(2);
  EXPECT_EQ(PalfDiskOptionsWrapper::Status::SHRINKING_STATUS,
            palf_env->palf_env_impl_.disk_options_wrapper_.status_);
  EXPECT_EQ(palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_recycling_blocks_.log_disk_usage_limit_size_,
            10*PALF_PHY_BLOCK_SIZE);
  // At this time, the log disk has not yet completed the reduction, ObSimpleLogServer maintains the disk_opts_ as 10GB
  {
    PalfDiskOptions opts;
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    EXPECT_EQ(opts.log_disk_usage_limit_size_, 10*1024*1024*1024ul);
  }
  // case2: before the previous shrink operation is completed, you can continue to expand, at the same time, since the log disk is still smaller than the first shrink after expansion, it remains in the shrinking state.
  EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, 11));
  sleep(2);
  EXPECT_EQ(PalfDiskOptionsWrapper::Status::SHRINKING_STATUS,
            palf_env->palf_env_impl_.disk_options_wrapper_.status_);
  EXPECT_EQ(palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_recycling_blocks_.log_disk_usage_limit_size_,
            11*PALF_PHY_BLOCK_SIZE);
  // At this time, the log disk has not yet completed the reduction, ObSimpleLogServer maintains the disk_opts_ as 10GB
  {
    PalfDiskOptions opts;
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    EXPECT_EQ(opts.log_disk_usage_limit_size_, 10*1024*1024*1024ul);
  }
  sleep(2);
  EXPECT_EQ(PalfDiskOptionsWrapper::Status::SHRINKING_STATUS,
            palf_env->palf_env_impl_.disk_options_wrapper_.status_);
  EXPECT_EQ(palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_recycling_blocks_.log_disk_usage_limit_size_,
            11*PALF_PHY_BLOCK_SIZE);
  EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
  const LSN base_lsn(12*PALF_BLOCK_SIZE);
  EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->set_base_lsn(base_lsn));
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, id, 1000));
  EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));
  // wait until disk space enough
  EXPECT_EQ(OB_SUCCESS, wait_until_disk_space_to(leader_idx, (11*PALF_PHY_BLOCK_SIZE*80+100)/100));
  sleep(2);
  EXPECT_EQ(PalfDiskOptionsWrapper::Status::NORMAL_STATUS,
            palf_env->palf_env_impl_.disk_options_wrapper_.status_);
  EXPECT_EQ(palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_stopping_writing_.log_disk_usage_limit_size_,
            11*PALF_PHY_BLOCK_SIZE);
  // Wait for the background thread to execute the update_disk_options operation again, expect the locally persisted disk_opts to become 11*PALF_PHY_BLOCK_SIZE
  {
    sleep(2);
    PalfDiskOptions opts;
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    EXPECT_EQ(opts.log_disk_usage_limit_size_, 11*PALF_PHY_BLOCK_SIZE);
  }
}

TEST_F(TestObSimpleLogDiskMgr, shrink_log_disk)
{
  SET_CASE_LOG_FILE(TEST_NAME, "shrink_log_disk");
  OB_LOGGER.set_log_level("INFO");
  // Validate that scaling down fails due to the minimum requirement of 512MB log disk for a single log stream
  // Ensure that it can accommodate two log streams simultaneously
  PalfEnv *palf_env = NULL;
  int64_t leader_idx = 0;
  PalfHandleImplGuard leader;
  share::SCN create_scn = share::SCN::base_scn();
  int server_idx = 0;
  EXPECT_EQ(OB_SUCCESS, get_palf_env(server_idx, palf_env));
  EXPECT_EQ(OB_SUCCESS, update_disk_options(16));
  EXPECT_EQ(PalfDiskOptionsWrapper::Status::NORMAL_STATUS,
            palf_env->palf_env_impl_.disk_options_wrapper_.status_);
  EXPECT_EQ(palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_stopping_writing_.log_disk_usage_limit_size_,
            16*PALF_PHY_BLOCK_SIZE);
  int64_t tmp_id1 = ATOMIC_AAF(&palf_id_, 1);
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(tmp_id1, leader_idx, leader));
  }
  int64_t tmp_id2 = ATOMIC_AAF(&palf_id_, 1);
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(tmp_id2, leader_idx, leader));
  }
  EXPECT_EQ(OB_NOT_SUPPORTED, update_disk_options(9));
  EXPECT_EQ(OB_SUCCESS, delete_paxos_group(tmp_id1));
}

TEST_F(TestObSimpleLogDiskMgr, update_disk_options_restart)
{
  disable_hot_cache_ = true;
  SET_CASE_LOG_FILE(TEST_NAME, "update_disk_options_restart");
  OB_LOGGER.set_log_level("INFO");
  // Expansion operation
  EXPECT_EQ(OB_SUCCESS, update_disk_options(10*1024*1024*1024ul/PALF_PHY_BLOCK_SIZE));
  sleep(2);
  const int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  PalfEnv *palf_env = NULL;
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
    {
      PalfDiskOptions opts;
      EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
      EXPECT_EQ(opts.log_disk_usage_limit_size_, 10*1024*1024*1024ul);
    }
    EXPECT_EQ(OB_SUCCESS, get_palf_env(leader_idx, palf_env));
    {
      EXPECT_EQ(PalfDiskOptionsWrapper::Status::NORMAL_STATUS,
                palf_env->palf_env_impl_.disk_options_wrapper_.status_);
      EXPECT_EQ(10*1024*1024*1024ul,
                palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_recycling_blocks_.log_disk_usage_limit_size_);
    }
    // Generate data for 10 files
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10*32, id, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
    // The minimum log_disk_size requirement is to have 8 log files
    EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, 8));
    sleep(2);
    // Before shutdown, scaling down will not take effect officially, therefore it will not cause write stoppage
    EXPECT_EQ(true, palf_env->palf_env_impl_.diskspace_enough_);
    EXPECT_EQ(PalfDiskOptionsWrapper::Status::SHRINKING_STATUS,
              palf_env->palf_env_impl_.disk_options_wrapper_.status_);
    EXPECT_EQ(10*1024*1024*1024ul, palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_stopping_writing_.log_disk_usage_limit_size_);
    int64_t log_disk_usage, total_log_disk_size;
    EXPECT_EQ(OB_SUCCESS, palf_env->palf_env_impl_.get_disk_usage(log_disk_usage, total_log_disk_size));
    PALF_LOG(INFO, "log_disk_usage:", K(log_disk_usage), K(total_log_disk_size));
    // Before the downsizing is successful, log_disk_usage_limit_size_ still remains at 10G.
    // The locally persisted log_disk_size is 10G
    // The persisted log_disk_size in the internal table is 8*PALF_PHY_BLOCK_SIZE
    PalfDiskOptions opts;
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    EXPECT_EQ(opts.log_disk_usage_limit_size_, 10*1024*1024*1024ul);
  }
  // Physical shrink not successful yet, expected that shutdown restart will not stop writing
  EXPECT_EQ(OB_SUCCESS, restart_paxos_groups());
  {
    // The log_disk_size persisted in the internal table is still 8*PALF_PHY_BLOCK_SIZE
    // Restart and continue scaling down
    int64_t log_disk_usage, total_log_disk_size;
    EXPECT_EQ(OB_SUCCESS, get_palf_env(leader_idx, palf_env));
    usleep(2*1000*1000 + BlockGCTimerTask::BLOCK_GC_TIMER_INTERVAL_MS);
    EXPECT_EQ(true, palf_env->palf_env_impl_.diskspace_enough_);
    EXPECT_EQ(PalfDiskOptionsWrapper::Status::SHRINKING_STATUS,
              palf_env->palf_env_impl_.disk_options_wrapper_.status_);
    // Locally persisted (slog) records are still 10G, therefore writing will not stop
    EXPECT_EQ(10*1024*1024*1024ul, palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_stopping_writing_.log_disk_usage_limit_size_);
    EXPECT_EQ(OB_SUCCESS, palf_env->palf_env_impl_.get_disk_usage(log_disk_usage, total_log_disk_size));
    PALF_LOG(INFO, "log_disk_usage:", K(log_disk_usage), K(total_log_disk_size));
    PalfDiskOptions opts;
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    EXPECT_EQ(opts.log_disk_usage_limit_size_, 10*1024*1024*1024ul);
  }
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, get_leader(id, leader, leader_idx));
    PalfDiskOptions opts;
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    EXPECT_EQ(opts.log_disk_usage_limit_size_, 10*1024*1024*1024ul);
    // Physically ensure no shrinking, the persisted internal table becomes 16*PALF_PHY_BLOCK_SIZE, since palf's log_disk_size is 10G,
    // Therefore this operation is a shrink for palf. But when the next GC task runs, it finds that the currently used log disk space will not cause a stop write,
    // Thus the log disk became normal status
    EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, 16));
    // After the next round of GC task runs, the locally persisted log_disk_size will also become 16*PALF_PHY_BLOCK_SIZE
    usleep(2*1000*1000+palf::BlockGCTimerTask::BLOCK_GC_TIMER_INTERVAL_MS);
    // After one round of GC, it will become NORMAL_STATUS
    EXPECT_EQ(true, palf_env->palf_env_impl_.diskspace_enough_);
    EXPECT_EQ(PalfDiskOptionsWrapper::Status::NORMAL_STATUS,
              palf_env->palf_env_impl_.disk_options_wrapper_.status_);
    EXPECT_EQ(16*PALF_PHY_BLOCK_SIZE,
              palf_env->palf_env_impl_.disk_options_wrapper_.disk_opts_for_recycling_blocks_.log_disk_usage_limit_size_);
    // The background thread will complete the scaling down operation, ultimately making the local persistence 16*PALF_PHY_BLOCK_SIZE
    usleep(2*1000*1000+ObLooper::INTERVAL_US*2);
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    EXPECT_EQ(opts.log_disk_usage_limit_size_, 16*PALF_PHY_BLOCK_SIZE); 
  }
}

TEST_F(TestObSimpleLogDiskMgr, overshelling)
{
  disable_hot_cache_ = true;
  SET_CASE_LOG_FILE(TEST_NAME, "overshelling");
  OB_LOGGER.set_log_level("INFO");
  ObServerLogBlockMgr *log_pool = nullptr;
  EXPECT_EQ(OB_SUCCESS, get_log_pool(0, log_pool));
  ASSERT_NE(nullptr, log_pool);
  // Validate the correctness of the LogPool field in the scaling scenario
  EXPECT_EQ(16*PALF_PHY_BLOCK_SIZE, log_pool->min_log_disk_size_for_all_tenants_);
  const int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t leader_idx = 0;
  PalfEnv *palf_env = NULL;
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, leader_idx, leader));
  }
  EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, 15));
  int64_t log_disk_size_used_for_tenants = log_pool->min_log_disk_size_for_all_tenants_;
  // Shrink has not been successful yet, expected log_disk_size_used_for_tenants must be 16*PALF_PHY_BLOCK_SIZE
  PalfDiskOptions opts;
  EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
  if (opts.log_disk_usage_limit_size_ == 16*PALF_PHY_BLOCK_SIZE) {
    EXPECT_EQ(16*PALF_PHY_BLOCK_SIZE, log_disk_size_used_for_tenants);
    // Shrinking will not take effect immediately
    usleep(2*1000*1000+ObLooper::INTERVAL_US*2);
    EXPECT_EQ(15*PALF_PHY_BLOCK_SIZE, log_pool->min_log_disk_size_for_all_tenants_);
  } else {
    PALF_LOG(INFO, "update_disk_options successfully", K(log_disk_size_used_for_tenants), K(opts));
  }
  // Expansion expected to succeed immediately
  EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, 16));
  EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
  EXPECT_EQ(16*PALF_PHY_BLOCK_SIZE, log_disk_size_used_for_tenants);
  EXPECT_EQ(16*PALF_PHY_BLOCK_SIZE, opts.log_disk_usage_limit_size_);
  // Directly expand to the upper limit value of LogPool
  const int64_t limit_log_disk_size = log_pool->log_pool_meta_.curr_total_size_;
  EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, limit_log_disk_size/PALF_PHY_BLOCK_SIZE));
  EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
  log_disk_size_used_for_tenants = log_pool->min_log_disk_size_for_all_tenants_;
  EXPECT_EQ(limit_log_disk_size, log_disk_size_used_for_tenants);
  EXPECT_EQ(limit_log_disk_size, opts.log_disk_usage_limit_size_);

  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, get_leader(id, leader, leader_idx));
    // Generate 10 files
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 10*32, id, log_entry_size));
    EXPECT_EQ(OB_SUCCESS, wait_until_has_committed(leader, leader.palf_handle_impl_->get_max_lsn()));
    EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, 10));
    // Shrinking will definitely not succeed, tenant log disk specification remains at the upper limit value
    usleep(2*1000*1000+ObLooper::INTERVAL_US * 2);
    EXPECT_EQ(OB_SUCCESS, get_palf_env(leader_idx, palf_env));
    EXPECT_EQ(PalfDiskOptionsWrapper::Status::SHRINKING_STATUS,
              palf_env->palf_env_impl_.disk_options_wrapper_.status_);
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    log_disk_size_used_for_tenants = log_pool->min_log_disk_size_for_all_tenants_;
    EXPECT_EQ(limit_log_disk_size, log_disk_size_used_for_tenants);
    EXPECT_EQ(limit_log_disk_size, opts.log_disk_usage_limit_size_);
    EXPECT_EQ(OB_MACHINE_RESOURCE_NOT_ENOUGH, log_pool->create_tenant(MIN_DISK_SIZE_PER_PALF_INSTANCE));
    const LSN base_lsn(8*PALF_BLOCK_SIZE);
    EXPECT_EQ(OB_SUCCESS, leader.palf_handle_impl_->set_base_lsn(base_lsn));
    EXPECT_EQ(OB_SUCCESS, submit_log(leader, 1, id, 1000));
    EXPECT_EQ(OB_SUCCESS, wait_lsn_until_flushed(leader.palf_handle_impl_->get_max_lsn(), leader));
    EXPECT_EQ(OB_SUCCESS, wait_until_disk_space_to(leader_idx, (10*PALF_PHY_BLOCK_SIZE*80+100)/100));
    sleep(2);
    EXPECT_EQ(PalfDiskOptionsWrapper::Status::NORMAL_STATUS,
              palf_env->palf_env_impl_.disk_options_wrapper_.status_);
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    log_disk_size_used_for_tenants = log_pool->min_log_disk_size_for_all_tenants_;
    EXPECT_EQ(10*PALF_PHY_BLOCK_SIZE, log_disk_size_used_for_tenants);
    EXPECT_EQ(10*PALF_PHY_BLOCK_SIZE, opts.log_disk_usage_limit_size_);
    EXPECT_EQ(OB_SUCCESS, log_pool->create_tenant(MIN_DISK_SIZE_PER_PALF_INSTANCE));
    log_pool->abort_create_tenant(MIN_DISK_SIZE_PER_PALF_INSTANCE);
    // Expansion is expected to succeed
    EXPECT_EQ(OB_SUCCESS, update_disk_options(leader_idx, limit_log_disk_size/PALF_PHY_BLOCK_SIZE));
    EXPECT_EQ(PalfDiskOptionsWrapper::Status::NORMAL_STATUS,
              palf_env->palf_env_impl_.disk_options_wrapper_.status_);
    EXPECT_EQ(OB_SUCCESS, get_disk_options(leader_idx, opts));
    log_disk_size_used_for_tenants = log_pool->min_log_disk_size_for_all_tenants_;
    EXPECT_EQ(limit_log_disk_size, log_disk_size_used_for_tenants);
    EXPECT_EQ(limit_log_disk_size, opts.log_disk_usage_limit_size_);
    EXPECT_EQ(OB_MACHINE_RESOURCE_NOT_ENOUGH, log_pool->create_tenant(MIN_DISK_SIZE_PER_PALF_INSTANCE));

  }

}

TEST_F(TestObSimpleLogDiskMgr, hidden_sys)
{
  SET_CASE_LOG_FILE(TEST_NAME, "hidden_sys");
  int server_idx = 0;
  PalfEnv *palf_env = NULL;
  int64_t leader_idx = 0;
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int64_t total_used_size = 0, total_size = 0;
  share::SCN create_scn = share::SCN::base_scn();
  EXPECT_EQ(OB_SUCCESS, get_palf_env(0, palf_env));
  {
    PalfHandleImplGuard leader;
    EXPECT_EQ(OB_SUCCESS, palf_env->get_stable_disk_usage(total_used_size, total_size));
    EXPECT_EQ(0, total_used_size);
    EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, create_scn, leader_idx, leader));
    EXPECT_EQ(OB_SUCCESS, palf_env->get_stable_disk_usage(total_used_size, total_size));
    EXPECT_NE(0, total_used_size);
    EXPECT_EQ(OB_NOT_SUPPORTED, update_disk_options(0));
  }
  EXPECT_EQ(OB_SUCCESS, delete_paxos_group(id));
  EXPECT_EQ(OB_SUCCESS, update_disk_options(0));
  // disk_opts recorded in tenant unit take effect directly
  PalfDiskOptions disk_opts;
  EXPECT_EQ(OB_SUCCESS, get_disk_options(0, disk_opts));
  EXPECT_EQ(0, disk_opts.log_disk_usage_limit_size_);
  EXPECT_EQ(PalfDiskOptionsWrapper::Status::SHRINKING_STATUS,
            palf_env->palf_env_impl_.disk_options_wrapper_.status_);
  sleep(2);
  EXPECT_EQ(PalfDiskOptionsWrapper::Status::NORMAL_STATUS,
            palf_env->palf_env_impl_.disk_options_wrapper_.status_);
  EXPECT_EQ(OB_SUCCESS, palf_env->get_stable_disk_usage(total_used_size, total_size));
  EXPECT_EQ(0, total_used_size);
  EXPECT_EQ(0, total_size);
  EXPECT_EQ(OB_SUCCESS, update_disk_options(8));
  PalfHandleImplGuard leader;
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, create_scn, leader_idx, leader));
  EXPECT_EQ(OB_SUCCESS, palf_env->get_stable_disk_usage(total_used_size, total_size));
  EXPECT_NE(0, total_used_size);
}

TEST_F(TestObSimpleLogDiskMgr, test_big_log)
{
  update_server_log_disk(10*1024*1024*1024ul);
  update_disk_options(10*1024*1024*1024ul/palf::PALF_PHY_BLOCK_SIZE);
  SET_CASE_LOG_FILE(TEST_NAME, "test_big_log");
  int64_t id = ATOMIC_AAF(&palf_id_, 1);
  int server_idx = 0;
  PalfEnv *palf_env = NULL;
  int64_t leader_idx = 0;
  PalfHandleImplGuard leader;
  share::SCN create_scn = share::SCN::base_scn();
  EXPECT_EQ(OB_SUCCESS, get_palf_env(server_idx, palf_env));
  EXPECT_EQ(OB_SUCCESS, create_paxos_group(id, create_scn, leader_idx, leader));
  update_disk_options(leader_idx, MIN_DISK_SIZE_PER_PALF_INSTANCE/PALF_PHY_BLOCK_SIZE);
  // write big log
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 11, id, MAX_LOG_BODY_SIZE));
  LogStorage *log_storage = &leader.palf_handle_impl_->log_engine_.log_storage_;
  while(LSN(10*MAX_LOG_BODY_SIZE) > log_storage->log_tail_) {
    usleep(500);
  }
  LSN max_lsn = leader.palf_handle_impl_->get_max_lsn();
  wait_lsn_until_flushed(max_lsn, leader);
  PALF_LOG(INFO, "test_big_log max_lsn after writing big log", K(max_lsn));

  // write small log
  EXPECT_EQ(OB_SUCCESS, submit_log(leader, 11, id, log_entry_size));
  while(LSN(10*log_entry_size) > log_storage->log_tail_) {
    usleep(500);
  }
  
  max_lsn = leader.palf_handle_impl_->get_max_lsn();
  wait_lsn_until_flushed(max_lsn, leader);
  PALF_LOG(INFO, "test_big_log max_lsn after writing small log", K(max_lsn));
  EXPECT_EQ(OB_ITER_END, read_log(leader));
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  RUN_SIMPLE_LOG_CLUSTER_TEST(TEST_NAME);
}
