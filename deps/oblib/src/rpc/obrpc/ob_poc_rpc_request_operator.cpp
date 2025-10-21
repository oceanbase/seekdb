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

#include "rpc/obrpc/ob_poc_rpc_request_operator.h"
#include "rpc/obrpc/ob_poc_rpc_server.h"
#include "rpc/obmysql/ob_mysql_request_utils.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_local_procedure_call.h"

using namespace oceanbase::rpc;
using namespace oceanbase::oblpc;
namespace oceanbase
{
namespace obrpc
{
void* ObPocRpcRequestOperator::alloc_response_buffer(ObRequest* req, int64_t size)
{
  return NULL;
}

void ObPocRpcRequestOperator::response_result(ObRequest* req, obrpc::ObRpcPacket* pkt)
{
}

ObAddr ObPocRpcRequestOperator::get_peer(const ObRequest* req)
{
  return ObRpcProxy::myaddr_;
}

ObAddr ObLocalRpcRequestOperator::get_peer(const ObRequest* req)
{
  return ObRpcProxy::myaddr_;
}


void ObLocalRpcRequestOperator::response_result(ObRequest* req, obrpc::ObRpcPacket* pkt)
{
  request_finish_callback(); // same as old code
  const char* resp_ptr = NULL;
  int64_t resp_sz = 0;
  if (OB_LIKELY(pkt)) {
    resp_ptr = pkt->get_cdata();
    resp_sz = pkt->get_clen();
  }
  if (req->get_nio_protocol() == ObRequest::TRANSPORT_PROTO_LOCAL_SYNC) {
    // If it is a local sync rpc, wake up the condition variable
    ObSyncLocalProcedureCallContext *sync_ctx = (ObSyncLocalProcedureCallContext *)req->get_server_handle_context();
    sync_ctx->handle_resp(pkt, resp_ptr, resp_sz);
  } else if (req->get_nio_protocol() == ObRequest::TRANSPORT_PROTO_LOCAL_ASYNC) {
    // If it is a async rpc, execute the callback and destroy the context
    ObAsyncLocalProcedureCallContext *async_ctx = (ObAsyncLocalProcedureCallContext *)req->get_server_handle_context();
    IGNORE_RETURN async_ctx->handle_resp(pkt, resp_ptr, resp_sz);
    async_ctx->destroy();
  } else {
    RPC_OBRPC_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "invalid rpc type, should not be here", K(req->get_nio_protocol()));
  }
}

void* ObLocalRpcRequestOperator::alloc_response_buffer(ObRequest* req, int64_t size)
{
  void* ptr = NULL;
  ObLocalProcedureCallContext *ctx = reinterpret_cast<ObLocalProcedureCallContext *>(req->get_server_handle_context());
  ptr = ctx->alloc(size);
  return ptr;
}

}; // end namespace obrpc
}; // end namespace oceanbase

