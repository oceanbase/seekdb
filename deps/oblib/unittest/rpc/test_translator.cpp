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

#include <sys/time.h>
#include <gtest/gtest.h>
#include "rpc/ob_request.h"
#include "rpc/obmysql/ob_mysql_packet.h"
#include "observer/ob_srv_deliver.h"
#include "observer/ob_srv_xlator.h"


using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::obmysql;
using namespace oceanbase::obrpc;
using namespace oceanbase::rpc;
using namespace oceanbase::omt;


class TestDeliver
    : public ::testing::Test
{
public:
  TestDeliver()
  {}

  virtual void SetUp()
  {
  }

  virtual void TearDown()
  {
  }
};


TEST_F(TestDeliver, time_cost)
{
  ObGlobalContext gctx;
  ObRequest req(ObRequest::OB_MYSQL);
  ObMySQLRawPacket pkt;
  ObSrvMySQLXlator xlator(gctx);
  ObReqProcessor *processor =  NULL;

  pkt.set_cmd(oceanbase::obmysql::COM_PING);
  req.set_packet(&pkt);


  timeval start, end;
  gettimeofday(&start, NULL);

  int times = 100000;
  for (int i = 0; i < times; ++i) {
    int ret = xlator.translate(req, processor);
    EXPECT_EQ(OB_SUCCESS, ret);
  }

  gettimeofday(&end, NULL);
  int64_t use_time = (long int)(end.tv_sec - start.tv_sec) * 1000000 + (long int)(end.tv_usec - start.tv_usec);;
  std::cout << " use time :" << use_time / times << std::endl;
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
