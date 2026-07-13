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
#include "mock_logservice_container/mock_log_engine.h"
#include "mock_logservice_container/mock_log_sliding_window.h"
#include "mock_logservice_container/mock_log_mode_mgr.h"
#undef private

namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace share;

namespace unittest
{

class TestLogStateMgr: public ::testing::Test
{
public:
  TestLogStateMgr();
  virtual ~TestLogStateMgr();
public:
  virtual void SetUp();
  virtual void TearDown();
public:
  common::ObAddr self_;
  MockLogSlidingWindow mock_sw_;
  MockLogEngine mock_log_engine_;
  MockLogModeMgr mock_mode_mgr_;
  LogStateMgr state_mgr_;
};

TestLogStateMgr::TestLogStateMgr() {}
TestLogStateMgr::~TestLogStateMgr() {}

void TestLogStateMgr::SetUp()
{
  self_.set_ip_addr("127.0.0.1", 12345);
}

void TestLogStateMgr::TearDown()
{}

TEST_F(TestLogStateMgr, test_init)
{
  EXPECT_EQ(OB_INVALID_ARGUMENT, state_mgr_.init(self_, NULL, &mock_mode_mgr_));
  EXPECT_EQ(OB_SUCCESS, state_mgr_.init(self_, &mock_sw_, &mock_mode_mgr_));
  EXPECT_EQ(OB_INIT_TWICE, state_mgr_.init(self_, &mock_sw_, &mock_mode_mgr_));
}

TEST_F(TestLogStateMgr, recover_to_active)
{
  EXPECT_EQ(OB_SUCCESS, state_mgr_.init(self_, &mock_sw_, &mock_mode_mgr_));
  EXPECT_EQ(INIT, state_mgr_.get_state());
  EXPECT_EQ(OB_SUCCESS, state_mgr_.set_scan_disk_log_finished());
  EXPECT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_TRUE(state_mgr_.is_recovering());

  mock_sw_.all_log_flushed_ = false;
  EXPECT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_TRUE(state_mgr_.is_recovering());
  EXPECT_FALSE(mock_sw_.activated_);

  mock_sw_.all_log_flushed_ = true;
  mock_sw_.all_committed_slided_out_ = false;
  EXPECT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_TRUE(state_mgr_.is_recovering());
  EXPECT_EQ(mock_sw_.mock_max_flushed_end_lsn_, mock_sw_.pending_end_lsn_);

  mock_sw_.all_committed_slided_out_ = true;
  EXPECT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_TRUE(state_mgr_.is_active());
  EXPECT_TRUE(mock_sw_.activated_);
}

} // END of unittest
} // end of oceanbase

int main(int argc, char **argv)
{
  system("rm -f ./test_log_state_mgr.log");
  OB_LOGGER.set_file_name("test_log_state_mgr.log", true);
  OB_LOGGER.set_log_level("TRACE");
  PALF_LOG(INFO, "begin unittest::test_log_state_mgr");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
