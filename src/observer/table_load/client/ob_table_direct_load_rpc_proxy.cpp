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

#include "ob_table_direct_load_rpc_proxy.h"
#include "ob_table_direct_load_rpc_executor.h"

namespace oceanbase
{
namespace observer
{
using namespace table;

int ObTableDirectLoadRpcProxy::dispatch(ObTableDirectLoadExecContext &ctx,
                                        const ObTableDirectLoadRequest &request,
                                        ObTableDirectLoadResult &result)
{
#define OB_TABLE_DIRECT_LOAD_RPC_DISPATCH(pcode)                                  \
  case pcode:                                                                     \
    OB_TABLE_LOAD_RPC_PROCESS(ObTableDirectLoadRpc, pcode, request, result, ctx); \
    break;

  int ret = OB_SUCCESS;
  switch (request.header_.operation_type_) {
    OB_TABLE_DIRECT_LOAD_RPC_DISPATCH(ObTableDirectLoadOperationType::BEGIN);
    OB_TABLE_DIRECT_LOAD_RPC_DISPATCH(ObTableDirectLoadOperationType::COMMIT);
    OB_TABLE_DIRECT_LOAD_RPC_DISPATCH(ObTableDirectLoadOperationType::ABORT);
    OB_TABLE_DIRECT_LOAD_RPC_DISPATCH(ObTableDirectLoadOperationType::GET_STATUS);
    OB_TABLE_DIRECT_LOAD_RPC_DISPATCH(ObTableDirectLoadOperationType::INSERT);
    OB_TABLE_DIRECT_LOAD_RPC_DISPATCH(ObTableDirectLoadOperationType::HEART_BEAT);
    default:
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "unexpected command type", K(ret), K(request));
      break;
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
