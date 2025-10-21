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

#ifndef OB_RPC_SHARE_H
#define OB_RPC_SHARE_H

#include "rpc/obrpc/ob_rpc_proxy.h"

namespace oceanbase {
namespace rpc {
namespace frame {
class ObReqTransport;
}  // frame
}  // rpc

namespace share {
extern rpc::frame::ObReqTransport *g_obrpc_transport;

OB_INLINE void set_obrpc_transport(rpc::frame::ObReqTransport *obrpc_transport)
{
  g_obrpc_transport = obrpc_transport;
}

OB_INLINE int init_obrpc_proxy(obrpc::ObRpcProxy &proxy)
{
  return proxy.init(g_obrpc_transport);
}

}  // share
}  // oceanbase

#endif /* OB_RPC_SHARE_H */
