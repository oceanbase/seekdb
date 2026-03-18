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

#define USING_LOG_PREFIX STORAGE
#include "storage/high_availability/ob_restore_status.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace storage
{

ObRestoreStatus::ObRestoreStatus(const Status &status)
  : status_(status)
{
}

OB_SERIALIZE_MEMBER(ObRestoreStatus, status_);

int ObRestoreStatus::check_can_change_status(
    const ObRestoreStatus &cur_status,
    const ObRestoreStatus &change_status,
    bool &can_change)
{
  int ret = OB_SUCCESS;
  can_change = false;
  const Status cur = cur_status.get_status();
  switch (cur) {
    case Status::NONE: {
      if (change_status.is_restore_doing()) {
        can_change = true;
      }
      break;
    }
    case Status::RESTORE_DOING: {
      if (change_status.is_restore_wait()) {
        can_change = true;
      }
      break;
    }
    case Status::RESTORE_WAIT: {
      if (change_status.is_restore_failed() || change_status.is_none()) {
        can_change = true;
      }
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("invalid cur status for fail", K(ret), K(cur_status));
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
