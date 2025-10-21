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

#ifndef OCEANBASE_OBRPC_OB_POC_RPC_SERVER_H_
#define OCEANBASE_OBRPC_OB_POC_RPC_SERVER_H_
#include "rpc/obrpc/ob_rpc_mem_pool.h"
#include "rpc/ob_request.h"
#include "rpc/frame/ob_req_deliver.h"
#include "rpc/obrpc/ob_listener.h"

namespace oceanbase
{
namespace obrpc
{

enum {
  INVALID_RPC_PKT_ID = -1
};
struct ObRpcReverseKeepaliveArg;

void stream_rpc_register(const int64_t pkt_id, int64_t send_time_us);
int stream_rpc_reverse_probe(const ObRpcReverseKeepaliveArg& reverse_keepalive_arg);
int64_t get_max_rpc_packet_size();
extern "C" {
  int dispatch_to_ob_listener(int accept_fd);
  int tranlate_to_ob_error(int err);
}
}; // end namespace obrpc
}; // end namespace oceanbase

#endif /* OCEANBASE_OBRPC_OB_POC_RPC_SERVER_H_ */
