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

#include "share/ob_debug_sync.h"
#include "share/ob_server_struct.h"
#include "rootserver/ob_local_management_service.h"
namespace oceanbase
{
namespace common
{
int ObDebugSync::add_debug_sync(const ObString &str, const bool is_global,
    ObDSSessionActions &session_actions)
{
  int ret = OB_SUCCESS;
  ObDebugSyncAction action;
  ObDSActionArray *local_actions = thread_local_actions();
  bool clear = false;
  bool reset = false;
  if (stop_) {
    ret = OB_CANCELED;
    LOG_WARN("is stopping", K(ret));
  } else if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(str));
  } else if (OB_ISNULL(local_actions)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("get thread local actions failed", K(ret), K(str));
  } else if (OB_FAIL(parse_action(str, action, clear, reset))) {
    LOG_WARN("parse debug sync action failed", K(ret), K(str));
  } else {
    if (!is_global) {
      if (clear) {
        local_actions->clear(action.sync_point_);
        session_actions.clear(action.sync_point_);
      } else if (reset) {
        local_actions->clear_all();
        session_actions.clear_all();
        event_control_.clear_event();
      } else {
        if (!action.is_valid()) {
          ret = OB_PARSE_DEBUG_SYNC_ERROR;
          LOG_WARN("invalid action", K(ret), K(str), K(action));
        } else if (OB_FAIL(local_actions->add_action(action))) {
          LOG_WARN("add action failed", K(ret), K(action));
        } else if (OB_FAIL(session_actions.add_action(action))) {
          LOG_WARN("add action failed", K(ret), K(action));
        }
      }
      if (OB_SUCC(ret)) {
        DEBUG_SYNC(NOW);
      }
    } else {
      obcall::ObDebugSyncActionArg arg;
      arg.reset_ = reset;
      arg.clear_ = clear;
      arg.action_ = action;
      if (OB_FAIL(GCTX.local_management_service_->apply_ds_action(arg))) {
        LOG_WARN("apply debug sync action failed", K(ret), K(arg));
      }
    }
  }

  return ret;
}
}  // namespace common
}  // namespace oceanbase
