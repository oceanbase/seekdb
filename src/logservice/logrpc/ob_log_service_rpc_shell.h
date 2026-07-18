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

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_SERVICE_RPC_SHELL_
#define OCEANBASE_LOGSERVICE_OB_LOG_SERVICE_RPC_SHELL_

#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace rpc { namespace frame { class ObReqTransport; } }
namespace obcall
{

// Inert shell: single-replica seekdb routes all logservice log RPCs in-process
// via logservice::LogRequestHandler. This type no longer derives from any RPC
// framework; it is kept only as a (dead) member/pointer type held by ObLogService
// and ObLogHandler. The remaining real RPC methods (acquire_log_rebuild_info /
// sync_base_lsn) are shared-storage only and routed solely under
class ObLogServiceRpcProxy
{
public:
  int init(const common::ObAddr & = common::ObAddr())
  { return common::OB_SUCCESS; }
  void destroy() {}
};

} // end namespace obcall
} // end namespace oceanbase

#endif
