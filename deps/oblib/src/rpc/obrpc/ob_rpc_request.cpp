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

#define USING_LOG_PREFIX RPC_OBRPC
#include "rpc/obrpc/ob_rpc_request.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace obrpc
{

int ObRpcRequest::do_flush_buffer(common::ObDataBuffer *buffer, const ObRpcPacket *pkt,
                                      int64_t sess_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buffer)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("input buffer is null", K(ret));
  } else if (OB_ISNULL(buffer->get_data())) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("input buffer is null", K(ret));
  } else if (OB_ISNULL(pkt_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("pkt_ pointer is null", K(ret));
  } else {
    //flush buffer to caller
    const obrpc::ObRpcPacket *rpc_pkt = pkt;
    obrpc::ObRpcPacketCode pcode = rpc_pkt->get_pcode();
    int64_t size = buffer->get_position() + sizeof (obrpc::ObRpcPacket);
    char *pbuf = NULL;
    char *ibuf = static_cast<char *>(RPC_REQ_OP.alloc_response_buffer(
        this, static_cast<uint32_t>(size)));
    if (OB_ISNULL(ibuf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc buffer from req->ms->pool failed", K(ret));
    } else {
      pbuf = ibuf + sizeof (obrpc::ObRpcPacket);
      MEMCPY(pbuf, buffer->get_data(), buffer->get_position());
    }

    obrpc::ObRpcPacket *res_packet = NULL;
    if (OB_SUCC(ret)) {
      res_packet = new(ibuf) obrpc::ObRpcPacket();
      res_packet->set_pcode(pcode);
      res_packet->set_chid(rpc_pkt->get_chid());
      res_packet->set_content(pbuf, buffer->get_position());
      res_packet->set_session_id(sess_id);
      res_packet->set_trace_id(common::ObCurTraceId::get());
      res_packet->set_resp();
#ifdef ERRSIM
      res_packet->set_module_type(THIS_WORKER.get_module_type());
#endif
      res_packet->calc_checksum();
    }
    RPC_REQ_OP.response_result(this, res_packet);
  }
  return ret;
}


} //end of namespace rpc
} //end of namespace oceanbase
