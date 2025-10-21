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

#ifndef _OB_VIRTUAL_CS_PROTOCOL_PROCESSOR_H_
#define _OB_VIRTUAL_CS_PROTOCOL_PROCESSOR_H_

#include "lib/ob_define.h"
#include "rpc/frame/ob_req_handler.h"
#include "rpc/frame/ob_req_deliver.h"
#include "rpc/obmysql/ob_i_cs_mem_pool.h"

namespace oceanbase
{
namespace rpc
{
class ObPacket;
}
namespace observer
{
struct ObSMConnection;
}
namespace obmysql
{
class ObMySQLHandler;
class ObVirtualCSProtocolProcessor
{
public:
  ObVirtualCSProtocolProcessor() {}
  virtual ~ObVirtualCSProtocolProcessor() {}

  int decode(easy_message_t *m, rpc::ObPacket *&pkt) { return easy_decode(m, pkt); }
  int process(easy_request_t *r, bool &need_read_more) { return easy_process(r, need_read_more); }

  virtual int easy_decode(easy_message_t *m, rpc::ObPacket *&pkt);
  virtual int easy_process(easy_request_t *r, bool &need_read_more);

  virtual int do_decode(observer::ObSMConnection& conn, ObICSMemPool& pool, const char*& start, const char* end, rpc::ObPacket*& pkt, int64_t& next_read_bytes) = 0;
  virtual int do_splice(observer::ObSMConnection& conn, ObICSMemPool& pool, void*& pkt, bool& need_decode_more) = 0;
};

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OB_VIRTUAL_CS_PROTOCOL_PROCESSOR_H_ */
