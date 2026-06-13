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
#include "ob_storage_restore_struct.h"
#include "storage/restore/ob_ls_restore_args.h"
namespace oceanbase
{
namespace storage
{
/******************ObRestoreBaseInfo*********************/
ObRestoreBaseInfo::ObRestoreBaseInfo()
  : job_id_(0),
    restore_scn_(),
    backup_cluster_version_(0),
    backup_data_version_(0),
    backup_compatible_(ObBackupSetFileDesc::MAX_COMPATIBLE_VERSION),
    backup_dest_(),
    backup_set_list_()
{
}

void ObRestoreBaseInfo::reset()
{
  job_id_ = 0;
  restore_scn_.reset();
  backup_cluster_version_ = 0;
  backup_data_version_ = 0;
  backup_compatible_ = ObBackupSetFileDesc::MAX_COMPATIBLE_VERSION;
  backup_dest_.reset();
  backup_set_list_.reset();
}

bool ObRestoreBaseInfo::is_valid() const
{
  return job_id_ > 0
      && restore_scn_.is_valid()
      && backup_cluster_version_ > 0
      && backup_data_version_ > 0
      && backup_dest_.is_valid()
      && !backup_set_list_.empty()
      && backup_compatible_ >= ObBackupSetFileDesc::COMPATIBLE_VERSION_1
      && backup_compatible_ < ObBackupSetFileDesc::MAX_COMPATIBLE_VERSION;
  //backup piece list can be empty
}

int ObRestoreBaseInfo::assign(const ObRestoreBaseInfo &restore_base_info)
{
  int ret = OB_SUCCESS;
  backup_dest_.reset();
  if (!restore_base_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("assign restore info get invalid argument", K(ret), K(restore_base_info));
  } else {
    job_id_ = restore_base_info.job_id_;
    restore_scn_ = restore_base_info.restore_scn_;
    backup_cluster_version_ = restore_base_info.backup_cluster_version_;
    backup_data_version_ = restore_base_info.backup_data_version_;
    backup_compatible_ = restore_base_info.backup_compatible_;
    if (OB_FAIL(backup_dest_.deep_copy(restore_base_info.backup_dest_))) {
      LOG_WARN("failed to set backup dest", K(ret), K(restore_base_info));
    } else if (OB_FAIL(backup_set_list_.assign(restore_base_info.backup_set_list_))) {
      LOG_WARN("failed to assign backup set list", K(ret), K(restore_base_info));
    }
  }
  return ret;
}

int ObRestoreBaseInfo::copy_from(const ObTenantRestoreCtx &restore_arg)
{
  int ret = OB_SUCCESS;
  if (!restore_arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("restore_arg get invalid argument", K(ret), K(restore_arg));
  } else {
    job_id_ = restore_arg.get_job_id();
    restore_scn_ = restore_arg.get_restore_scn();
    backup_cluster_version_ = restore_arg.get_backup_cluster_version();
    backup_data_version_ = restore_arg.get_backup_data_version();
    backup_dest_.reset();
    backup_set_list_.reset();
  }
  return ret;
}

int ObRestoreBaseInfo::get_restore_backup_set_dest(const int64_t backup_set_id,
    share::ObRestoreBackupSetBriefInfo &backup_set_dest) const
{
  int ret = OB_SUCCESS;
  bool find_target = false;
  backup_set_dest.reset();
  ARRAY_FOREACH_X(backup_set_list_, i, cnt, OB_SUCC(ret)) {
    if (backup_set_id == backup_set_list_.at(i).backup_set_desc_.backup_set_id_) {
      if (OB_FAIL(backup_set_dest.assign(backup_set_list_.at(i)))) {
        LOG_WARN("fail to assign", K(ret), K(backup_set_list_));
      } else {
        find_target = true;
        break;
      }
    }
  }
  if (OB_SUCC(ret) && !find_target) {
    ret = OB_OBJECT_NOT_EXIST;
    LOG_WARN("backup set not exist", K(ret), K(backup_set_id), K(backup_set_list_));
  }
  return ret;
}

int ObRestoreBaseInfo::get_restore_data_dest_id(
    common::ObISQLClient &proxy,
    const uint64_t tenant_id,
    int64_t &dest_id) const
{
  int ret = OB_NOT_SUPPORTED;
  UNUSED(proxy);
  UNUSED(tenant_id);
  dest_id = 0;
  LOG_WARN("get_restore_data_dest_id is not supported", K(ret));
  return ret;
}


int ObRestoreBaseInfo::get_last_backup_set_desc(share::ObBackupSetDesc &backup_set_desc) const
{
  int ret = OB_SUCCESS;
  if (backup_set_list_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("backup set list should not be empty", K(ret));
  } else {
    backup_set_desc = backup_set_list_.at(backup_set_list_.count() - 1).backup_set_desc_;
  }
  return ret;
}

/******************ObTabletRestoreAction*********************/

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
    bool_ret = false;
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else if (ACTION::RESTORE_MINOR != action) {
    bool_ret = false;
  } else {
    bool_ret = true;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_major(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    bool_ret = false;
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else if (ACTION::RESTORE_MAJOR != action) {
    bool_ret = false;
  } else {
    bool_ret = true;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_none(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    bool_ret = false;
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else if (ACTION::RESTORE_NONE != action) {
    bool_ret = false;
  } else {
    bool_ret = true;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_all(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    bool_ret = false;
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else if (ACTION::RESTORE_ALL != action) {
    bool_ret = false;
  } else {
    bool_ret = true;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_tablet_meta(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    bool_ret = false;
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else if (ACTION::RESTORE_TABLET_META != action) {
    bool_ret = false;
  } else {
    bool_ret = true;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_remote_sstable(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    bool_ret = false;
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else if (ACTION::RESTORE_REMOTE_SSTABLE != action) {
    bool_ret = false;
  } else {
    bool_ret = true;
  }
  return bool_ret;
}

bool ObTabletRestoreAction::is_restore_replace_remote_sstable(const ACTION &action)
{
  bool bool_ret = false;
  if (!is_valid(action)) {
    bool_ret = false;
    LOG_ERROR_RET(OB_ERR_UNEXPECTED, "restore action is unexpected", K(action));
  } else if (ACTION::RESTORE_REPLACE_REMOTE_SSTABLE != action) {
    bool_ret = false;
  } else {
    bool_ret = true;
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
  } else if (is_restore_all(action)) {
    status = ObTabletRestoreStatus::FULL;
  } else if (is_restore_minor(action)) {
    status = ObTabletRestoreStatus::MINOR_AND_MAJOR_META;
  } else if (is_restore_major(action)) {
    status = ObTabletRestoreStatus::FULL;
  } else if (is_restore_tablet_meta(action)) {
    status = ObTabletRestoreStatus::EMPTY;
  } else if (is_restore_remote_sstable(action)) {
    status = ObTabletRestoreStatus::REMOTE;
  } else if (is_restore_replace_remote_sstable(action)) {
    status = ObTabletRestoreStatus::FULL;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("can not trans restore action to restore status", K(ret), K(action), K(status));
  }
  return ret;
}

bool ObTabletRestoreAction::is_restore_status_match(
      const ACTION &action, const ObTabletRestoreStatus::STATUS &status)
{
  bool b_ret = false;
  if (!is_valid(action) || !ObTabletRestoreStatus::is_valid(status)) {
    b_ret = false;
  } else if (is_restore_all(action)) {
    b_ret = status == ObTabletRestoreStatus::EMPTY;
  } else if (is_restore_minor(action)) {
    b_ret = status == ObTabletRestoreStatus::EMPTY;
  } else if (is_restore_major(action)) {
    b_ret = status == ObTabletRestoreStatus::MINOR_AND_MAJOR_META;
  } else if (is_restore_tablet_meta(action)) {
    b_ret =  status == ObTabletRestoreStatus::PENDING;
  } else if (is_restore_remote_sstable(action)) {
    b_ret = status == ObTabletRestoreStatus::EMPTY;
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

/******************ObRestoreUtils*********************/

int ObRestoreUtils::get_backup_data_type(
    const ObITable::TableKey &table_key,
    share::ObBackupDataType &data_type)
{
  int ret = OB_SUCCESS;
  if (!table_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get backup data type get invalid argument", K(ret), K(table_key));
  } else if (table_key.get_tablet_id().is_ls_inner_tablet()) {
    data_type.set_sys_data_backup();
  } else if (table_key.is_minor_sstable()) {
    data_type.set_minor_data_backup();
  } else if (table_key.is_major_sstable()) {
    data_type.set_major_data_backup();
  } else if (table_key.is_ddl_dump_sstable()) {
    data_type.set_minor_data_backup();
  } else if (table_key.is_mds_sstable()) {
    data_type.set_minor_data_backup();
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid table key", K(ret), K(table_key));
  }
  return ret;
}

/******************ObTabletGroupRestoreArg*********************/
ObTabletGroupRestoreArg::ObTabletGroupRestoreArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    is_leader_(false),
    tablet_id_array_(),
    src_(),
    dst_(),
    restore_base_info_(),
    action_(ObTabletRestoreAction::MAX)
{
}

void ObTabletGroupRestoreArg::reset()
{
  tenant_id_ = OB_INVALID_ID;
  ls_id_.reset();
  is_leader_ = false;
  tablet_id_array_.reset();
  src_.reset();
  dst_.reset();
  restore_base_info_.reset();
  action_ = ObTabletRestoreAction::MAX;
}

bool ObTabletGroupRestoreArg::is_valid() const
{
  bool bool_ret = false;
  bool_ret = tenant_id_ != OB_INVALID_ID
      && ls_id_.is_valid()
      && !tablet_id_array_.empty()
      && ObTabletRestoreAction::is_valid(action_);
  if (bool_ret) {
    if (!is_leader_ && (!src_.is_valid() || !dst_.is_valid())) {
      bool_ret = false;
    } else {
      bool_ret = true;
    }
  }
  return bool_ret;
}

int ObTabletGroupRestoreArg::assign(const ObTabletGroupRestoreArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("assign restore tablet group arg get invalid argument", K(ret), K(arg));
  } else {
    tenant_id_ = arg.tenant_id_;
    ls_id_ = arg.ls_id_;
    is_leader_ = arg.is_leader_;
    src_ = arg.src_;
    dst_ = arg.dst_;
    action_ = arg.action_;
    if (OB_FAIL(tablet_id_array_.assign(arg.tablet_id_array_))) {
      LOG_WARN("failed to assign tablet id array", K(ret), K(arg));
    } else if (OB_FAIL(restore_base_info_.assign(arg.restore_base_info_))) {
      LOG_WARN("failed to assign restore base info", K(ret), K(arg));
    }
  }
  return ret;
}

/******************ObLSRestoreArg*********************/
ObLSRestoreArg::ObLSRestoreArg()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    is_leader_(false),
    src_(),
    dst_(),
    restore_base_info_()
{
}

void ObLSRestoreArg::reset()
{
  tenant_id_ = OB_INVALID_ID;
  ls_id_.reset();
  is_leader_ = false;
  src_.reset();
  dst_.reset();
  restore_base_info_.reset();
}

bool ObLSRestoreArg::is_valid() const
{
  bool bool_ret = false;
  bool_ret = tenant_id_ != OB_INVALID_ID
      && ls_id_.is_valid();
  if (bool_ret) {
    if (!is_leader_ && (!src_.is_valid() || !dst_.is_valid())) {
      bool_ret = false;
    } else {
      bool_ret = true;
    }
  }
  return bool_ret;
}

int ObLSRestoreArg::assign(const ObLSRestoreArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("assign restore tablet group arg get invalid argument", K(ret), K(arg));
  } else {
    tenant_id_ = arg.tenant_id_;
    ls_id_ = arg.ls_id_;
    is_leader_ = arg.is_leader_;
    src_ = arg.src_;
    dst_ = arg.dst_;
    if (OB_FAIL(restore_base_info_.assign(arg.restore_base_info_))) {
      LOG_WARN("failed to assign restore base info", K(ret), K(arg));
    }
  }
  return ret;
}

/******************ObIRestoreDagNetCtx*********************/
ObIRestoreDagNetCtx::ObIRestoreDagNetCtx()
  : task_id_(),
    src_(),
    start_ts_(0),
    finish_ts_(0)
{
}

ObIRestoreDagNetCtx::~ObIRestoreDagNetCtx()
{
}

}
}
