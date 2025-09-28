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

#ifndef OCEANBASE_OBRPC_OB_POC_RPC_PROXY_H_
#define OCEANBASE_OBRPC_OB_POC_RPC_PROXY_H_
#include "rpc/frame/ob_req_transport.h"
#include "rpc/ob_request.h"
#include "rpc/obrpc/ob_poc_rpc_server.h"

namespace oceanbase
{
namespace obrpc
{
typedef rpc::frame::ObReqTransport::AsyncCB UAsyncCB;
class Handle;
class ObRpcProxy;
class ObRpcResultCode;


template<typename UCB, typename Input>
    void set_ucb_args(UCB* ucb, const Input& args)
{
  ucb->set_args(args);
}

template<typename NoneType>
    void set_ucb_args(UAsyncCB* ucb, const NoneType& none)
{
  UNUSED(ucb);
  UNUSED(none);
}

class ObPocClientStub
{
public:
  ObPocClientStub() {}
  ~ObPocClientStub() {}
  static int64_t get_proxy_timeout(ObRpcProxy& proxy);
  static void set_rcode(ObRpcProxy& proxy, const ObRpcResultCode& rcode);
  static void set_handle(ObRpcProxy& proxy, Handle* handle, const ObRpcPacketCode& pcode, const ObRpcOpts& opts, bool is_stream_next, int64_t session_id, int64_t pkt_id, int64_t send_ts);

  static uint8_t balance_assign_tidx()
  {
    static uint8_t s_rpc_tidx CACHE_ALIGNED;
    return ATOMIC_FAA(&s_rpc_tidx, 1);
  }
  static int log_user_error_and_warn(const ObRpcResultCode &rcode);
};
}; // end namespace obrpc
}; // end namespace oceanbase
extern "C" {
int pn_terminate_pkt(uint64_t gtid, uint32_t pkt_id);
}
#endif /* OCEANBASE_OBRPC_OB_POC_RPC_PROXY_H_ */
