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

#ifndef OCEANBASE_OBRPC_OB_POC_RPC_REQUEST_OPERATOR_H_
#define OCEANBASE_OBRPC_OB_POC_RPC_REQUEST_OPERATOR_H_
#include "rpc/ob_rpc_request_operator.h"

namespace oceanbase
{
namespace obrpc
{
class ObPocRpcRequestOperator: public rpc::ObIRpcRequestOperator
{
public:
  ObPocRpcRequestOperator() {}
  virtual ~ObPocRpcRequestOperator() {}
  virtual void* alloc_response_buffer(rpc::ObRequest* req, int64_t size) override;
  virtual void response_result(rpc::ObRequest* req, obrpc::ObRpcPacket* pkt) override;
  virtual common::ObAddr get_peer(const rpc::ObRequest* req) override;
};

class ObLocalRpcRequestOperator: public rpc::ObIRpcRequestOperator
{
public:
  ObLocalRpcRequestOperator() {}
  virtual ~ObLocalRpcRequestOperator() {}
  virtual void* alloc_response_buffer(rpc::ObRequest* req, int64_t size) override;
  virtual void response_result(rpc::ObRequest* req, obrpc::ObRpcPacket* pkt) override;
  virtual common::ObAddr get_peer(const rpc::ObRequest* req) override;
};

}; // end namespace obrpc
}; // end namespace oceanbase

#endif /* OCEANBASE_OBRPC_OB_POC_RPC_REQUEST_OPERATOR_H_ */

