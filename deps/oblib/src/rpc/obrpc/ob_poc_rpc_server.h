/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
