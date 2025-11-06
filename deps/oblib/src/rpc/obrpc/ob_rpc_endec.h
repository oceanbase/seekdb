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
