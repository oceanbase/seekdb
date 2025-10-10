/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
