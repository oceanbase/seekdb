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

#include "ob_table_load_control_rpc_proxy.h"
#include "ob_table_load_control_rpc_executor.h"

namespace oceanbase
{
namespace observer
{
using namespace common;

int ObTableLoadControlRpcProxy::dispatch(const ObDirectLoadControlRequest &request,
                                         ObDirectLoadControlResult &result, ObIAllocator &allocator)
{
#define OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(pcode)                                        \
  case pcode:                                                                            \
    OB_TABLE_LOAD_RPC_PROCESS(ObTableLoadControlRpc, pcode, request, result, allocator); \
    break;

  int ret = OB_SUCCESS;
  switch (request.command_type_) {
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::PRE_BEGIN);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::CONFIRM_BEGIN);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::PRE_MERGE);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::START_MERGE);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::COMMIT);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::ABORT);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::GET_STATUS);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::HEART_BEAT);
    /// trans
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::PRE_START_TRANS);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::CONFIRM_START_TRANS);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::PRE_FINISH_TRANS);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::CONFIRM_FINISH_TRANS);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::ABANDON_TRANS);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::GET_TRANS_STATUS);
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::INSERT_TRANS);
    // init empty tablets
    OB_TABLE_LOAD_CONTROL_RPC_DISPATCH(ObDirectLoadControlCommandType::INIT_EMPTY_TABLETS);
    default:
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "unexpected command type", K(ret), K(request));
      break;
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
