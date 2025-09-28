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

#define USING_LOG_PREFIX RPC_OBRPC
#include "ob_rpc_endec.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_net_handler.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
namespace oceanbase
{
namespace obrpc
{
int64_t calc_extra_payload_size()
{
  int64_t payload = 0;
  if (!g_runtime_enabled) {
    payload += ObIRpcExtraPayload::instance().get_serialize_size();
  } else {
    ObRuntimeContext& ctx = get_ob_runtime_context();
    payload += ctx.get_serialize_size();
  }
  if (OBTRACE->is_inited()) {
    payload += OBTRACE->get_serialize_size();
  }
  return payload;
}

int fill_extra_payload(ObRpcPacket& pkt, char* buf, int64_t len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (!g_runtime_enabled) {
    if (OB_FAIL(common::serialization::encode(
                    buf, len, pos, ObIRpcExtraPayload::instance()))) {
      LOG_WARN("serialize debug sync actions fail", K(ret), K(pos), K(len));
    }
  } else {
    ObRuntimeContext& ctx = get_ob_runtime_context();
    if (OB_FAIL(common::serialization::encode(buf, len, pos, ctx))) {
      LOG_WARN("serialize context fail", K(ret), K(pos), K(len));
    } else {
      pkt.set_has_context();
      pkt.set_disable_debugsync();
    }
  }
  if (OBTRACE->is_inited() && OB_SUCC(ret)) {
    if (OB_FAIL(common::serialization::encode(buf, len, pos, *OBTRACE))) {
      LOG_WARN("serialize failed", K(ret), K(buf), K(pos));
    } else {
      pkt.set_has_trace_info();
    }
  }
  return ret;
}

int init_packet(ObRpcProxy& proxy, ObRpcPacket& pkt, ObRpcPacketCode pcode, const ObRpcOpts &opts,
                const bool unneed_response)
{
  int ret = proxy.init_pkt(&pkt, pcode, opts, unneed_response);
  if (common::OB_INVALID_CLUSTER_ID == pkt.get_dst_cluster_id()) {
    pkt.set_dst_cluster_id(ObRpcNetHandler::CLUSTER_ID);
  }
  return ret;
}

}; // end namespace obrpc
}; // end namespace oceanbase
