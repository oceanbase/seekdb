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
#include "ob_restore_compatibility_util.h"
#include "storage/high_availability/ob_storage_ha_utils.h"

using namespace oceanbase;
using namespace share;
using namespace storage;

ObRestoreCompatibilityUtil::ObRestoreCompatibilityUtil()
{
}

int ObRestoreCompatibilityUtil::is_tablet_restore_phase_done(
    const ObLSID &ls_id,
    const ObLSRestoreStatus &ls_restore_status,
    const ObTabletHandle &tablet_handle,
    bool &is_finish) const
{
  int ret = OB_SUCCESS;
  const ObTabletMeta &tablet_meta = tablet_handle.get_obj()->get_tablet_meta();
  const ObTabletHAStatus &ha_status = tablet_meta.ha_status_;

  switch (ls_restore_status.get_status()) {
    case ObLSRestoreStatus::RESTORE_TABLETS_META :
    case ObLSRestoreStatus::WAIT_RESTORE_TABLETS_META : {
      is_finish = !ha_status.is_restore_status_pending();
      break;
    }

    case ObLSRestoreStatus::RESTORE_TO_CONSISTENT_SCN :
    case ObLSRestoreStatus::WAIT_RESTORE_TO_CONSISTENT_SCN : {
      is_finish = !(ha_status.is_restore_status_full() && tablet_meta.has_transfer_table());
      break;
    }

    case ObLSRestoreStatus::QUICK_RESTORE:
    case ObLSRestoreStatus::WAIT_QUICK_RESTORE:
    case ObLSRestoreStatus::QUICK_RESTORE_FINISH: {
      if (ls_id.is_sys_ls()) {
        is_finish = ha_status.is_restore_status_full();
      } else if (ha_status.is_restore_status_undefined()) {
        bool is_deleted = true;
        if (ls_restore_status.is_quick_restore()) {
          is_finish = true;
        } else if (OB_FAIL(ObStorageHAUtils::check_tablet_is_deleted(tablet_handle, is_deleted))) {
          LOG_WARN("failed to check tablet is deleted", K(ret), K(tablet_meta));
        } else if (is_deleted) {
          is_finish = true;
          LOG_INFO("UNDEFINED tablet is deleted", K(tablet_meta));
        } else {
          is_finish = false;
          LOG_INFO("UNDEFINED tablet is not deleted", K(tablet_meta));
        }
      } else {
        is_finish = ha_status.is_restore_status_remote();
        if (!ha_status.is_restore_status_full()) {
        } else if (!tablet_meta.has_transfer_table()) {
          is_finish = true;
        } else {
          is_finish = false;
        }
      }
      break;
    }

    case ObLSRestoreStatus::RESTORE_MAJOR_DATA : {
      is_finish = !ha_status.is_restore_status_remote();
      break;
    }

    case ObLSRestoreStatus::WAIT_RESTORE_MAJOR_DATA : {
      if (ha_status.is_restore_status_full()) {
        is_finish = true;
      } else if (ha_status.is_restore_status_undefined()) {
        bool is_deleted = true;
        if (OB_FAIL(ObStorageHAUtils::check_tablet_is_deleted(tablet_handle, is_deleted))) {
          LOG_WARN("failed to check tablet is deleted", K(ret), K(tablet_meta));
        } else {
          is_finish = is_deleted;
        }
      } else {
        is_finish = false;
      }
      break;
    }

    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to check tablet is deleted", K(ret), K(ls_id), K(ls_restore_status), K(tablet_meta));
      break;
    }
  }

  return ret;
}

ObTabletRestoreAction::ACTION ObRestoreCompatibilityUtil::get_restore_action(
    const ObLSID &ls_id,
    const ObLSRestoreStatus &ls_restore_status) const
{
  ObTabletRestoreAction::ACTION action = ObTabletRestoreAction::RESTORE_NONE;
  switch (ls_restore_status.get_status()) {
    case ObLSRestoreStatus::RESTORE_TABLETS_META : {
      action = ObTabletRestoreAction::RESTORE_TABLET_META;
      break;
    }

    case ObLSRestoreStatus::QUICK_RESTORE: {
      if (ls_id.is_sys_ls()) {
        action = ObTabletRestoreAction::RESTORE_ALL;
      } else {
        action = ObTabletRestoreAction::RESTORE_REMOTE_SSTABLE;
      }
      break;
    }

    case ObLSRestoreStatus::RESTORE_MAJOR_DATA : {
      if (ls_id.is_user_ls()) {
        action = ObTabletRestoreAction::RESTORE_REPLACE_REMOTE_SSTABLE;
      }
      break;
    }

    default: {
      action = ObTabletRestoreAction::RESTORE_NONE;
      break;
    }
  }

  return action;
}
