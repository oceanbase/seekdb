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
#define USING_LOG_PREFIX RPC_TEST

#include <gtest/gtest.h>
#include "rpc/obrpc/ob_rpc_packet.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_net_handler.h"
#include "test_obrpc_util.h"

using namespace oceanbase;
using namespace oceanbase::rpc;
using namespace oceanbase::obrpc;
using namespace oceanbase::common;

class TestObrpcPacket
    : public ::testing::Test
{
public:
  virtual void SetUp()
  {
  }

  virtual void TearDown()
  {
  }
};

class TestPacketProxy
    : public ObRpcProxy
{
public:
  DEFINE_TO(TestPacketProxy);
  RPC_S(@PR5 test_overflow, OB_TEST2_PCODE, (uint64_t));
};
class SimpleProcessor
    : public TestPacketProxy::Processor<OB_TEST2_PCODE>
{
public:
  SimpleProcessor(): ret_code_(OB_SUCCESS) {}
  int ret_code_;
protected:
  int process()
  {
    return OB_SUCCESS;
  }
  int response(const int retcode) {
    ret_code_ = retcode;
    return OB_SUCCESS;
  }

};

TEST_F(TestObrpcPacket, NameIndex)
{
  ObRpcPacketSet &set = ObRpcPacketSet::instance();
  EXPECT_EQ(OB_RENEW_LEASE, set.pcode_of_idx(set.idx_of_pcode(OB_RENEW_LEASE)));
  EXPECT_EQ(OB_BOOTSTRAP, set.pcode_of_idx(set.idx_of_pcode(OB_BOOTSTRAP)));
  EXPECT_STREQ("OB_BOOTSTRAP", set.name_of_idx(set.idx_of_pcode(OB_BOOTSTRAP)));
}

int main(int argc, char *argv[])
{
  OB_LOGGER.set_log_level("INFO");
  OB_LOGGER.set_file_name("test_obrpc_packet.log", true);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
