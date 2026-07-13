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
#include "share/ob_cluster_version.h"
#define private public
#include "logservice/palf/log_mode_mgr.h"
#include "mock_logservice_container/mock_log_sliding_window.h"
#include "mock_logservice_container/mock_log_engine.h"
#undef private
namespace oceanbase
{
namespace unittest
{
using namespace common;
using namespace share;
using namespace palf;

const ObAddr addr1(ObAddr::IPV4, "127.0.0.1", 1000);

class TestLogModeMgr : public ::testing::Test
{
public:
  TestLogModeMgr()
  {
    mock_state_mgr_ = OB_NEW(MockLogStateMgr, "TestLog");
    mock_sw_ = OB_NEW(MockLogSlidingWindow, "TestLog");
    mock_log_engine_ = OB_NEW(MockLogEngine, "TestLog");
  }
  ~TestLogModeMgr()
  {
    OB_DELETE(MockLogStateMgr, "TestLog", mock_state_mgr_);
    OB_DELETE(MockLogSlidingWindow, "TestLog", mock_sw_);
    OB_DELETE(MockLogEngine, "TestLog", mock_log_engine_);
  }
public:
  palf::MockLogStateMgr *mock_state_mgr_;
  palf::MockLogSlidingWindow *mock_sw_;
  palf::MockLogEngine *mock_log_engine_;
};

TEST_F(TestLogModeMgr, test_init)
{
  PALF_LOG(INFO, "test_init case");
  LogModeMgr mode_mgr;
  LogModeMeta valid_meta, invalid_meta;
  ObAddr invalid_addr;
  EXPECT_EQ(OB_SUCCESS, valid_meta.generate(AccessMode::APPEND, share::SCN::base_scn()));
  EXPECT_EQ(OB_INVALID_ARGUMENT, mode_mgr.init(invalid_addr, valid_meta));
  EXPECT_EQ(OB_INVALID_ARGUMENT, mode_mgr.init(addr1, invalid_meta));
  EXPECT_EQ(OB_SUCCESS, mode_mgr.init(addr1, valid_meta));
  EXPECT_EQ(OB_INIT_TWICE, mode_mgr.init(addr1, valid_meta));
  PALF_LOG(INFO, "test_init case");
}

TEST_F(TestLogModeMgr, test_can_interface)
{
  PALF_LOG(INFO, "test_can_interface case");
  LogModeMgr mode_mgr;
  mode_mgr.applied_mode_meta_.access_mode_ = AccessMode::APPEND;
  EXPECT_TRUE(mode_mgr.can_append());
  PALF_LOG(INFO, "test_can_interface case");
}

} // end namespace unittest
} // end namespace oceanbase

int main(int argc, char **argv)
{
  const std::string rm_base_dir_cmd = "rm -f test_log_mode_mgr.log";
  system(rm_base_dir_cmd.c_str());
  OB_LOGGER.set_file_name("test_log_mode_mgr.log", true);
  OB_LOGGER.set_log_level("INFO");
  PALF_LOG(INFO, "begin unittest::test_log_mode_mgr");
  ::testing::InitGoogleTest(&argc, argv);
  oceanbase::ObClusterVersion::get_instance().update_data_version(DATA_CURRENT_VERSION);
  return RUN_ALL_TESTS();
}
