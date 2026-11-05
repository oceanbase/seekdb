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

#ifndef OCEANBASE_RPC_OB_REQ_OPERATOR_H_
#define OCEANBASE_RPC_OB_REQ_OPERATOR_H_

#include "rpc/ob_request.h"
namespace oceanbase
{
namespace rpc
{
// Local server address (formerly ObCallProxy::myaddr_, then ob_poc_call_server).
// Lives in the surviving rpc request-operator layer. Set once at server start
// (ObServer::init).
extern common::ObAddr g_rpc_self_addr;
// The POC/local-sync/local-async request operators that this used to dispatch
// to were part of the deleted obcall RPC transport.  Only MySQL requests reach
// the server now, so a single trivial operator survives (see the .cpp).
class ObReqOperator
{
public:
  ObReqOperator() {}
  ~ObReqOperator() {}
  void* alloc_response_buffer(ObRequest* req, int64_t size);
  void response_result(ObRequest* req);
  common::ObAddr get_peer(const ObRequest* req);
};

extern ObReqOperator global_rpc_req_operator;
#define RPC_REQ_OP (oceanbase::rpc::global_rpc_req_operator)
} // end of namespace rpc
} // end of namespace oceanbase

#endif /* OCEANBASE_RPC_OB_REQ_OPERATOR_H_ */
