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

#ifndef OCEANBASE_OBRPC_OB_RPC_ENDEC_H_
#define OCEANBASE_OBRPC_OB_RPC_ENDEC_H_
#include "rpc/obrpc/ob_rpc_opts.h"
#include "rpc/obrpc/ob_rpc_mem_pool.h"
#include "rpc/obrpc/ob_rpc_packet.h"
#include "rpc/obrpc/ob_rpc_result_code.h"
#include "lib/compress/ob_compressor_pool.h"
#include "lib/ash/ob_active_session_guard.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

namespace oceanbase
{
namespace obrpc
{
extern int64_t get_max_rpc_packet_size();
class ObRpcProxy;
int64_t calc_extra_payload_size();
int fill_extra_payload(ObRpcPacket& pkt, char* buf, int64_t len, int64_t &pos);
int init_packet(ObRpcProxy& proxy, ObRpcPacket& pkt, ObRpcPacketCode pcode, const ObRpcOpts &opts,
                const bool unneed_response);
common::ObCompressorType get_proxy_compressor_type(ObRpcProxy& proxy);


}; // end namespace obrpc
}; // end namespace oceanbase

#endif /* OCEANBASE_OBRPC_OB_RPC_ENDEC_H_ */
