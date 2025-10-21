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

#ifndef OCEANBASE_OBRPC_OB_RPC_OPTS_H_
#define OCEANBASE_OBRPC_OB_RPC_OPTS_H_
#include "rpc/obrpc/ob_rpc_packet.h"
#include "lib/net/ob_addr.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace obrpc
{
struct ObRpcOpts
{
  uint64_t tenant_id_;
  ObRpcPriority pr_;    // priority of this RPC packet
  mutable bool is_stream_; // is this RPC packet a stream packet?
  mutable bool is_stream_last_; // is this RPC packet the last packet in stream?
  common::ObAddr local_addr_;
  common::ObString ssl_invited_nodes_;

  ObRpcOpts()
      : tenant_id_(common::OB_INVALID_ID),
        pr_(ORPR_UNDEF),
        is_stream_(false),
        is_stream_last_(false),
        local_addr_(),
        ssl_invited_nodes_()
  {
  }
};

}; // end namespace obrpc
}; // end namespace oceanbase

#endif /* OCEANBASE_OBRPC_OB_RPC_OPTS_H_ */
