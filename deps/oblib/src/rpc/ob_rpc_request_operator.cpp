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

#include "ob_rpc_request_operator.h"
#include "rpc/obrpc/ob_easy_rpc_request_operator.h"
#include "rpc/obrpc/ob_poc_rpc_request_operator.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/stat/ob_diagnostic_info_container.h"

namespace oceanbase
{
using namespace obrpc;
namespace rpc
{
ObPocRpcRequestOperator global_poc_req_operator;
ObLocalRpcRequestOperator global_local_req_operator;
ObIRpcRequestOperator& ObRpcRequestOperator::get_operator(const ObRequest* req)
{
  ObIRpcRequestOperator* op = NULL;
  switch(req->get_nio_protocol()) {
    case ObRequest::TRANSPORT_PROTO_POC:
      op = &global_poc_req_operator;
      break;
    case ObRequest::TRANSPORT_PROTO_LOCAL_SYNC:
    case ObRequest::TRANSPORT_PROTO_LOCAL_ASYNC:
      op = &global_local_req_operator;
      break;
    default:
      op = &global_poc_req_operator;
  }
  return *op;
}

void ObRpcRequestOperator::response_result(ObRequest* req, obrpc::ObRpcPacket* pkt) {
  common::ObDiagnosticInfo *di = req->get_diagnostic_info();
  if (OB_NOT_NULL(di)) {
    req->reset_diagnostic_info();
  }
  return get_operator(req).response_result(req, pkt);
}

ObRpcRequestOperator global_rpc_req_operator;
}; // end namespace rpc
}; // end namespace oceanbase

