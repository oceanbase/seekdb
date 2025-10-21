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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_SHOW_RESTORE_PREVIEW_H_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_TENANT_SHOW_RESTORE_PREVIEW_H_

#include "share/ob_virtual_table_iterator.h"
#include "share/backup/ob_backup_struct.h"
#include "lib/container/ob_array.h"
#include "share/scn.h"

namespace oceanbase
{
namespace observer
{

class ObTenantShowRestorePreview : public common::ObVirtualTableIterator
{
  enum BackupType 
  {
    BACKUP_TYPE_SET = 0,
    BACKUP_TYPE_PIECE = 1,
    BACKUP_TYPE_MAX = 2,
  };

  enum RestorePreviewColumn 
  {
    BACKUP_TYPE  = common::OB_APP_MIN_COLUMN_ID,
    BACKUP_ID    = common::OB_APP_MIN_COLUMN_ID + 1,
    PREVIEW_PATH  = common::OB_APP_MIN_COLUMN_ID + 2,
    BACKUP_DESC = common::OB_APP_MIN_COLUMN_ID + 3,
  };

  static const int64_t MAX_INT64_STR_LENGTH = 100;
public:
  ObTenantShowRestorePreview();
  virtual ~ObTenantShowRestorePreview();
  int init();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int parse_restore_scn_from_session_(const ObString &backup_passwd, ObIArray<ObString> &tenant_path_array);
  int inner_get_next_row_();
  int get_backup_type_(BackupType &type);
  int get_backup_id_(int64_t &backup_id);
  int get_backup_path_(common::ObString &str);
  int get_backup_desc_(common::ObString &str);
private:
  bool is_inited_;
  int64_t idx_;
  int64_t total_cnt_;
  ObString uri_;
  share::SCN restore_scn_;
  bool only_contain_backup_set_;
  ObArray<share::ObRestoreBackupSetBriefInfo> backup_set_list_;
  ObArray<share::ObRestoreLogPieceBriefInfo> backup_piece_list_;
  ObArray<share::ObBackupPathString> log_path_list_;
  common::ObArenaAllocator allocator_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTenantShowRestorePreview);

};

} // end namespace observer
} // end namespace oceanbase

#endif
