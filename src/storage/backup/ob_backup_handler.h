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

#ifndef STORAGE_LOG_STREAM_BACKUP_HANDLER_H_
#define STORAGE_LOG_STREAM_BACKUP_HANDLER_H_

#include "common/ob_tablet_id.h"
#include "share/ob_ls_id.h"
#include "storage/backup/ob_backup_data_struct.h"

namespace oceanbase {
namespace backup {

class ObBackupHandler {
public:
  static int schedule_backup_meta_dag(const ObBackupJobDesc &job_desc, const share::ObBackupDest &backup_dest,
      const uint64_t tenant_id, const share::ObBackupSetDesc &backup_set_desc, const share::ObLSID &ls_id,
      const int64_t turn_id, const int64_t retry_id, const share::SCN &start_scn);
  static int schedule_backup_data_dag(const ObBackupJobDesc &job_desc, const share::ObBackupDest &backup_dest,
      const uint64_t tenant_id, const share::ObBackupSetDesc &backup_set_desc, const share::ObLSID &ls_id,
      const int64_t turn_id, const int64_t retry_id, const share::ObBackupDataType &backup_data_type);
  static int schedule_build_tenant_level_index_dag(const ObBackupJobDesc &job_desc,
      const share::ObBackupDest &backup_dest, const uint64_t tenant_id, const share::ObBackupSetDesc &backup_set_desc,
      const int64_t turn_id, const int64_t retry_id, const share::ObBackupDataType &backup_data_type);
  static int schedule_backup_complement_log_dag(const ObBackupJobDesc &job_desc, const share::ObBackupDest &backup_dest,
      const uint64_t tenant_id, const share::ObBackupSetDesc &backup_set_desc, const share::ObLSID &ls_id,
      const share::SCN &start_scn, const share::SCN &end_scn, const bool is_only_calc_stat);
  static int schedule_backup_fuse_tablet_meta_dag(const ObBackupJobDesc &job_desc, const share::ObBackupDest &backup_dest,
      const uint64_t tenant_id, const share::ObBackupSetDesc &backup_set_desc, const share::ObLSID &ls_id, const int64_t turn_id, const int64_t retry_id);
};

}  // namespace backup
}  // namespace oceanbase

#endif
