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
#include "standby/restore/ob_storage_restore_struct.h"

namespace oceanbase
{
namespace storage
{

const char *ObTabletRestoreAction::get_action_str(const ACTION &action)
{
  const char *str = "UNKNOWN";
  const char *action_strs[] = {
      "RESTORE_ALL",
      "RESTORE_TABLET_META",
      "RESTORE_MINOR",
      "RESTORE_MAJOR",
      "RESTORE_NONE",
      "RESTORE_REMOTE_SSTABLE",
      "RESTORE_REPLACE_REMOTE_SSTABLE",
  };
  STATIC_ASSERT(MAX == ARRAYSIZEOF(action_strs), "action count mismatch");
  if (action < 0 || action >= MAX) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "invalid action", K(action));
  } else {
    str = action_strs[action];
  }
  return str;
}

bool ObTabletRestoreAction::is_valid(const ACTION &action)
{
  return action >= ACTION::RESTORE_ALL && action < ACTION::MAX;
}

bool ObTabletRestoreAction::is_restore_minor(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else {
    bool_ret = ACTION::RESTORE_MINOR == action;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_major(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else {
    bool_ret = ACTION::RESTORE_MAJOR == action;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_none(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else {
    bool_ret = ACTION::RESTORE_NONE == action;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_all(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else {
    bool_ret = ACTION::RESTORE_ALL == action;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_tablet_meta(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else {
    bool_ret = ACTION::RESTORE_TABLET_META == action;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_remote_sstable(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else {
    bool_ret = ACTION::RESTORE_REMOTE_SSTABLE == action;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_replace_remote_sstable(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else {
    bool_ret = ACTION::RESTORE_REPLACE_REMOTE_SSTABLE == action;
  }
  return bool_ret;
}

int ObTabletRestoreAction::trans_restore_action_to_restore_status(
    const ACTION &action, ObTabletRestoreStatus::STATUS &status)
{
  int ret = OB_SUCCESS;
  status = ObTabletRestoreStatus::RESTORE_STATUS_MAX;
  if (!is_valid(action)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("trans restore action to restore status get invalid argument", K(ret), K(action));
  } else if (is_restore_all(action) || is_restore_major(action) || is_restore_replace_remote_sstable(action)) {
    status = ObTabletRestoreStatus::FULL;
  } else if (is_restore_minor(action)) {
    status = ObTabletRestoreStatus::MINOR_AND_MAJOR_META;
  } else if (is_restore_tablet_meta(action)) {
    status = ObTabletRestoreStatus::EMPTY;
  } else if (is_restore_remote_sstable(action)) {
    status = ObTabletRestoreStatus::REMOTE;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("can not trans restore action to restore status", K(ret), K(action), K(status));
  }
  return ret;
}

bool ObTabletRestoreAction::is_restore_status_match(
    const ACTION &action,
    const ObTabletRestoreStatus::STATUS &status)
{
  bool b_ret = false;
  if (!is_valid(action) || !ObTabletRestoreStatus::is_valid(status)) {
    b_ret = false;
  } else if (is_restore_all(action) || is_restore_minor(action) || is_restore_remote_sstable(action)) {
    b_ret = status == ObTabletRestoreStatus::EMPTY;
  } else if (is_restore_major(action)) {
    b_ret = status == ObTabletRestoreStatus::MINOR_AND_MAJOR_META;
  } else if (is_restore_tablet_meta(action)) {
    b_ret = status == ObTabletRestoreStatus::PENDING;
  } else if (is_restore_replace_remote_sstable(action)) {
    b_ret = status == ObTabletRestoreStatus::REMOTE;
  }
  return b_ret;
}

bool ObTabletRestoreAction::need_restore_mds_sstable(const ACTION &action)
{
  return ACTION::RESTORE_MINOR == action
         || ACTION::RESTORE_ALL == action
         || ACTION::RESTORE_REMOTE_SSTABLE == action
         || ACTION::RESTORE_REPLACE_REMOTE_SSTABLE == action;
}

bool ObTabletRestoreAction::need_restore_minor_sstable(const ACTION &action)
{
  return ACTION::RESTORE_MINOR == action
         || ACTION::RESTORE_ALL == action
         || ACTION::RESTORE_REMOTE_SSTABLE == action
         || ACTION::RESTORE_REPLACE_REMOTE_SSTABLE == action;
}

bool ObTabletRestoreAction::need_restore_ddl_sstable(const ACTION &action)
{
  return ACTION::RESTORE_MINOR == action
         || ACTION::RESTORE_ALL == action
         || ACTION::RESTORE_REMOTE_SSTABLE == action
         || ACTION::RESTORE_REPLACE_REMOTE_SSTABLE == action;
}

bool ObTabletRestoreAction::need_restore_major_sstable(const ACTION &action)
{
  return ACTION::RESTORE_MAJOR == action
         || ACTION::RESTORE_ALL == action
         || ACTION::RESTORE_REMOTE_SSTABLE == action
         || ACTION::RESTORE_REPLACE_REMOTE_SSTABLE == action;
}

bool ObTabletRestoreAction::need_verify_table_store(const ACTION &action)
{
  return need_restore_major_sstable(action);
}

bool ObTabletRestoreAction::disallow_remote_table_exist(const ACTION &action)
{
  return ACTION::RESTORE_MAJOR == action
         || ACTION::RESTORE_ALL == action
         || ACTION::RESTORE_REPLACE_REMOTE_SSTABLE == action;
}

} // namespace storage
} // namespace oceanbase
