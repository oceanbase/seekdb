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

#include "rpc/obrpc/ob_net_client.h"
#include <gtest/gtest.h>

using namespace oceanbase::obrpc;
using namespace oceanbase::common;

class TestNetClient
    : public ::testing::Test
{
public:
  virtual void SetUp()
  {
    client_.init();
  }

  virtual void TearDown()
  {
    client_.destroy();
  }

protected:
  ObNetClient client_;
};

TEST_F(TestNetClient, TestName)
{

}

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
