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
#include "rpc/ob_request.h"
#include "observer/ob_srv_deliver.h"
#include "observer/omt/ob_worker_processor.h"
#include "observer/omt/ob_multi_tenant.h"

using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::obrpc;
using namespace oceanbase::rpc;
using namespace oceanbase::omt;

class ObMockReqHandler
    : public ObiReqQHandler
{
public:
  virtual int onThreadCreated(obsys::CThread *)
  {
    return OB_SUCCESS;
  }
  virtual int onThreadDestroy(obsys::CThread *)
  {
    return OB_SUCCESS;
  }

  virtual bool handlePacketQueue(ObRequest *, void*)
  {
    return OB_SUCCESS;
  }
};

ObRpcSessionHandler sh;
ObMockReqHandler mrh;
ObFakeWorkerProcessor fwp;
ObMultiTenant omt(fwp);

class TestDeliver
    : public ::testing::Test, public ObSrvDeliver
{
public:
  TestDeliver()
      : ObSrvDeliver(mrh, sh, GCTX)
  {}

  virtual void SetUp()
  {
    ASSERT_EQ(OB_SUCCESS, init());
  }

  virtual void TearDown()
  {
  }
};


TEST_F(TestDeliver, NotInit)
{
  ObRpcPacket pkt;
  ObRequest req(ObRequest::OB_RPC);
  req.set_packet(&pkt);
  int ret = deliver(req);
  EXPECT_EQ(OB_NOT_INIT, ret);
}

TEST_F(TestDeliver, Norm)
{
  GCTX.omt_ = &omt;
  GCTX.status_ = SS_SERVING;
  ObRpcPacket pkt;
  ObRequest req(ObRequest::OB_RPC);
  req.set_packet(&pkt);
  int ret = deliver(req);
  EXPECT_EQ(OB_TENANT_NOT_IN_SERVER, ret);
  pkt.set_pcode(OB_RENEW_LEASE);
  ret = deliver(req);
  EXPECT_EQ(OB_SUCCESS, ret);
  pkt.set_priority(10);
  ret = deliver(req);
  EXPECT_EQ(OB_SUCCESS, ret);
  sleep(1);
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
