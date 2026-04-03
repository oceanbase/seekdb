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
#include "ob_log_fetch_log_rpc_stop_reason.h"

namespace oceanbase
{
namespace logfetcher
{

const char *print_rpc_stop_reason(const RpcStopReason reason)
{
  const char *reason_str = "INVALID";
  switch (reason) {
    case RpcStopReason::INVALID_REASON:
      reason_str = "INVALID";
      break;

    case RpcStopReason::REACH_MAX_LOG:
      reason_str = "REACH_MAX_LOG";
      break;

    case RpcStopReason::REACH_UPPER_LIMIT:
      reason_str = "REACH_UPPER_LIMIT";
      break;

    case RpcStopReason::FETCH_NO_LOG:
      reason_str = "FETCH_NO_LOG";
      break;

    case RpcStopReason::FETCH_LOG_FAIL:
      reason_str = "FETCH_LOG_FAIL";
      break;

    case RpcStopReason::REACH_MAX_RPC_RESULT:
      reason_str = "REACH_MAX_RPC_RESULT";
      break;

    case RpcStopReason::FORCE_STOP_RPC:
      reason_str = "FORCE_STOP_RPC";
      break;

    case RpcStopReason::RESULT_NOT_READABLE:
      reason_str = "RESULT_NOT_READABLE";
      break;

    case RpcStopReason::RPC_PROTO_NOT_MATCH:
      reason_str = "RPC_PROTO_NOT_MATCH";
      break;

    default:
      reason_str = "INVALID";
      break;
  }

  return reason_str;
}

}
}