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

#include "ob_table_load_resource_rpc_proxy.h"
#include "ob_table_load_resource_rpc_executor.h"

namespace oceanbase
{
namespace observer
{
using namespace common;

int ObTableLoadResourceRpcProxy::dispatch(const ObDirectLoadResourceOpRequest &request, 
                                          ObDirectLoadResourceOpResult &result,
                                          common::ObIAllocator &allocator)
{
#define OB_TABLE_LOAD_RESOURCE_RPC_DISPATCH(pcode)                                          \
  case pcode:                                                                               \
    OB_TABLE_LOAD_RPC_PROCESS(ObTableLoadResourceRpc, pcode, request, result, allocator);   \
    break;

  int ret = OB_SUCCESS;
  switch (request.command_type_) {
    OB_TABLE_LOAD_RESOURCE_RPC_DISPATCH(ObDirectLoadResourceCommandType::APPLY);
    OB_TABLE_LOAD_RESOURCE_RPC_DISPATCH(ObDirectLoadResourceCommandType::RELEASE);
    OB_TABLE_LOAD_RESOURCE_RPC_DISPATCH(ObDirectLoadResourceCommandType::UPDATE);
    OB_TABLE_LOAD_RESOURCE_RPC_DISPATCH(ObDirectLoadResourceCommandType::CHECK);
    default:
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "unexpected command type", K(ret), K(request));
      break;
  }

  return ret;
}

} // namespace observer
} // namespace oceanbase
