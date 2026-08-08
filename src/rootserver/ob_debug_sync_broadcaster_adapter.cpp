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

#define USING_LOG_PREFIX RS

#include "rootserver/ob_debug_sync_broadcaster_adapter.h"

#include "rootserver/ob_i_debug_sync_local_runtime.h"
#include "share/ob_debug_sync.h"
#include "share/ob_ex_rpc.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace rootserver
{

int ObDebugSyncBroadcasterAdapter::broadcast_debug_sync_action(
    const bool reset,
    const bool clear,
    const common::ObDebugSyncAction &action)
{
  obcall::ObDebugSyncActionArg arg;
  arg.reset_ = reset;
  arg.clear_ = clear;
  arg.action_ = action;
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid debug sync broadcast request", KR(ret), K(arg));
  } else if (OB_FAIL(ex_rpc::sync_call(
      [&] { return local_runtime_.set_ds_action(arg); }))) {
    LOG_WARN("broadcast debug sync action failed", KR(ret), K(arg));
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
