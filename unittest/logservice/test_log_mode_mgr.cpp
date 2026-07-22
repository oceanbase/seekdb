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
#include "logservice/palf/log_mode_mgr.h"
#undef private

namespace oceanbase
{
namespace unittest
{
using namespace common;
using namespace palf;

class TestLogModeMgr : public ::testing::Test
{
public:
  void SetUp() override
  {
    self_.set_ip_addr("127.0.0.1", 1000);
    ASSERT_EQ(OB_SUCCESS,
              initial_meta_.generate(AccessMode::APPEND, share::SCN::base_scn()));
  }

  int init(LogModeMgr &mode_mgr)
  {
    return mode_mgr.init(self_, initial_meta_);
  }

protected:
  ObAddr self_;
  LogModeMeta initial_meta_;
};

TEST_F(TestLogModeMgr, init)
{
  LogModeMgr mode_mgr;
  LogModeMeta invalid_meta;
  ObAddr invalid_addr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, mode_mgr.init(invalid_addr, initial_meta_));
  EXPECT_EQ(OB_INVALID_ARGUMENT, mode_mgr.init(self_, invalid_meta));
  EXPECT_EQ(OB_SUCCESS, init(mode_mgr));
  EXPECT_EQ(OB_INIT_TWICE, init(mode_mgr));
}

TEST_F(TestLogModeMgr, access_mode_queries)
{
  LogModeMgr mode_mgr;
  AccessMode access_mode = AccessMode::INVALID_ACCESS_MODE;
  share::SCN ref_scn;
  EXPECT_EQ(OB_NOT_INIT, mode_mgr.get_access_mode(access_mode));
  EXPECT_EQ(OB_NOT_INIT, mode_mgr.get_access_mode_ref_scn(access_mode, ref_scn));
  ASSERT_EQ(OB_SUCCESS, init(mode_mgr));

  EXPECT_EQ(OB_SUCCESS, mode_mgr.get_access_mode(access_mode));
  EXPECT_EQ(AccessMode::APPEND, access_mode);
  EXPECT_EQ(OB_SUCCESS, mode_mgr.get_access_mode_ref_scn(access_mode, ref_scn));
  EXPECT_EQ(initial_meta_.ref_scn_, ref_scn);
  EXPECT_TRUE(mode_mgr.can_append());

  mode_mgr.destroy();
  EXPECT_EQ(OB_NOT_INIT, mode_mgr.get_access_mode(access_mode));
  EXPECT_FALSE(mode_mgr.can_append());
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_mode_mgr.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
