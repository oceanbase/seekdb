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

#include "rpc/obrpc/ob_rpc_proxy.h"
#include "share/config/ob_server_config.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace observer
{
class ObSrvNetworkFrame;
} // end of namespace observer
} // end of namespace oceanbase

namespace oceanbase
{
namespace obrpc
{
RPC_S_S(@PR5 test, OB_TEST_PCODE);
class TestProxy
    : public ObRpcProxy
{
public:
  DEFINE_TO(TestProxy);

  RPC_S_M(@PR5 test, OB_TEST_PCODE);
};

} // end of namespace obrpc
} // end of namespace oceanbase

namespace oceanbase { namespace unittest {

class Client
    : public share::ObThreadPool
{
public:
  Client(const obrpc::TestProxy &proxy)
      : proxy_(proxy)
  {}

  virtual void run1();

private:
  const obrpc::TestProxy &proxy_;
}; // end of class Client

int start_frame(observer::ObSrvNetworkFrame &frame);
bool parse_arg(int argc, char *argv[]);

}}
