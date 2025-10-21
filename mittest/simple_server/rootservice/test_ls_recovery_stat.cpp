// owner: msy164651 
// owner group: rs

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

#define USING_LOG_PREFIX RS 

#include <gmock/gmock.h>
#define  private public
#define  protected public

#include "env/ob_simple_cluster_test_base.h"

namespace oceanbase
{
using namespace unittest;
using namespace share;
using namespace common;
namespace rootserver
{
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
class TestLSRecoveryGuard : public unittest::ObSimpleClusterTestBase
{
public:
  TestLSRecoveryGuard() : unittest::ObSimpleClusterTestBase("test_ls_recovery_stat") {}
protected:

  uint64_t tenant_id_;
};

TEST_F(TestLSRecoveryGuard, sys_recovery_guard)
{
  int ret = OB_SUCCESS;
  {
    // Do not init directly destruct
    ObLSRecoveryGuard guard;
  }
  {
    // init system tenant, destructor
    ObLSRecoveryGuard guard;
    ASSERT_EQ(OB_SUCCESS, guard.init(OB_SYS_TENANT_ID, SYS_LS));
  }
  {
    // init non-existent tenant, or log stream
    ObLSRecoveryGuard guard;
    ASSERT_EQ(OB_TENANT_NOT_IN_SERVER, guard.init(1002, SYS_LS));
  }
}

TEST_F(TestLSRecoveryGuard, user_recovery_guard)
{
  int ret = OB_SUCCESS;
  ASSERT_EQ(OB_SUCCESS, create_tenant());
  tenant_id_ = 1002;
  ObSqlString sql;
  int64_t row = 0;
  ret = sql.assign_fmt("alter system switchover to standby tenant 'tt1'");
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = get_curr_simple_server().init_sql_proxy();
  common::ObMySQLProxy &sql_proxy = get_curr_simple_server().get_sql_proxy();
  ret = sql_proxy.write(sql.ptr(), row);
  ASSERT_EQ(OB_SUCCESS, ret);

  SCN readable_scn;
  palf::LogConfigVersion config_version;
  {
    ObLSRecoveryGuard guard;
    ObLSID ls_id(10000000);
    ASSERT_EQ(OB_LS_NOT_EXIST, guard.init(tenant_id_, ls_id));
  }
  {
    ObLSRecoveryGuard guard;
    // Lock acquisition successful, no longer report, but can be included in statistics
    ASSERT_EQ(OB_SUCCESS, guard.init(tenant_id_, SYS_LS, 300 * 1000));
    usleep(3000 * 1000);//sleep 300ms, should be set to the latest
    readable_scn = guard.ls_recovery_stat_->readable_scn_upper_limit_;
    // The scn in memory can still be increased
    SCN readable_scn_memory = guard.ls_recovery_stat_->replicas_scn_.at(0).get_readable_scn();
    config_version = guard.ls_recovery_stat_->config_version_in_inner_; 
    ASSERT_EQ(1, guard.ls_recovery_stat_->ref_cnt_);
    ObLSRecoveryGuard guard1;
    // Cannot acquire lock successfully
    ASSERT_EQ(OB_EAGAIN, guard1.init(tenant_id_, SYS_LS, 2 * 1000 * 1000));
    ASSERT_EQ(OB_INIT_TWICE, guard.init(tenant_id_, SYS_LS));
    ASSERT_EQ(1, guard.ls_recovery_stat_->ref_cnt_);
    ASSERT_EQ(OB_SUCCESS, guard.ls_recovery_stat_->reset_inner_readable_scn());
    usleep(3000 * 1000);//sleep 300ms, should be set to the latest
    ASSERT_EQ(readable_scn.get_val_for_sql(), guard.ls_recovery_stat_->readable_scn_upper_limit_.get_val_for_sql());
  }
  
} 
} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::unittest::init_log_and_gtest(argc, argv);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
