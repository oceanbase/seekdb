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

#ifndef OCEANBASE_STORAGE_STANDBY_RESTORE_STRUCT_
#define OCEANBASE_STORAGE_STANDBY_RESTORE_STRUCT_

#include "ob_standby_restore_tablet_status.h"

namespace oceanbase
{
namespace storage
{

struct ObTabletRestoreAction
{
  enum ACTION
  {
    RESTORE_ALL = 0,
    RESTORE_TABLET_META = 1,
    RESTORE_MINOR = 2,
    RESTORE_MAJOR = 3,
    RESTORE_NONE = 4,
    RESTORE_REMOTE_SSTABLE = 5,
    RESTORE_REPLACE_REMOTE_SSTABLE = 6,
    MAX,
  };
  static const char *get_action_str(const ACTION &action);
  static bool is_valid(const ACTION &action);
  static bool is_restore_minor(const ACTION &action);
  static bool is_restore_major(const ACTION &action);
  static bool is_restore_none(const ACTION &action);
  static bool is_restore_all(const ACTION &action);
  static bool is_restore_tablet_meta(const ACTION &action);
  static bool is_restore_remote_sstable(const ACTION &action);
  static bool is_restore_replace_remote_sstable(const ACTION &action);
  static int trans_restore_action_to_restore_status(
      const ACTION &action, ObTabletRestoreStatus::STATUS &status);
  static bool need_restore_mds_sstable(const ACTION &action);
  static bool need_restore_minor_sstable(const ACTION &action);
  static bool need_restore_ddl_sstable(const ACTION &action);
  static bool need_restore_major_sstable(const ACTION &action);
  static bool need_verify_table_store(const ACTION &action);
  static bool disallow_remote_table_exist(const ACTION &action);
  static bool is_restore_status_match(
      const ACTION &action, const ObTabletRestoreStatus::STATUS &status);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_STANDBY_RESTORE_STRUCT_
