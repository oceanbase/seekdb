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
#include "logservice/ob_log_service.h"

namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace share;
using namespace logservice;
namespace unittest
{

TEST(TestLogSharedStorageService, test_is_valid)
{
  ObSharedLogService shared_log;
  logservice::ObLogService log_service;
  const uint64_t tenant_id = 1002;
  ObSharedLogUploadHandler *handler = NULL;

  ASSERT_EQ(OB_NOT_INIT, shared_log.add_ls(ObLSID(1)));
  ASSERT_EQ(OB_NOT_INIT, shared_log.remove_ls(ObLSID(1)));
  ASSERT_EQ(OB_NOT_INIT, shared_log.get_log_ss_handler(ObLSID(1), handler));

  const ObAddr addr(ObAddr::IPV4, "127.0.0.1", 1000);
  common::ObMySQLProxy sql_proxy;
  obrpc::ObLogServiceRpcProxy rpc_proxy;
  ObLocationAdapter location_adapter;
  palf::LogSharedQueueTh log_shared_queue_th;
  ObTenantMutilAllocator allocator(tenant_id);
  ASSERT_EQ(OB_SUCCESS, shared_log.init(tenant_id, addr, &log_service,
      &sql_proxy, &rpc_proxy, &location_adapter, &allocator, &log_shared_queue_th));

  ASSERT_EQ(OB_INVALID_ARGUMENT, shared_log.add_ls(ObLSID()));
  ASSERT_EQ(OB_INVALID_ARGUMENT, shared_log.remove_ls(ObLSID()));
  ASSERT_EQ(OB_INVALID_ARGUMENT, shared_log.get_log_ss_handler(ObLSID(), handler));

}

} //end of namespace logservice
}//end of namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_ss_service.log", true);
  OB_LOGGER.set_log_level("INFO");
  PALF_LOG(INFO, "begin unittest::test_log_ss_service");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
