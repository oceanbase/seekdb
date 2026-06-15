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

#ifndef OCEABASE_STORAGE_HA_RESTORE_STRUCT_
#define OCEABASE_STORAGE_HA_RESTORE_STRUCT_

#include "share/ob_ls_id.h"
#include "common/ob_tablet_id.h"
#include "lib/container/ob_array.h"
#include "common/ob_member.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"
#include "ob_storage_ha_struct.h"
#include "ob_tablet_ha_status.h"
#include "storage/blocksstable/ob_logic_macro_id.h"
#include "storage/blocksstable/index_block/ob_sstable_sec_meta_iterator.h"

namespace oceanbase
{
namespace storage
{

struct ObTenantRestoreCtx;
struct ObRestoreBaseInfo
{
  ObRestoreBaseInfo();
  virtual ~ObRestoreBaseInfo() = default;
  bool is_valid() const;
  void reset();
  int assign(const ObRestoreBaseInfo &restore_base_info);
  int copy_from(const ObTenantRestoreCtx &restore_arg);
  int get_restore_backup_set_dest(const int64_t backup_set_id, share::ObRestoreBackupSetBriefInfo &backup_set_dest) const;
  int get_last_backup_set_desc(share::ObBackupSetDesc &backup_set_desc) const;
  VIRTUAL_TO_STRING_KV(
      K_(job_id),
      K_(restore_scn),
      K_(backup_compatible),
      K_(backup_dest),
      K_(backup_set_list));

  int64_t job_id_;
  share::SCN restore_scn_;
  share::ObBackupSetFileDesc::Compatible backup_compatible_;
  share::ObBackupDest backup_dest_;
  common::ObArray<share::ObRestoreBackupSetBriefInfo> backup_set_list_;
};

struct ObTabletRestoreAction
{
  enum ACTION
  {
    RESTORE_ALL = 0,  // restore MINOR + MAJOR
    RESTORE_TABLET_META = 1,
    RESTORE_MINOR = 2,
    RESTORE_MAJOR = 3,
    RESTORE_NONE = 4,
    RESTORE_REMOTE_SSTABLE = 5, // restore remote sstable
    RESTORE_REPLACE_REMOTE_SSTABLE = 6, // replace remote sstable with local sstable
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

struct ObRestoreUtils
{
  static int get_backup_data_type(
      const ObITable::TableKey &table_key,
      share::ObBackupDataType &data_type);
};

struct ObTabletGroupRestoreArg
{
  ObTabletGroupRestoreArg();
  virtual ~ObTabletGroupRestoreArg() = default;
  void reset();
  bool is_valid() const;
  int assign(const ObTabletGroupRestoreArg &arg);

  VIRTUAL_TO_STRING_KV(
      K_(tenant_id),
      K_(ls_id),
      K_(is_leader),
      K_(tablet_id_array),
      K_(src),
      K_(dst),
      K_(restore_base_info),
      K_(action));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  bool is_leader_;
  ObArray<common::ObTabletID> tablet_id_array_;
  common::ObReplicaMember src_;
  common::ObReplicaMember dst_;
  ObRestoreBaseInfo restore_base_info_;
  ObTabletRestoreAction::ACTION action_;
};

struct ObLSRestoreArg
{
  ObLSRestoreArg();
  virtual ~ObLSRestoreArg() = default;
  void reset();
  bool is_valid() const;
  int assign(const ObLSRestoreArg &arg);

  VIRTUAL_TO_STRING_KV(
      K_(tenant_id),
      K_(ls_id),
      K_(is_leader),
      K_(src),
      K_(dst),
      K_(restore_base_info));
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  bool is_leader_;
  common::ObReplicaMember src_;
  common::ObReplicaMember dst_;
  ObRestoreBaseInfo restore_base_info_;
};

struct ObIRestoreDagNetCtx
{
public:
  ObIRestoreDagNetCtx();
  virtual ~ObIRestoreDagNetCtx();
  virtual int fill_comment(char *buf, const int64_t buf_len) const = 0;
  virtual int set_result(const int32_t result) = 0;
  virtual bool is_restore_failed() const = 0;
  virtual int check_need_retry(bool &need_retry) = 0;
  virtual int get_result(int32_t &result) = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;

public:
  static const int64_t MAX_RETRY_CNT = 3;
  share::ObTaskId task_id_;
  ObStorageHASrcInfo src_;
  int64_t start_ts_;
  int64_t finish_ts_;
  DISALLOW_COPY_AND_ASSIGN(ObIRestoreDagNetCtx);
};



}
}

#endif
