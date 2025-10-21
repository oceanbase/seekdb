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

#ifndef OCEANBASE_RPC_OB_RPC_REQUEST_OPERATOR_H_
#define OCEANBASE_RPC_OB_RPC_REQUEST_OPERATOR_H_

#include "rpc/ob_request.h"
namespace oceanbase
{
namespace rpc
{
class ObIRpcRequestOperator
{
public:
  ObIRpcRequestOperator() {}
  virtual ~ObIRpcRequestOperator() {}
  virtual void* alloc_response_buffer(ObRequest* req, int64_t size) = 0;
  virtual void response_result(ObRequest* req, obrpc::ObRpcPacket* pkt) = 0;
  virtual common::ObAddr get_peer(const ObRequest* req) = 0;
};

class ObRpcRequestOperator: public ObIRpcRequestOperator
{
public:
  ObRpcRequestOperator() {}
  virtual ~ObRpcRequestOperator() {}
  virtual void* alloc_response_buffer(ObRequest* req, int64_t size) override {
    return get_operator(req).alloc_response_buffer(req, size);
  }
  virtual void response_result(ObRequest* req, obrpc::ObRpcPacket* pkt) override;
  virtual common::ObAddr get_peer(const ObRequest* req) override {
    return get_operator(req).get_peer(req);
  }
private:
  ObIRpcRequestOperator& get_operator(const ObRequest* req);
};

extern ObRpcRequestOperator global_rpc_req_operator;
#define RPC_REQ_OP (oceanbase::rpc::global_rpc_req_operator)
} // end of namespace rp
} // end of namespace oceanbase

#endif /* OCEANBASE_RPC_OB_RPC_REQUEST_OPERATOR_H_ */
