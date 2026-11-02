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

#ifdef _WIN32
#define USING_LOG_PREFIX RPC
#endif
#include "ob_req_operator.h"
#include "rpc/frame/ob_req_packet_code.h"

namespace oceanbase
{
namespace rpc
{
namespace frame
{
// Relocated from the deleted obcall transport (still LIVE via _rpc_checksum config).
ObReqCheckSumCheckLevel g_rpc_checksum_check_level = ObReqCheckSumCheckLevel::FORCE;
} // end namespace frame

// Definition of the local server address global (decl in the header).
common::ObAddr g_rpc_self_addr;

// The obcall RPC transport (POC client stub / in-process local-procedure-call)
// is gone.  Only MySQL (OB_MYSQL) requests reach this server now; their replies
// travel over the MySQL/SQL request operator, never through here.  The single
// surviving operator is therefore a no-op response path that merely reports the
// local address as the "peer".
void ObReqOperator::response_result(ObRequest* req)
{
}

void* ObReqOperator::alloc_response_buffer(ObRequest* /*req*/, int64_t /*size*/)
{
  return NULL;
}

ObAddr ObReqOperator::get_peer(const ObRequest* /*req*/)
{
  return g_rpc_self_addr;
}

ObReqOperator global_rpc_req_operator;
}; // end namespace rpc
}; // end namespace oceanbase
