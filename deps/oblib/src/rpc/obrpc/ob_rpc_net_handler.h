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

#ifndef OCEANBASE_RPC_OBRPC_OB_RPC_NET_HANDLER_
#define OCEANBASE_RPC_OBRPC_OB_RPC_NET_HANDLER_

#include "lib/ob_define.h"
#include "rpc/frame/ob_req_handler.h"
#include "rpc/obrpc/ob_rpc_protocol_processor.h"
#include "rpc/obrpc/ob_rpc_compress_protocol_processor.h"

namespace oceanbase
{
namespace obrpc
{

class ObRpcNetHandler
    : public rpc::frame::ObReqHandler
{
public:
public:
  static constexpr const int64_t CLUSTER_ID = 1;
  static uint64_t CLUSTER_NAME_HASH;
  static bool is_self_cluster(int64_t cluster_id)
  {
    return ObRpcNetHandler::CLUSTER_ID == cluster_id &&
           ObRpcNetHandler::CLUSTER_ID != OB_INVALID_CLUSTER_ID &&
           cluster_id != OB_INVALID_CLUSTER_ID;
  }
}; // end of class ObRpcNetHandler

} // end of namespace obrpc
} // end of namespace oceanbase

#endif //OCEANBASE_RPC_OBRPC_OB_RPC_NET_HANDLER_
