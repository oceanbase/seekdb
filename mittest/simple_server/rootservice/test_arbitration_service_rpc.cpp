// owner: jingyu.cr 
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

#define USING_LOG_PREFIX SHARE

#include <gmock/gmock.h>
#include "env/ob_simple_cluster_test_base.h"



namespace oceanbase
{
using namespace unittest;
namespace share
{
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

using namespace schema;
using namespace common;

class TestArbitrationServiceRpc : public unittest::ObSimpleClusterTestBase
{
public:
  TestArbitrationServiceRpc() : unittest::ObSimpleClusterTestBase("test_arbitration_service_rpc") {}
};

TEST_F(TestArbitrationServiceRpc, test_argument)
{
  int ret = OB_SUCCESS;
  
  uint64_t tenant_id = 1001;
  share::ObLSID ls_id(1);
  share::ObLSID invalid_ls_id(1001);
  common::ObAddr dst_server(ObAddr::VER::IPV4, "127.0.0.1", 1080);
  common::ObMember invalid_member;
  int64_t timestamp_for_arb_member = ObTimeUtility::current_time();
  common::ObMember arb_member(dst_server, timestamp_for_arb_member);
  int64_t timeout_us = 180 * 1000 * 1000; //180s
  int64_t invalid_timeout_us = OB_INVALID_TIMESTAMP;

  ObAddArbArg add_arg;
  ret = add_arg.init(tenant_id, invalid_ls_id, arb_member, timeout_us);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = add_arg.init(tenant_id, ls_id, invalid_member, timeout_us);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = add_arg.init(tenant_id, ls_id, arb_member, invalid_timeout_us);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = add_arg.init(tenant_id, ls_id, arb_member, timeout_us);
  ASSERT_EQ(OB_SUCCESS, ret);

  ObRemoveArbArg remove_arg;
  ret = remove_arg.init(tenant_id, invalid_ls_id, arb_member, timeout_us);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = remove_arg.init(tenant_id, ls_id, invalid_member, timeout_us);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = remove_arg.init(tenant_id, ls_id, arb_member, invalid_timeout_us);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = remove_arg.init(tenant_id, ls_id, arb_member, timeout_us);
  ASSERT_EQ(OB_SUCCESS, ret);

  ObCreateArbArg create_arg;
  share::ObTenantRole tenant_role(ObTenantRole::PRIMARY_TENANT);
  share::ObTenantRole invalid_tenant_role(ObTenantRole::INVALID_TENANT);
  ret = create_arg.init(tenant_id, invalid_ls_id, tenant_role);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = create_arg.init(tenant_id, ls_id, invalid_tenant_role);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = create_arg.init(tenant_id, ls_id, tenant_role);
  ASSERT_EQ(OB_SUCCESS, ret);

  ObDeleteArbArg delete_arg;
  ret = delete_arg.init(tenant_id, invalid_ls_id);
  ASSERT_EQ(OB_INVALID_ARGUMENT, ret);
  ret = delete_arg.init(tenant_id, ls_id);
  ASSERT_EQ(OB_SUCCESS, ret);
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
