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
#include "logservice/palf/log_config_mgr.h"
#include "mock_logservice_container/mock_election.h"
#include "mock_logservice_container/mock_log_sliding_window.h"
#include "mock_logservice_container/mock_log_engine.h"
#include "mock_logservice_container/mock_log_mode_mgr.h"
#include "mock_logservice_container/mock_log_reconfirm.h"
#include "logservice/env/mock_ob_locality_manager.h"
#undef private
namespace oceanbase
{
namespace unittest
{
using namespace common;
using namespace share;
using namespace palf;

const ObAddr addr1(ObAddr::IPV4, "127.0.0.1", 1000);
const ObAddr addr2(ObAddr::IPV4, "127.0.0.2", 1000);
const ObAddr addr3(ObAddr::IPV4, "127.0.0.3", 1000);
const ObAddr addr4(ObAddr::IPV4, "127.0.0.4", 1000);
const ObAddr addr5(ObAddr::IPV4, "127.0.0.5", 1000);
const ObAddr addr6(ObAddr::IPV4, "127.0.0.6", 1000);
const ObAddr addr7(ObAddr::IPV4, "127.0.0.7", 1000);
const ObAddr addr8(ObAddr::IPV4, "127.0.0.8", 1000);
const ObAddr addr9(ObAddr::IPV4, "127.0.0.9", 1000);
ObRegion region1("BEIJING");
ObRegion region2("SHANGHAI");
ObRegion default_region(DEFAULT_REGION_NAME);
const int64_t INIT_ELE_EPOCH = 1;
const int64_t INIT_PROPOSAL_ID = 1;

int build_log_sync_member_list(ObMemberList &member_list)
{
  member_list.reset();
  return member_list.add_server(addr1);
}

int64_t get_default_log_sync_replica_num()
{
  return 1;
}

int build_default_log_config_info(LogConfigInfoV2 &config_info,
                                  GlobalLearnerList &learner_list,
                                  LogConfigVersion &config_version)
{
  ObMemberList member_list;
  int ret = build_log_sync_member_list(member_list);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(config_version.generate(1, 1))) {
  } else {
    ret = config_info.generate(member_list, get_default_log_sync_replica_num(),
                               learner_list, config_version);
  }
  return ret;
}

class TestLogConfigMgr : public ::testing::Test
{
public:
  TestLogConfigMgr()
  {
    mock_election_ = OB_NEW(mockelection::MockElection, "TestLog");
    mock_state_mgr_ = OB_NEW(MockLogStateMgr, "TestLog");
    mock_sw_ = OB_NEW(MockLogSlidingWindow, "TestLog");
    mock_log_engine_ = OB_NEW(MockLogEngine, "TestLog");
    mock_mode_mgr_ = OB_NEW(MockLogModeMgr, "TestLog");
    mock_reconfirm_ = OB_NEW(MockLogReconfirm, "TestLog");
    mock_plugins_ = OB_NEW(LogPlugins, "TestLog");
    mock_locality_cb_ = OB_NEW(MockObLocalityManager, "TestLog");

    region_map_.create(OB_MAX_MEMBER_NUMBER, ObMemAttr(MTL_ID(), ObModIds::OB_HASH_NODE,
        ObCtxIds::DEFAULT_CTX_ID));
    EXPECT_EQ(OB_SUCCESS, mock_locality_cb_->init(&region_map_));
    EXPECT_EQ(OB_SUCCESS, mock_plugins_->add_plugin(dynamic_cast<PalfLocalityInfoCb*>(mock_locality_cb_)));
  }
  ~TestLogConfigMgr()
  {
    EXPECT_EQ(OB_SUCCESS, mock_plugins_->del_plugin(dynamic_cast<PalfLocalityInfoCb*>(mock_locality_cb_)));
    OB_DELETE(MockElection, "TestLog", mock_election_);
    OB_DELETE(MockLogStateMgr, "TestLog", mock_state_mgr_);
    OB_DELETE(MockLogSlidingWindow, "TestLog", mock_sw_);
    OB_DELETE(MockLogEngine, "TestLog", mock_log_engine_);
    OB_DELETE(MockLogModeMgr, "TestLog", mock_mode_mgr_);
    OB_DELETE(MockLogReconfirm, "TestLog", mock_reconfirm_);
    OB_DELETE(LogPlugins, "TestLog", mock_plugins_);
    OB_DELETE(MockObLocalityManager, "TestLog", mock_locality_cb_);
  }
  void init_test_log_config_env(const common::ObAddr &self,
                                const LogConfigInfoV2 &config_info,
                                LogConfigMgr &cm,
                                common::ObRole role = LEADER,
                                ObReplicaState state = ACTIVE)
  {
    mock_state_mgr_->role_ = role;
    mock_state_mgr_->state_ = state;
    mock_state_mgr_->leader_ = (LEADER == role)? self: ObAddr();
    mock_election_->role_ = role;
    mock_election_->leader_epoch_ = INIT_ELE_EPOCH;
    mock_sw_->mock_last_submit_lsn_ = LSN(PALF_INITIAL_LSN_VAL);
    mock_sw_->mock_last_submit_end_lsn_ = LSN(PALF_INITIAL_LSN_VAL);
    mock_sw_->mock_last_submit_pid_ = INIT_PROPOSAL_ID;
    mock_sw_->state_mgr_ = mock_state_mgr_;
    ASSERT_TRUE(config_info.is_valid());
    PALF_LOG(INFO, "init_test_log_config_env", K(role), K(state), K(mock_state_mgr_->leader_), K(mock_state_mgr_->role_));
    mock_log_engine_->reset_register_parent_resp_ret();
    common::GlobalLearnerList learner_list;
    const int64_t init_pid = INIT_PROPOSAL_ID;
    LogConfigMeta config_meta;
    ASSERT_EQ(OB_SUCCESS, config_meta.generate(init_pid, config_info, config_info, 1, LSN(0), 1));
    ASSERT_EQ(OB_SUCCESS, cm.init(1, self, config_meta, mock_log_engine_, mock_sw_, mock_state_mgr_, mock_election_,
        mock_mode_mgr_, mock_reconfirm_, mock_plugins_));
  }
public:
  mockelection::MockElection *mock_election_;
  palf::MockLogStateMgr *mock_state_mgr_;
  palf::MockLogSlidingWindow *mock_sw_;
  palf::MockLogEngine *mock_log_engine_;
  palf::MockLogModeMgr *mock_mode_mgr_;
  palf::MockLogReconfirm *mock_reconfirm_;
  palf::LogPlugins *mock_plugins_;
  unittest::MockObLocalityManager *mock_locality_cb_;
  LogMemberRegionMap region_map_;
};

TEST_F(TestLogConfigMgr, test_remove_child_is_not_learner)
{
  LogConfigMgr cm;
  LogLearnerList removed_children;
  LogLearnerList retire_children;
  LogLearnerList target_children;
  EXPECT_EQ(OB_SUCCESS, cm.all_learnerlist_.add_learner(ObMember(addr1, -1)));
  EXPECT_EQ(OB_SUCCESS, cm.all_learnerlist_.add_learner(ObMember(addr4, -1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr1, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr2, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr3, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr4, 1)));
  EXPECT_EQ(OB_SUCCESS, target_children.add_learner(LogLearner(addr1, 1)));
  EXPECT_EQ(OB_SUCCESS, target_children.add_learner(LogLearner(addr4, 1)));
  EXPECT_EQ(OB_SUCCESS, retire_children.add_learner(LogLearner(addr2, 1)));
  EXPECT_EQ(OB_SUCCESS, retire_children.add_learner(LogLearner(addr3, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.remove_child_is_not_learner_(removed_children));
  EXPECT_TRUE(cm.children_.learner_addr_equal(target_children));
  EXPECT_TRUE(retire_children.learner_addr_equal(removed_children));
  PALF_LOG(INFO, "children", K(cm.children_));
}

TEST_F(TestLogConfigMgr, test_remove_diff_region_child)
{
  LogConfigMgr cm;
  LogLearnerList diff_region_children;
  LogLearnerList removed_children;
  LogLearnerList target_children;
  ObTenantBase tenant_base(1);
  ObTenantEnv::set_tenant(&tenant_base);
  cm.region_ = region1;
  EXPECT_EQ(OB_SUCCESS, target_children.add_learner(LogLearner(addr1, region1, 1)));
  EXPECT_EQ(OB_SUCCESS, target_children.add_learner(LogLearner(addr2, region1, 1)));
  EXPECT_EQ(OB_SUCCESS, removed_children.add_learner(LogLearner(addr3, 1)));
  EXPECT_EQ(OB_SUCCESS, removed_children.add_learner(LogLearner(addr4, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr1, region1, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr2, region1, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr3, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr4, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.remove_diff_region_child_(diff_region_children));
  EXPECT_TRUE(target_children.learner_addr_equal(cm.children_));
  EXPECT_TRUE(removed_children.learner_addr_equal(diff_region_children));
}

TEST_F(TestLogConfigMgr, test_remove_duplicate_region_child)
{
  LogConfigMgr cm;
  LogLearnerList dup_region_children;
  LogLearnerList removed_children;
  LogLearnerList target_children;
  ObTenantBase tenant_base(1);
  ObTenantEnv::set_tenant(&tenant_base);
  EXPECT_EQ(OB_SUCCESS, target_children.add_learner(LogLearner(addr1, region1, 1)));
  EXPECT_EQ(OB_SUCCESS, target_children.add_learner(LogLearner(addr3, region2, 1)));
  EXPECT_EQ(OB_SUCCESS, removed_children.add_learner(LogLearner(addr2, region1, 1)));
  EXPECT_EQ(OB_SUCCESS, removed_children.add_learner(LogLearner(addr4, region2, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr1, region1, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr2, region1, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr3, region2, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr4, region2, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.remove_duplicate_region_child_(dup_region_children));
  EXPECT_TRUE(target_children.learner_addr_equal(cm.children_));
  EXPECT_TRUE(removed_children.learner_addr_equal(dup_region_children));
}

TEST_F(TestLogConfigMgr, test_handle_learner_keepalive)
{
  LogConfigMgr cm;
  cm.is_inited_ = true;
  LogLearner child(addr1, 1);
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(child));
  EXPECT_EQ(OB_SUCCESS, cm.handle_learner_keepalive_resp(child));
  LogLearner child_in_list;
  EXPECT_EQ(OB_SUCCESS, cm.children_.get_learner_by_addr(addr1, child_in_list));
  EXPECT_GT(child_in_list.keepalive_ts_, 0);
}

TEST_F(TestLogConfigMgr, test_config_change_lock)
{
  ObMemberList init_member_list;
  ASSERT_EQ(OB_SUCCESS, build_log_sync_member_list(init_member_list));
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, init_config_version.generate(1, 1));
  GlobalLearnerList learner_list;
  LogConfigInfoV2 default_config_info;
  ASSERT_EQ(OB_SUCCESS, default_config_info.generate(init_member_list, get_default_log_sync_replica_num(),
                                                    learner_list, init_config_version));


  const int64_t lock_type_try_lock = ConfigChangeLockType::LOCK_PAXOS_MEMBER_CHANGE; 
  const int64_t lock_type_unlock = ConfigChangeLockType::LOCK_NOTHING; 
  LogLockMeta lock_meta;
  EXPECT_EQ(OB_SUCCESS, lock_meta.generate(2, lock_type_try_lock));
  LogConfigInfoV2 locked_config_info;
  ASSERT_EQ(OB_SUCCESS, locked_config_info.generate(init_member_list, get_default_log_sync_replica_num(),
                                                  learner_list, init_config_version, lock_meta));

  LogLockMeta unlock_meta = lock_meta;
  unlock_meta.unlock();
  LogConfigInfoV2 unlock_config_info;
  ASSERT_EQ(OB_SUCCESS, unlock_config_info.generate(init_member_list, get_default_log_sync_replica_num(),
                                                  learner_list, init_config_version, unlock_meta));

  std::vector<LogConfigInfoV2> config_info_list;
  std::vector<LogConfigChangeArgs> arg_list;
  std::vector<int> expect_ret_list;
  std::vector<bool> expect_finished_list;
  std::vector<bool> expect_whether_lock_list;
  std::vector<int64_t> expect_lock_owner_list;
  std::vector<int64_t> get_config_change_stat_lock_owner_list;
  std::vector<bool> get_config_change_stat_lock_stat_list;

  //<case 1>[try_lock] invalid argument
  config_info_list.push_back(default_config_info);
  arg_list.push_back(LogConfigChangeArgs(-1, lock_type_try_lock, TRY_LOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_INVALID_ARGUMENT);
  expect_finished_list.push_back(false);
  expect_whether_lock_list.push_back(false);
  expect_lock_owner_list.push_back(OB_INVALID_CONFIG_CHANGE_LOCK_OWNER);
  get_config_change_stat_lock_owner_list.push_back(OB_INVALID_CONFIG_CHANGE_LOCK_OWNER);
  get_config_change_stat_lock_stat_list.push_back(false);

  //<case 2>[try_lock] OB_SUCCESS is_finish =false
  config_info_list.push_back(default_config_info);
  arg_list.push_back(LogConfigChangeArgs(2, lock_type_try_lock, TRY_LOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_SUCCESS);
  expect_finished_list.push_back(false);
  expect_whether_lock_list.push_back(true);
  expect_lock_owner_list.push_back(2);
  get_config_change_stat_lock_owner_list.push_back(2);
  get_config_change_stat_lock_stat_list.push_back(true);

  //<case 3>[try_lock] OB_SUCCESS is_finish = true
  config_info_list.push_back(locked_config_info);
  arg_list.push_back(LogConfigChangeArgs(2, lock_type_try_lock, TRY_LOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_SUCCESS);
  expect_finished_list.push_back(true);
  expect_whether_lock_list.push_back(true);
  expect_lock_owner_list.push_back(2);
  get_config_change_stat_lock_owner_list.push_back(2);
  get_config_change_stat_lock_stat_list.push_back(true);

  //<case 4>[try_lock] OB_TRY_LOCK_CONFIG_CHANGE_CONFLICT
  config_info_list.push_back(locked_config_info);
  arg_list.push_back(LogConfigChangeArgs(3, lock_type_try_lock, TRY_LOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_TRY_LOCK_CONFIG_CHANGE_CONFLICT);
  expect_finished_list.push_back(false);
  expect_whether_lock_list.push_back(true);
  expect_lock_owner_list.push_back(2);
  get_config_change_stat_lock_owner_list.push_back(2);
  get_config_change_stat_lock_stat_list.push_back(true);


  //<case 5>[try_unlock]: owner is someone else, return OB_SUCCESS
  config_info_list.push_back(locked_config_info);
  arg_list.push_back(LogConfigChangeArgs(1, lock_type_unlock, UNLOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_STATE_NOT_MATCH);
  expect_finished_list.push_back(false);
  expect_whether_lock_list.push_back(true);
  expect_lock_owner_list.push_back(2);
  get_config_change_stat_lock_owner_list.push_back(2);
  get_config_change_stat_lock_stat_list.push_back(true);

  //<case 6>[try_unlock]: need unlock and unlocked successfully
  config_info_list.push_back(locked_config_info);
  arg_list.push_back(LogConfigChangeArgs(2, lock_type_unlock, UNLOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_SUCCESS);
  expect_finished_list.push_back(false);
  expect_whether_lock_list.push_back(false);
  expect_lock_owner_list.push_back(2);
  get_config_change_stat_lock_owner_list.push_back(2);
  get_config_change_stat_lock_stat_list.push_back(false);

  //<case 7>[try_unlock]: is already unlock
  config_info_list.push_back(unlock_config_info);
  arg_list.push_back(LogConfigChangeArgs(2, lock_type_unlock, UNLOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_SUCCESS);
  expect_finished_list.push_back(true);
  expect_whether_lock_list.push_back(false);
  expect_lock_owner_list.push_back(2);
  get_config_change_stat_lock_owner_list.push_back(2);
  get_config_change_stat_lock_stat_list.push_back(false);

  //<case 8>[try_unlock]: bigger lock owner
  config_info_list.push_back(unlock_config_info);
  arg_list.push_back(LogConfigChangeArgs(3, lock_type_unlock, UNLOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_STATE_NOT_MATCH);
  expect_finished_list.push_back(false);
  expect_whether_lock_list.push_back(false);
  expect_lock_owner_list.push_back(2);
  get_config_change_stat_lock_owner_list.push_back(2);
  get_config_change_stat_lock_stat_list.push_back(false);

//<case 9>[try_unlock]: smaller lock owner
  config_info_list.push_back(unlock_config_info);
  arg_list.push_back(LogConfigChangeArgs(1, lock_type_unlock, UNLOCK_CONFIG_CHANGE));
  expect_ret_list.push_back(OB_STATE_NOT_MATCH);
  expect_finished_list.push_back(false);
  expect_whether_lock_list.push_back(false);
  expect_lock_owner_list.push_back(2);
  get_config_change_stat_lock_owner_list.push_back(2);
  get_config_change_stat_lock_stat_list.push_back(false);



  for (int i = 0; i < arg_list.size(); ++i) {
    PALF_LOG(INFO, "test_check_config_change_args for lock begin case", K(i+1));
    LogConfigMgr cm;
    LogConfigVersion config_version, expect_config_version;
    init_test_log_config_env(addr1, config_info_list[i], cm);
    init_config_version.generate(cm.log_ms_meta_.proposal_id_, 1);
    expect_config_version = init_config_version;
    expect_config_version.inc_update_version(cm.log_ms_meta_.proposal_id_);
    bool already_finished = false;
    int tmp_ret = OB_SUCCESS;
    LSN prev_lsn;
    prev_lsn.val_ = PALF_INITIAL_LSN_VAL;
    tmp_ret = cm.append_config_meta_(1, arg_list[i], already_finished);
    config_version = cm.log_ms_meta_.curr_.config_.config_version_;
    ASSERT_EQ(tmp_ret, expect_ret_list[i]) << "ret failed case: " << (i+1);
    ASSERT_EQ(already_finished, expect_finished_list[i]) << "finished failed case:" << (i+1);
    ASSERT_EQ(cm.log_ms_meta_.curr_.lock_meta_.is_locked(), expect_whether_lock_list[i])<< "lock_stat failed case:" << (i+1);
    ASSERT_EQ(cm.log_ms_meta_.curr_.lock_meta_.lock_owner_, expect_lock_owner_list[i])<< "lock_owner failed case:" << (i+1);

    int64_t get_lock_owner = -1;
    bool is_locked = false;
    ASSERT_EQ(OB_SUCCESS, cm.get_config_change_lock_stat(get_lock_owner, is_locked));
    ASSERT_EQ(get_lock_owner, get_config_change_stat_lock_owner_list[i])<< "get_lock_owner failed case:" << (i+1);
    ASSERT_EQ(is_locked, get_config_change_stat_lock_stat_list[i])<< "get_lock_stat failed case:" << (i+1);
    if (tmp_ret == OB_SUCCESS) {
      if (already_finished) {
        ASSERT_EQ(config_version, init_config_version) << i;
      } else {
        ASSERT_EQ(config_version, expect_config_version) << i;
      }
    }
    PALF_LOG(INFO, "test_check_config_change_args for lock end case", K(i+1));
  }
}

TEST_F(TestLogConfigMgr, test_apply_config_meta)
{
  ObMemberList init_member_list;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  LogConfigInfoV2 default_config_info;
  ASSERT_EQ(OB_SUCCESS, build_log_sync_member_list(init_member_list));
  ASSERT_EQ(OB_SUCCESS, init_config_version.generate(1, 1));
  ASSERT_EQ(OB_SUCCESS, default_config_info.generate(init_member_list, 1, learner_list, init_config_version));

  struct ConfigCase {
    LogConfigChangeArgs args;
    int expect_ret;
    bool expect_finished;
  };
  const ConfigCase cases[] = {
    { LogConfigChangeArgs(), OB_INVALID_ARGUMENT, false },
    { LogConfigChangeArgs(ObMember(addr1, -1), 1, ADD_MEMBER), OB_SUCCESS, true },
    { LogConfigChangeArgs(ObMember(addr4, -1), 0, ADD_LEARNER), OB_SUCCESS, false },
    { LogConfigChangeArgs(ObMember(addr1, -1), 0, ADD_LEARNER), OB_INVALID_ARGUMENT, false },
    { LogConfigChangeArgs(ObMember(addr1, -1), 1, REMOVE_MEMBER), OB_NOT_ALLOW_REMOVING_LEADER, false },
  };
  for (int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    LogConfigMgr cm;
    bool already_finished = false;
    init_test_log_config_env(addr1, default_config_info, cm);
    LogConfigChangeArgs args = cases[i].args;
    args.config_version_ = init_config_version;
    const int tmp_ret = cm.append_config_meta_(1, args, already_finished);
    EXPECT_EQ(tmp_ret, cases[i].expect_ret) << "case: " << (i + 1);
    EXPECT_EQ(already_finished, cases[i].expect_finished) << "case: " << (i + 1);
  }
}

TEST_F(TestLogConfigMgr, test_submit_start_working_log)
{
  PALF_LOG(INFO, "test_submit_start_working_log begin case");
  ObMemberList init_member_list;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  LogConfigInfoV2 config_info;
  ASSERT_EQ(OB_SUCCESS, build_log_sync_member_list(init_member_list));
  ASSERT_EQ(OB_SUCCESS, init_config_version.generate(1, 1));
  ASSERT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(),
                                            learner_list, init_config_version));
  {
    LogConfigMgr cm;
    LogConfigVersion sw_config_version;
    init_test_log_config_env(addr1, config_info, cm, FOLLOWER);
    EXPECT_EQ(OB_NOT_MASTER, cm.confirm_start_working_log(INIT_PROPOSAL_ID, INIT_ELE_EPOCH, sw_config_version));
  }
  {
    LogConfigMgr cm;
    LogConfigVersion sw_config_version;
    init_test_log_config_env(addr1, config_info, cm, LEADER, RECONFIRM);
    EXPECT_EQ(OB_SUCCESS, cm.log_ms_meta_.curr_.config_.learnerlist_.add_learner(ObMember(addr4, -1)));
    LSN prev_lsn;
    prev_lsn.val_ = PALF_INITIAL_LSN_VAL;
    mock_sw_->mock_last_submit_lsn_ = prev_lsn;
    mock_sw_->mock_last_submit_end_lsn_ = prev_lsn;
    mock_sw_->mock_last_submit_pid_ = INVALID_PROPOSAL_ID;
    mock_mode_mgr_->mock_last_submit_mode_meta_.proposal_id_ = 1;
    mock_sw_->mock_max_flushed_lsn_ = prev_lsn;
    mock_sw_->mock_max_flushed_log_pid_ = INVALID_PROPOSAL_ID;
    mock_mode_mgr_->mock_accepted_mode_meta_.proposal_id_ = 1;
    LogConfigVersion expect_config_version;
    ASSERT_EQ(OB_SUCCESS, expect_config_version.generate(1, 2));
    bool unused_bool = false;
    EXPECT_EQ(OB_EAGAIN, cm.confirm_start_working_log(INIT_PROPOSAL_ID, INIT_ELE_EPOCH, sw_config_version));
    EXPECT_EQ(OB_SUCCESS, cm.ack_config_log(addr4, 1, expect_config_version, unused_bool));
    EXPECT_EQ(OB_SUCCESS, cm.ack_config_log(addr1, 1, expect_config_version, unused_bool));
    EXPECT_EQ(OB_SUCCESS, cm.confirm_start_working_log(INIT_PROPOSAL_ID, INIT_ELE_EPOCH, sw_config_version));
    EXPECT_EQ(0, cm.state_);
    EXPECT_EQ(0, cm.resend_log_list_.get_member_number());
  }
  PALF_LOG(INFO, "test_submit_start_working_log end case");
}

TEST_F(TestLogConfigMgr, test_submit_config_log)
{
  PALF_LOG(INFO, "test_submit_config_log begin case");
  LogConfigInfoV2 config_info;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_default_log_config_info(config_info, learner_list, init_config_version));
  {
    LogConfigMgr cm;
    init_test_log_config_env(addr1, config_info, cm, LEADER);
    int64_t proposal_id = INVALID_PROPOSAL_ID;
    EXPECT_EQ(OB_INVALID_ARGUMENT, cm.submit_config_log_(cm.log_ms_meta_.curr_.config_.log_sync_memberlist_, proposal_id, 1, LSN(0), 1, cm.log_ms_meta_));
    EXPECT_EQ(OB_INVALID_ARGUMENT, cm.submit_config_log_(cm.log_ms_meta_.curr_.config_.log_sync_memberlist_, 1, 1, LSN(), 1, cm.log_ms_meta_));
    EXPECT_EQ(OB_INVALID_ARGUMENT, cm.submit_config_log_(cm.log_ms_meta_.curr_.config_.log_sync_memberlist_, 1, 1, LSN(0), INVALID_PROPOSAL_ID, cm.log_ms_meta_));
    EXPECT_EQ(OB_INVALID_ARGUMENT, cm.submit_config_log_(cm.log_ms_meta_.curr_.config_.log_sync_memberlist_, 1, 1, LSN(0), 1, LogConfigMeta()));
    proposal_id = 1;
    mock_sw_->mock_max_flushed_lsn_.val_ = PALF_INITIAL_LSN_VAL;
    mock_sw_->mock_max_flushed_log_pid_ = 0;
    mock_mode_mgr_->mock_accepted_mode_meta_.proposal_id_ = 1;
    EXPECT_EQ(OB_SUCCESS, cm.submit_config_log_(cm.log_ms_meta_.curr_.config_.log_sync_memberlist_, proposal_id, INVALID_PROPOSAL_ID, LSN(0), 1, cm.log_ms_meta_));
    EXPECT_GT(cm.last_submit_config_log_time_us_, 0);
  }
  {
    LogConfigMgr cm;
    init_test_log_config_env(addr1, config_info, cm);
    init_config_version.generate(cm.log_ms_meta_.proposal_id_, 1);
    const int64_t proposal_id = 1;
    LogConfigVersion config_version;
    LSN prev_lsn;
    prev_lsn.val_ = PALF_INITIAL_LSN_VAL;
    LogConfigChangeArgs args;
    args.type_ = palf::ADD_LEARNER;
    args.server_ = common::ObMember(addr4, -1);
    args.config_version_ = init_config_version;
    mock_sw_->mock_last_submit_lsn_ = prev_lsn;
    mock_sw_->mock_last_submit_end_lsn_ = prev_lsn;
    mock_sw_->mock_last_submit_pid_ = INVALID_PROPOSAL_ID;
    mock_mode_mgr_->mock_last_submit_mode_meta_.proposal_id_ = 0;
    mock_sw_->mock_max_flushed_lsn_.val_ = PALF_INITIAL_LSN_VAL;
    mock_sw_->mock_max_flushed_log_pid_ = 0;
    mock_mode_mgr_->mock_accepted_mode_meta_.proposal_id_ = 0;
    EXPECT_EQ(OB_EAGAIN, cm.change_config_(args, proposal_id, INIT_ELE_EPOCH, config_version));
    EXPECT_EQ(OB_SUCCESS, cm.submit_config_log_(cm.log_ms_meta_.curr_.config_.log_sync_memberlist_,
                                                proposal_id, 0, prev_lsn, 0, cm.log_ms_meta_));
    EXPECT_GT(cm.last_submit_config_log_time_us_, 0);
    EXPECT_EQ(cm.state_, 1);
  }
  PALF_LOG(INFO, "test_submit_config_log end case");
}


TEST_F(TestLogConfigMgr, test_after_flush_config_log)
{
  PALF_LOG(INFO, "test_after_flush_config_log begin case");
  LogConfigInfoV2 config_info;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_default_log_config_info(config_info, learner_list, init_config_version));
  // self is paxos member, retire parent
  // child is not learner, retire child
  LogConfigMgr cm;
  init_test_log_config_env(addr1, config_info, cm);
  cm.parent_ = addr2;
  cm.register_time_us_ = 555;
  EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(LogLearner(addr3, 1)));
  EXPECT_EQ(OB_SUCCESS, cm.after_flush_config_log(cm.log_ms_meta_.curr_.config_.config_version_));
  EXPECT_EQ(OB_INVALID_TIMESTAMP, cm.register_time_us_);
  EXPECT_EQ(OB_INVALID_TIMESTAMP, cm.parent_keepalive_time_us_);
  EXPECT_EQ(OB_INVALID_TIMESTAMP, cm.last_submit_register_req_time_us_);
  EXPECT_FALSE(cm.parent_.is_valid());
  EXPECT_EQ(0, cm.children_.get_member_number());
  PALF_LOG(INFO, "test_after_flush_config_log end case");
}

TEST_F(TestLogConfigMgr, test_handle_register_parent_req)
{
  PALF_LOG(INFO, "test_handle_register_parent_req begin case");
  ObMemberList init_member_list;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_log_sync_member_list(init_member_list));
  ASSERT_EQ(OB_SUCCESS, init_config_version.generate(1, 1));
  {
    // paxos member register, ignore
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr1, config_info, cm);
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_req(LogLearner(addr3, 1), true));
    EXPECT_EQ(0, cm.children_.get_member_number());
    EXPECT_EQ(RegisterReturn::INVALID_REG_RET, mock_log_engine_->reg_ret_);
  }
  {
    // duplicate register req
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr1, config_info, cm);
    LogLearner child(addr4, 1);
    EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(child));
    cm.all_learnerlist_.add_learner(ObMember(addr4, -1));
    child.register_time_us_ = 1;
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_req(child, true));
    EXPECT_EQ(RegisterReturn::REGISTER_DONE, mock_log_engine_->reg_ret_);
  }
  {
    // register to leader
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr1, config_info, cm);
    LogLearner child1(addr4, 1);
    LogLearner exist_child(addr5, region1, 1);
    EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(child1));
    EXPECT_EQ(OB_SUCCESS, cm.children_.add_learner(exist_child));
    cm.all_learnerlist_.add_learner(ObMember(child1.get_server(), -1));
    cm.all_learnerlist_.add_learner(ObMember(exist_child.get_server(), -1));
    child1.register_time_us_ = 1;
    child1.region_ = region1;
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_req(child1, true));
    EXPECT_EQ(RegisterReturn::REGISTER_CONTINUE, mock_log_engine_->reg_ret_);
    mock_log_engine_->reset_register_parent_resp_ret();
    LogLearner child2(addr6, region2, 1);
    child2.register_time_us_ = 1;
    cm.all_learnerlist_.add_learner(ObMember(child2.get_server(), -1));
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_req(child2, true));
    EXPECT_EQ(RegisterReturn::REGISTER_DONE, mock_log_engine_->reg_ret_);
    EXPECT_TRUE(cm.children_.contains(child2));
    EXPECT_EQ(cm.self_, mock_log_engine_->parent_itself_.server_);
    EXPECT_EQ(child2.register_time_us_, mock_log_engine_->parent_itself_.register_time_us_);
  }
  {
    // register to follower
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr1, config_info, cm);
    LogLearner child1(addr4, region1, 1);
    child1.register_time_us_ = 1;
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_req(child1, false));
    EXPECT_EQ(REGISTER_DIFF_REGION, mock_log_engine_->reg_ret_);
    mock_log_engine_->reset_register_parent_resp_ret();
    LogLearner child2(addr4, 1);
    child2.register_time_us_ = 1;
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_req(child2, false));
    EXPECT_EQ(REGISTER_DONE, mock_log_engine_->reg_ret_);
    EXPECT_TRUE(cm.children_.contains(addr4));
    mock_log_engine_->reset_register_parent_resp_ret();
    cm.children_.add_learner(LogLearner(addr5, 1));
    cm.children_.add_learner(LogLearner(addr6, 1));
    cm.children_.add_learner(LogLearner(addr7, 1));
    cm.children_.add_learner(LogLearner(addr8, 1));
    LogLearner child9(addr9, 1);
    child9.register_time_us_ = 1;
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_req(child9, false));
    EXPECT_EQ(REGISTER_CONTINUE, mock_log_engine_->reg_ret_);
    PALF_LOG(INFO, "trace", K(cm.children_), K(mock_log_engine_->candidate_list_));
  }
  PALF_LOG(INFO, "test_handle_register_parent_req end case");
}

TEST_F(TestLogConfigMgr, test_handle_register_parent_resp)
{
  PALF_LOG(INFO, "test_handle_register_parent_resp begin case");
  ObMemberList init_member_list;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_log_sync_member_list(init_member_list));
  ASSERT_EQ(OB_SUCCESS, init_config_version.generate(1, 1));
  {
    // state not match
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr1, config_info, cm, FOLLOWER);
    EXPECT_EQ(OB_STATE_NOT_MATCH, cm.handle_register_parent_resp(LogLearner(addr4, 1), LogCandidateList(), REGISTER_DONE));
    // register done
    cm.register_time_us_ = 1;
    cm.last_submit_register_req_time_us_ = 1;
    LogLearner parent(addr4, 1);
    parent.register_time_us_ = 1;
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_resp(parent, LogCandidateList(), REGISTER_DONE));
    EXPECT_EQ(OB_INVALID_TIMESTAMP, cm.last_submit_register_req_time_us_);
  }
  {
    // register continue
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr1, config_info, cm, FOLLOWER);
    cm.register_time_us_ = 1;
    cm.last_submit_register_req_time_us_ = 1;
    LogLearner parent(addr4, 1);
    parent.register_time_us_ = 1;
    // cnadidate list is empty
    EXPECT_EQ(OB_ERR_UNEXPECTED, cm.handle_register_parent_resp(parent, LogCandidateList(), REGISTER_CONTINUE));
    LogCandidateList candidate_list;
    candidate_list.add_learner(common::ObMember(addr2, -1));
    // register continue
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_resp(parent, candidate_list, REGISTER_CONTINUE));
    EXPECT_GT(cm.last_submit_register_req_time_us_, 1);
    EXPECT_EQ(1, cm.register_time_us_);
  }
  {
    // register not master
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr4, config_info, cm, FOLLOWER);
    mock_state_mgr_->leader_ = addr1;
    cm.register_time_us_ = 1;
    cm.last_submit_register_req_time_us_ = 1;
    LogLearner parent(addr4, 1);
    parent.register_time_us_ = 1;
    LogCandidateList candidate_list;
    candidate_list.add_learner(common::ObMember(addr2, -1));
    // register continue
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_resp(parent, candidate_list, REGISTER_NOT_MASTER));
    EXPECT_EQ(cm.last_submit_register_req_time_us_, 1);
    EXPECT_EQ(cm.register_time_us_, 1);
    EXPECT_EQ(OB_SUCCESS, cm.handle_register_parent_resp(parent, candidate_list, REGISTER_DIFF_REGION));
    EXPECT_GT(cm.last_submit_register_req_time_us_, 1);
    EXPECT_GT(cm.register_time_us_, 1);
  }
  PALF_LOG(INFO, "test_handle_register_parent_resp end case");
}

TEST_F(TestLogConfigMgr, test_region_changed)
{
  PALF_LOG(INFO, "test_region_changed begin case");
  ObMemberList init_member_list;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_log_sync_member_list(init_member_list));
  ASSERT_EQ(OB_SUCCESS, init_config_version.generate(1, 1));
  LogConfigMgr cm;
  LogConfigInfoV2 config_info;
  EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
  init_test_log_config_env(addr5, config_info, cm, FOLLOWER);
  cm.parent_ = addr4;
  cm.register_time_us_ = 1;
  cm.parent_keepalive_time_us_ = 5;
  mock_state_mgr_->leader_ = addr1;
  EXPECT_EQ(OB_SUCCESS, cm.set_region(region1));
  EXPECT_EQ(region1, cm.region_);
  EXPECT_FALSE(cm.parent_.is_valid());
  EXPECT_GT(cm.last_submit_register_req_time_us_, 1);
  EXPECT_GT(cm.register_time_us_, 1);
  EXPECT_EQ(cm.parent_keepalive_time_us_, OB_INVALID_TIMESTAMP);
  PALF_LOG(INFO, "test_region_changed end case");
}

TEST_F(TestLogConfigMgr, test_check_children_health)
{
  PALF_LOG(INFO, "test_check_children_health begin case");
  ObMemberList init_member_list;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_log_sync_member_list(init_member_list));
  ASSERT_EQ(OB_SUCCESS, init_config_version.generate(1, 1));
  {
    // active leader
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr1, config_info, cm);
    LogLearner timeout_child(addr4, 1);
    LogLearner dup_region_child(addr5, 1);
    LogLearner normal_child(addr6, region1, 1);
    dup_region_child.keepalive_ts_ = ObTimeUtility::current_time_ns();
    normal_child.keepalive_ts_ = ObTimeUtility::current_time_ns();
    cm.children_.add_learner(timeout_child);
    cm.children_.add_learner(dup_region_child);
    cm.children_.add_learner(normal_child);
    (void) cm.check_children_health();
    EXPECT_EQ(2, cm.children_.get_member_number());
    EXPECT_TRUE(cm.children_.contains(normal_child.server_));
    EXPECT_TRUE(cm.children_.contains(dup_region_child.server_));
  }
  {
    // active follower
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr2, config_info, cm, FOLLOWER);
    LogLearner timeout_child(addr4, 1);
    LogLearner diff_region_child(addr5, region1, 1);
    LogLearner normal_child(addr6, 1);
    diff_region_child.keepalive_ts_ = ObTimeUtility::current_time_ns();
    normal_child.keepalive_ts_ = ObTimeUtility::current_time_ns();
    cm.children_.add_learner(timeout_child);
    cm.children_.add_learner(diff_region_child);
    cm.children_.add_learner(normal_child);
    (void) cm.check_children_health();
    LogLearnerList children;
    EXPECT_EQ(OB_SUCCESS, cm.get_children_list(children));
    EXPECT_EQ(1, children.get_member_number());
    EXPECT_TRUE(children.contains(normal_child.server_));
  }
  PALF_LOG(INFO, "test_check_children_health end case");
}

TEST_F(TestLogConfigMgr, test_check_parent_health)
{
  PALF_LOG(INFO, "test_check_parent_health begin case");
  ObMemberList init_member_list;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_log_sync_member_list(init_member_list));
  ASSERT_EQ(OB_SUCCESS, init_config_version.generate(1, 1));
  {
    // registering timeout
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr2, config_info, cm, FOLLOWER);
    cm.last_submit_register_req_time_us_ = 1;
    mock_state_mgr_->leader_ = addr1;
    EXPECT_EQ(OB_SUCCESS, cm.check_parent_health());
    EXPECT_GT(cm.register_time_us_, 1);
    EXPECT_GT(cm.last_submit_register_req_time_us_, 1);
  }
  {
    // first registration
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr2, config_info, cm, FOLLOWER);
    mock_state_mgr_->leader_ = addr1;
    EXPECT_EQ(OB_SUCCESS, cm.check_parent_health());
    EXPECT_GT(cm.register_time_us_, 1);
    EXPECT_GT(cm.last_submit_register_req_time_us_, 1);
  }
  {
    // parent timeout
    LogConfigMgr cm;
    LogConfigInfoV2 config_info;
    EXPECT_EQ(OB_SUCCESS, config_info.generate(init_member_list, get_default_log_sync_replica_num(), learner_list, init_config_version));
    init_test_log_config_env(addr2, config_info, cm, FOLLOWER);
    mock_state_mgr_->leader_ = addr1;
    cm.parent_ = addr4;
    EXPECT_EQ(OB_SUCCESS, cm.check_parent_health());
    EXPECT_GT(cm.register_time_us_, 1);
    EXPECT_GT(cm.last_submit_register_req_time_us_, 1);
    EXPECT_FALSE(cm.parent_.is_valid());
  }
  PALF_LOG(INFO, "test_check_parent_health end case");
}

TEST_F(TestLogConfigMgr, test_handle_retire_msg)
{
  PALF_LOG(INFO, "test_handle_retire_msg end case");
  LogConfigInfoV2 config_info;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_default_log_config_info(config_info, learner_list, init_config_version));
  {
    LogConfigMgr cm;
    init_test_log_config_env(addr4, config_info, cm, FOLLOWER);
    mock_state_mgr_->leader_ = addr1;
    cm.parent_ = addr2;
    cm.register_time_us_ = 100;
    LogLearner parent(addr2, 100);
    EXPECT_EQ(OB_SUCCESS, cm.handle_retire_child(parent));
    EXPECT_GT(cm.last_submit_register_req_time_us_, 0);
    EXPECT_FALSE(cm.parent_.is_valid());
  }
  {
    LogConfigMgr cm;
    init_test_log_config_env(addr2, config_info, cm, FOLLOWER);
    const LogLearner child = LogLearner(addr4, 100);
    cm.children_.add_learner(child);
    EXPECT_EQ(OB_SUCCESS, cm.handle_retire_parent(child));
    EXPECT_EQ(0, cm.children_.get_member_number());
  }
  {
    // different register_time_us
    LogConfigMgr cm;
    init_test_log_config_env(addr2, config_info, cm, FOLLOWER);
    LogLearner child = LogLearner(addr4, 100);
    cm.children_.add_learner(child);
    child.register_time_us_ = 200;
    EXPECT_EQ(OB_SUCCESS, cm.handle_learner_keepalive_resp(child));
    EXPECT_EQ(child.keepalive_ts_, OB_INVALID_TIMESTAMP);
  }
  {
    LogConfigMgr cm;
    init_test_log_config_env(addr2, config_info, cm, FOLLOWER);
    const LogLearner child = LogLearner(addr4, 100);
    cm.children_.add_learner(child);
    EXPECT_EQ(OB_INVALID_TIMESTAMP, child.keepalive_ts_);
    EXPECT_EQ(OB_SUCCESS, cm.handle_learner_keepalive_resp(child));
    EXPECT_GT(cm.children_.get_learner(0).keepalive_ts_, 0);
  }
  PALF_LOG(INFO, "test_handle_retire_msg end case");
}

TEST_F(TestLogConfigMgr, test_init_by_default_config_meta)
{
  const int64_t init_log_proposal_id = 0;
  LogConfigInfoV2 init_config_info;
  LogConfigVersion init_config_version;
  init_config_version.generate(init_log_proposal_id, 0);
  init_config_info.generate(init_config_version);
  LogConfigMeta log_config_meta;
  LogConfigMgr cm;
  EXPECT_EQ(OB_SUCCESS, log_config_meta.generate_for_default(init_log_proposal_id,
      init_config_info, init_config_info));
  EXPECT_EQ(OB_SUCCESS, cm.init(1, addr1, log_config_meta, mock_log_engine_,
      mock_sw_, mock_state_mgr_, mock_election_,
      mock_mode_mgr_, mock_reconfirm_, mock_plugins_));
}

TEST_F(TestLogConfigMgr, test_too_many_learners)
{
  LogConfigInfoV2 config_info;
  GlobalLearnerList learner_list;
  LogConfigVersion init_config_version;
  ASSERT_EQ(OB_SUCCESS, build_default_log_config_info(config_info, learner_list, init_config_version));
  LogConfigMgr cm;
  init_test_log_config_env(addr1, config_info, cm, LEADER);

  ObAddr::VER ip_type = ObAddr::IPV4;
  const char* addr_str = "255.255.255.255";
  const int32_t port = INT32_MAX;
  for (int i = 0; i < 200; i++) {
    EXPECT_EQ(OB_SUCCESS, cm.log_ms_meta_.curr_.config_.learnerlist_.add_server(ObAddr(ip_type, addr_str, port - i)));
  }

  // test add_learner
  LogConfigChangeArgs args(ObMember(addr4, -1), 0, ADD_LEARNER);
  bool unused_bool = false;
  EXPECT_EQ(OB_INVALID_ARGUMENT, cm.check_config_change_args_(args, unused_bool));
}

} // end namespace unittest
} // end namespace oceanbase

int main(int argc, char **argv)
{
  const std::string rm_base_dir_cmd = "rm -f test_log_config_mgr.log";
  system(rm_base_dir_cmd.c_str());
  OB_LOGGER.set_file_name("test_log_config_mgr.log", true);
  OB_LOGGER.set_log_level("INFO");
  PALF_LOG(INFO, "begin unittest::test_log_config_mgr");
  ::testing::InitGoogleTest(&argc, argv);
  ObClusterVersion::get_instance().update_data_version(DATA_CURRENT_VERSION);
  return RUN_ALL_TESTS();
}
