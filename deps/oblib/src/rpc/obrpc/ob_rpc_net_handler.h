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
  static int64_t CLUSTER_ID;
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
