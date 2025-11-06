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

#ifndef OCEANBASE_RPC_OBRPC_OB_RPC_TRANSLATOR_
#define OCEANBASE_RPC_OBRPC_OB_RPC_TRANSLATOR_

#include "rpc/frame/ob_req_translator.h"
#include "rpc/obrpc/ob_rpc_session_handler.h"
#include "ob_rpc_packet.h"

namespace oceanbase
{
namespace common
{
class ObDataBuffer;
} // end of namespace common
namespace rpc
{
class ObRequest;
namespace frame
{
class ObReqProcessor;
} // end of namespace frame
} // end of namespace rpc


namespace obrpc
{

class ObRpcStreamCond;

class ObRpcTranslator
    : public rpc::frame::ObReqTranslator
{
public:
  int th_init();
  int th_destroy();

  inline ObRpcSessionHandler &get_session_handler();

protected:
  rpc::frame::ObReqProcessor* get_processor(rpc::ObRequest &req) = 0;

protected:
  ObRpcSessionHandler session_handler_;
}; // end of class ObRpcTranslator

//////////////////////////////////////////////////////////////
//  inline functions definition
///
ObRpcSessionHandler &ObRpcTranslator::get_session_handler()
{
  return session_handler_;
}

} // end of namespace obrpc
} // end of namespace oceanbase

#endif //OCEANBASE_RPC_OBRPC_OB_RPC_TRANSLATOR_
