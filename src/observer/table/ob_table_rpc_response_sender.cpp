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

#define USING_LOG_PREFIX SERVER
#include "ob_table_rpc_response_sender.h"

using namespace oceanbase::observer;
using namespace oceanbase::common;
using namespace oceanbase::table;
using namespace oceanbase::share;
using namespace oceanbase::obcall;

// Table-API obcall RPC response path decommissioned.
// The original implementation serialized *result_ + rpc::frame::ObResultCode into an
// ObCallPacket and shipped it back over the obcall transport (RPC_REQ_OP).
// Both the transport and the dispatch that would invoke this are gone, so this
// is now a no-op. Kept only so the by-value member compiles. Unreachable.
int ObTableRpcResponseSender::response(const int cb_param)
{
  UNUSED(cb_param);
  return OB_NOT_SUPPORTED;
}
