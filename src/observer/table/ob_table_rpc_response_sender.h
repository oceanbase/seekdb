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

#ifndef _OB_TABLE_RPC_RESPONSE_SENDER_H
#define _OB_TABLE_RPC_RESPONSE_SENDER_H 1
#include "rpc/ob_request.h"
#include "rpc/obrpc/ob_rpc_packet.h"
#include "rpc/frame/ob_req_processor.h"
#include "rpc/obmysql/ob_mysql_request_utils.h"
#include "rpc/obrpc/ob_rpc_result_code.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "ob_table_rpc_processor_util.h"
namespace oceanbase
{
namespace obrpc
{
// this class is copied from ObRpcProcessor
class ObTableRpcResponseSender
{
public:
  ObTableRpcResponseSender(rpc::ObRequest *req, table::ObITableResult *result, const int exec_ret_code = common::OB_SUCCESS)
      :req_(req),
       result_(result),
       exec_ret_code_(exec_ret_code),
       pcode_(ObRpcPacketCode::OB_INVALID_RPC_CODE),
       using_buffer_(NULL)
  {
    if (OB_NOT_NULL(req_)) {
      const ObRpcPacket *rpc_pkt = &reinterpret_cast<const ObRpcPacket&>(req_->get_packet());
      pcode_ = rpc_pkt->get_pcode();
    }
  }
  ObTableRpcResponseSender()
      : req_(nullptr),
        result_(nullptr),
        exec_ret_code_(common::OB_SUCCESS),
        pcode_(ObRpcPacketCode::OB_INVALID_RPC_CODE),
        using_buffer_(nullptr)
  {
  }
  virtual ~ObTableRpcResponseSender() = default;
  int response(const int cb_param);
  OB_INLINE void set_pcode(ObRpcPacketCode pcode) { pcode_ = pcode; }
  OB_INLINE void set_req(rpc::ObRequest *req)
  {
    req_ = req;
    if (OB_NOT_NULL(req_)) {
      const ObRpcPacket *rpc_pkt = &reinterpret_cast<const ObRpcPacket&>(req_->get_packet());
      pcode_ = rpc_pkt->get_pcode();
    }
  }
  OB_INLINE const rpc::ObRequest* get_req() const { return req_; }
  OB_INLINE void set_result(table::ObITableResult *result) { result_ = result; }
private:
  int serialize();
  int do_response(ObRpcPacket *response_pkt, bool require_rerouting);
  char *easy_alloc(int64_t size) const;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTableRpcResponseSender);
private:
  rpc::ObRequest *req_;
  table::ObITableResult *result_;
  const int exec_ret_code_; // return code of the processor execution
  ObRpcPacketCode pcode_;
  common::ObDataBuffer *using_buffer_;
};

} // end namespace obrpc
} // end namespace oceanbase

#endif /* _OB_TABLE_RPC_RESPONSE_SENDER_H */
