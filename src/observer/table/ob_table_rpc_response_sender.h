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
#include "rpc/frame/ob_req_processor.h"
#include "rpc/obmysql/ob_mysql_request_utils.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "ob_table_rpc_processor_util.h"
#include "share/table/ob_table_rpc_binding.h"  // rpc::frame::ObReqPacketCode (local stand-in)
namespace oceanbase
{
namespace obcall
{
using namespace oceanbase::rpc::frame; // ObReqPacketCode + OB_* pcode tags
// Table-API response sender. The obcall TRANSPORT (ObCallPacket / rpc::frame::ObResultCode /
// RPC_REQ_OP serialization) that this used to drive is gone together with the dead
// Table-API RPC dispatch. The class shape is preserved (it is held by value in
// ObTableMoveResponseSender / ObTableEndTransCb / redis service), but response() is a
// no-op returning OB_NOT_SUPPORTED. Nothing reaches it at runtime.
class ObTableRpcResponseSender
{
public:
  ObTableRpcResponseSender(rpc::ObRequest *req, table::ObITableResult *result, const int exec_ret_code = common::OB_SUCCESS)
      :req_(req),
       result_(result),
       exec_ret_code_(exec_ret_code),
       pcode_(ObReqPacketCode::OB_INVALID_RPC_CODE),
       using_buffer_(NULL)
  {
  }
  ObTableRpcResponseSender()
      : req_(nullptr),
        result_(nullptr),
        exec_ret_code_(common::OB_SUCCESS),
        pcode_(ObReqPacketCode::OB_INVALID_RPC_CODE),
        using_buffer_(nullptr)
  {
  }
  virtual ~ObTableRpcResponseSender() = default;
  int response(const int cb_param);
  OB_INLINE void set_pcode(ObReqPacketCode pcode) { pcode_ = pcode; }
  OB_INLINE void set_req(rpc::ObRequest *req) { req_ = req; }
  OB_INLINE const rpc::ObRequest* get_req() const { return req_; }
  OB_INLINE void set_result(table::ObITableResult *result) { result_ = result; }
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObTableRpcResponseSender);
private:
  rpc::ObRequest *req_;
  table::ObITableResult *result_;
  const int exec_ret_code_; // return code of the processor execution
  ObReqPacketCode pcode_;
  common::ObDataBuffer *using_buffer_;
};

} // end namespace obcall
} // end namespace oceanbase

#endif /* _OB_TABLE_RPC_RESPONSE_SENDER_H */
