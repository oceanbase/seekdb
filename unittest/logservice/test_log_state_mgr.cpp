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
#include "logservice/palf/log_state_mgr.h"
#include "mock_logservice_container/mock_log_mode_mgr.h"
#include "mock_logservice_container/mock_log_sliding_window.h"
#undef private

namespace oceanbase
{
namespace unittest
{
using namespace common;
using namespace palf;

class TestLogStateMgr : public ::testing::Test
{
public:
  void SetUp() override
  {
    self_.set_ip_addr("127.0.0.1", 12345);
  }

protected:
  common::ObAddr self_;
  MockLogSlidingWindow mock_sw_;
  MockLogModeMgr mock_mode_mgr_;
  LogStateMgr state_mgr_;
};

TEST_F(TestLogStateMgr, init)
{
  ObAddr invalid_addr;
  EXPECT_EQ(OB_NOT_INIT, state_mgr_.set_scan_disk_log_finished());
  EXPECT_EQ(OB_NOT_INIT, state_mgr_.switch_state());
  EXPECT_FALSE(state_mgr_.can_slide_sw());
  EXPECT_FALSE(state_mgr_.can_append());
  EXPECT_EQ(INIT, state_mgr_.get_state());

  EXPECT_EQ(OB_INVALID_ARGUMENT,
            state_mgr_.init(invalid_addr, &mock_sw_, &mock_mode_mgr_));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            state_mgr_.init(self_, nullptr, &mock_mode_mgr_));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            state_mgr_.init(self_, &mock_sw_, nullptr));
  EXPECT_EQ(OB_SUCCESS, state_mgr_.init(self_, &mock_sw_, &mock_mode_mgr_));
  EXPECT_EQ(OB_INIT_TWICE, state_mgr_.init(self_, &mock_sw_, &mock_mode_mgr_));
  EXPECT_TRUE(state_mgr_.can_slide_sw());
  EXPECT_EQ(INIT, state_mgr_.get_state());

  state_mgr_.destroy();
  EXPECT_FALSE(state_mgr_.can_slide_sw());
  EXPECT_EQ(INIT, state_mgr_.get_state());
}

TEST_F(TestLogStateMgr, local_recovery_transitions)
{
  ASSERT_EQ(OB_SUCCESS, state_mgr_.init(self_, &mock_sw_, &mock_mode_mgr_));
  EXPECT_FALSE(state_mgr_.is_state_changed());
  EXPECT_FALSE(state_mgr_.is_active());
  EXPECT_FALSE(state_mgr_.is_recovering());
  EXPECT_FALSE(state_mgr_.need_freeze_group_buffer());

  // INIT does not advance until the disk scan is complete.
  ASSERT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_EQ(INIT, state_mgr_.get_state());

  ASSERT_EQ(OB_SUCCESS, state_mgr_.set_scan_disk_log_finished());
  EXPECT_TRUE(state_mgr_.is_state_changed());
  ASSERT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_TRUE(state_mgr_.is_recovering());
  EXPECT_EQ(RECOVERING, state_mgr_.get_state());

  // Recovery waits for both local flush and committed-log sliding.
  mock_sw_.all_log_flushed_ = false;
  ASSERT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_TRUE(state_mgr_.is_recovering());
  EXPECT_FALSE(mock_sw_.activate_called_);

  mock_sw_.all_log_flushed_ = true;
  mock_sw_.all_committed_slided_ = false;
  mock_sw_.max_lsn_ = LSN(1024);
  ASSERT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_TRUE(state_mgr_.is_recovering());
  EXPECT_EQ(LSN(1024), mock_sw_.committed_end_lsn_);
  EXPECT_FALSE(mock_sw_.activate_called_);

  mock_sw_.all_committed_slided_ = true;
  ASSERT_EQ(OB_SUCCESS, state_mgr_.switch_state());
  EXPECT_TRUE(mock_sw_.activate_called_);
  EXPECT_TRUE(state_mgr_.is_active());
  EXPECT_TRUE(state_mgr_.can_append());
  EXPECT_TRUE(state_mgr_.need_freeze_group_buffer());
  EXPECT_EQ(ACTIVE, state_mgr_.get_state());
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_state_mgr.log", true);
  OB_LOGGER.set_log_level("TRACE");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
