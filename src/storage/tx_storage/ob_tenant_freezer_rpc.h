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

#include "rpc/frame/ob_result_code.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_server_struct.h"
#include "storage/tx_storage/ob_tenant_freezer_common.h"

#ifndef OCEABASE_STORAGE_TENANT_FREEZER_RPC_
#define OCEABASE_STORAGE_TENANT_FREEZER_RPC_

namespace oceanbase
{
namespace obcall
{
// Tenant-freezer RPC removed (single-replica): the ObTenantFreezerRpcProxy /
// ObTenantFreezerP / ObTenantFreezerRpcCb (proxy + processor + callback) are gone.
// Callers post the freeze via ex_rpc::async_call, which runs this dispatch in the
// target tenant's MTL context. See ob_tenant_freezer{,_rpc}.cpp.
int tenant_freeze_dispatch(const storage::ObTenantFreezeArg &arg);

} // obcall
} // oceanbase
#endif
