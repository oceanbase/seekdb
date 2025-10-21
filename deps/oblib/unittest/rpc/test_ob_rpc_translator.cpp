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
#include "rpc/obrpc/ob_rpc_packet.h"
#include "observer/ob_rpc_translator.h"

using namespace oceanbase::observer;
using namespace oceanbase::common;

class TestObRpcTranslator
    : public ::testing::Test
{
public:
  TestObRpcTranslator()
      : t_(gctx_)
  {}

  virtual void SetUp()
  {
  }

  virtual void TearDown()
  {
  }
protected:
  ObGlobalContext gctx_;
  ObRpcTranslator t_;
};

TEST_F(TestObRpcTranslator, TestName)
{
  ObRequestProcessor *p = NULL;
  ObPacket pkt;

  EXPECT_EQ(OB_SUCCESS, t_.init_thread_local());

  EXPECT_EQ(OB_ERR_NULL_VALUE, t_.translate_packet(NULL, p));
  EXPECT_TRUE(NULL == p);

  pkt.set_pcode(OB_SET_CONFIG);
  EXPECT_EQ(OB_INNER_STAT_ERROR, t_.translate_packet(&pkt, p));
  EXPECT_TRUE(NULL == p);

  pkt.set_target_id(OB_SELF_FLAG);
  EXPECT_EQ(OB_SUCCESS, t_.translate_packet(&pkt, p));
  EXPECT_FALSE(NULL == p);

  pkt.set_pcode(OB_GET_REQUEST);
  EXPECT_EQ(OB_NOT_SUPPORTED, t_.translate_packet(&pkt, p));
  EXPECT_TRUE(NULL == p);
}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
