// owner: yunxing.cyx
// owner group: transaction

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

#define USING_LOG_PREFIX SQL
#define private public
#define protected public
#include "observer/ob_server.h"
#include "observer/omt/ob_tenant_timezone_mgr.h"
#include "mock_tenant_module_env.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::transaction;
namespace oceanbase
{
namespace sql
{
class TestSessionSerDe : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    LOG_INFO("SetUpTestCase");
    EXPECT_EQ(OB_SUCCESS, MockTenantModuleEnv::get_instance().init());
  }
  static void TearDownTestCase()
  {
    LOG_INFO("TearDownTestCase");
    MockTenantModuleEnv::get_instance().destroy();
  }

  void SetUp()
  {
    ASSERT_TRUE(MockTenantModuleEnv::get_instance().is_inited());
  }
};

static ObTZInfoMap map;
int tz_map_getter(const uint64_t tenant_id,
                  ObTZMapWrap &timezone_wrap) {
  UNUSED(tenant_id);
  timezone_wrap.set_tz_map(&map);
  return OB_SUCCESS;
}

TEST_F(TestSessionSerDe, tx_desc)
{
  common::ObArenaAllocator allocator;
  ObSQLSessionInfo session;
  ObPhysicalPlan plan;
  session.cur_phy_plan_ = &plan;
  OBSERVER.init_schema();
  OBSERVER.init_tz_info_mgr();
  omt::ObTenantTimezoneMgr::get_instance().init(tz_map_getter);
  ASSERT_EQ(OB_SUCCESS, ObPreProcessSysVars::init_sys_var());
  session.test_init(1, 1, 1, &allocator);
  session.load_all_sys_vars_default();
  ObTxDesc tx;
  tx.tenant_id_ = 1;
  ASSERT_EQ(tx.flags_.SHADOW_, false);
  ObTxPart p;
  p.id_ = 1001;
  tx.tx_id_ = ObTransID(100);
  tx.state_ = ObTxDesc::State::IDLE;
  tx.parts_.push_back(p);
  session.get_tx_desc() = &tx;
  DEFER(session.get_tx_desc() = NULL);
  int ret = OB_SUCCESS;
  const int64_t buf_len = 4096;
  int64_t pos = 0;
  char buf[buf_len];
  MEMSET(buf, 0, sizeof(buf));
  ret = serialization::encode(buf, buf_len, pos, session);
  ASSERT_EQ(OB_SUCCESS, ret);
  ObSQLSessionInfo session2;
  pos = 0;
  ret = serialization::decode(buf, buf_len, pos, session2);
  session2.is_inited_ = true;
  ASSERT_EQ(OB_SUCCESS, ret);
  ObTxDesc *tx2 = session2.get_tx_desc();
  ASSERT_EQ(tx2->tx_id_, tx.tx_id_);
  ASSERT_EQ(tx2->tx_id_.get_id(), 100);
  ASSERT_EQ(tx2->flags_.SHADOW_, true);
  ASSERT_EQ(tx2->state_, ObTxDesc::State::IDLE);
  ASSERT_EQ(tx2->parts_.count(), 1);
  ASSERT_EQ(tx2->parts_[0].id_, tx.parts_[0].id_);
  LOG_INFO("x", KP(&session), KP(&session2));
}
}
}


int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  OB_LOGGER.set_file_name("test_session_serde.log", true);
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
