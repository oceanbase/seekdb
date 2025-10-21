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

#ifndef _OB_RPC_COMPRESS_PROTOCOL_PROCESSOR_H_
#define _OB_RPC_COMPRESS_PROTOCOL_PROCESSOR_H_


#include "io/easy_io_struct.h"
#include "rpc/obrpc/ob_rpc_compress_struct.h"
#include "rpc/obrpc/ob_virtual_rpc_protocol_processor.h"

namespace oceanbase
{
namespace common
{
class ObStreamCompressor;
};

namespace obrpc
{
class ObRpcCompressProtocolProcessor: public ObVirtualRpcProtocolProcessor
{
public:
  ObRpcCompressProtocolProcessor() {}
  virtual ~ObRpcCompressProtocolProcessor() {}

  virtual int encode(easy_request_t *req, ObRpcPacket *pkt);
  virtual int decode(easy_message_t *ms, ObRpcPacket *&pkt);
private:
  int reset_compress_ctx_mode(easy_connection_t *conn,
                              ObRpcCompressMode mode,
                              ObRpcPacket *&pkt,
                              ObCmdPacketInCompress::CmdType &cmd_type,
                              bool &is_still_need_compress);

  int reset_decompress_ctx_mode(easy_connection_t *easy_conn,
                                ObRpcCompressMode mode);

  int reset_decompress_ctx_ctx(easy_connection_t *easy_conn);
};

}//end of namespace obrpc
}//end of namespace oceanbase
#endif
