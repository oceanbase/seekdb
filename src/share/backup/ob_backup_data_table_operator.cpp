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

#define USING_LOG_PREFIX SHARE
#include "ob_backup_data_table_operator.h"
#include "src/share/inner_table/ob_inner_table_schema_constants.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"

namespace oceanbase
{ 
using namespace common;
using namespace common::sqlclient;
namespace share
{

uint64_t ObBackupBaseTableOperator::get_exec_tenant_id(uint64_t tenant_id) 
{
  return gen_meta_tenant_id(tenant_id);
}

/*
 -----------------------------__all_backup_set_files--------------------------------
 */

int ObBackupSetFileOperator::insert_backup_set_file(common::ObISQLClient &proxy, const ObBackupSetFileDesc &backup_set_desc)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSetFileOperator::fill_dml_with_backup_set_(const ObBackupSetFileDesc &backup_set_desc, ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  char tenant_version_display[OB_INNER_TABLE_BACKUP_TASK_CLUSTER_FORMAT_LENGTH] = "";
  char cluster_version_display[OB_INNER_TABLE_BACKUP_TASK_CLUSTER_FORMAT_LENGTH] = "";
  const int64_t pos =  ObClusterVersion::get_instance().print_version_str(
    tenant_version_display, OB_INNER_TABLE_BACKUP_TASK_CLUSTER_FORMAT_LENGTH, backup_set_desc.tenant_compatible_);
  const int64_t pos1 =  ObClusterVersion::get_instance().print_version_str(
    cluster_version_display, OB_INNER_TABLE_BACKUP_TASK_CLUSTER_FORMAT_LENGTH, backup_set_desc.cluster_version_);
  const char *comment =  OB_SUCCESS == backup_set_desc.result_ ? "" : common::ob_strerror(backup_set_desc.result_);
  char min_restore_scn_display[OB_INNER_TABLE_BACKUP_TASK_CLUSTER_FORMAT_LENGTH] = "";
  if (backup_set_desc.min_restore_scn_.is_valid_and_not_min()) {
    if (OB_FAIL(ObBackupUtils::backup_scn_to_str(backup_set_desc.tenant_id_, 
                                                 backup_set_desc.min_restore_scn_, 
                                                 min_restore_scn_display, 
                                                 OB_INNER_TABLE_BACKUP_TASK_CLUSTER_FORMAT_LENGTH))) {
      LOG_WARN("failed to backup scn to str", K(ret), K(backup_set_desc));
    }
  }

  if (FAILEDx(dml.add_pk_column(OB_STR_BACKUP_SET_ID, backup_set_desc.backup_set_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_TENANT_ID, backup_set_desc.tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_INCARNATION, backup_set_desc.incarnation_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_DEST_ID, backup_set_desc.dest_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_TYPE, backup_set_desc.backup_type_.get_backup_type_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_PREV_FULL_BACKUP_SET_ID, backup_set_desc.prev_full_backup_set_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_PREV_INC_BACKUP_SET_ID, backup_set_desc.prev_inc_backup_set_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_START_TS, backup_set_desc.start_time_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_END_TS, backup_set_desc.end_time_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_STATUS, backup_set_desc.get_backup_set_status_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_FILE_STATUS, ObBackupFileStatus::get_str(backup_set_desc.file_status_)))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_PLUS_ARCHIVELOG, backup_set_desc.get_plus_archivelog_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_RESULT, backup_set_desc.result_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_ENCRYPTION_MODE, ObBackupEncryptionMode::to_str(backup_set_desc.encryption_mode_)))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_PASSWD, backup_set_desc.passwd_.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_PATH, backup_set_desc.backup_path_.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_TENANT_COMPATIBLE, tenant_version_display))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_CLUSTER_VERSION, cluster_version_display))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_COMPATIBLE, backup_set_desc.backup_compatible_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_uint64_column(OB_STR_START_REPLAY_SCN, backup_set_desc.start_replay_scn_.get_val_for_inner_table_field()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_COMMENT, comment))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_uint64_column(OB_STR_MIN_RESTORE_SCN, backup_set_desc.min_restore_scn_.get_val_for_inner_table_field()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_INPUT_BYTES, backup_set_desc.stats_.input_bytes_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_OUTPUT_BYTES, backup_set_desc.stats_.output_bytes_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_TABLET_COUNT, backup_set_desc.stats_.tablet_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_FINISH_TABLET_COUNT, backup_set_desc.stats_.finish_tablet_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_MACRO_BLOCK_COUNT, backup_set_desc.stats_.macro_block_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_FINISH_MACRO_BLOCK_COUNT, backup_set_desc.stats_.finish_macro_block_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_EXTRA_BYTES, backup_set_desc.stats_.extra_bytes_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_FILE_COUNT, backup_set_desc.stats_.finish_file_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_DATA_TURN_ID, backup_set_desc.data_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_META_TURN_ID, backup_set_desc.meta_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_MINOR_TURN_ID, backup_set_desc.minor_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_MAJOR_TURN_ID, backup_set_desc.major_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_CONSISTENT_SCN, backup_set_desc.consistent_scn_.get_val_for_inner_table_field()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_MIN_RESTORE_SCN_DISPLAY, min_restore_scn_display))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  }
  return ret;
}

int ObBackupSetFileOperator::get_backup_set_files(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id,
    ObIArray<ObBackupSetFileDesc> &tenant_backup_set_infos)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSetFileOperator::get_backup_set_files_specified_dest(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id,
    const int64_t dest_id,
    ObIArray<ObBackupSetFileDesc> &backup_set_infos)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSetFileOperator::parse_backup_set_info_result_(
    sqlclient::ObMySQLResult &result, 
    ObIArray<ObBackupSetFileDesc> &backup_set_infos)
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObBackupSetFileDesc backup_set_desc;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("[DATA_BACKUP]failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_backup_set_(result, backup_set_desc))) {
      LOG_WARN("[DATA_BACKUP]failed to parse backup set result", K(ret));
    } else if (OB_FAIL(backup_set_infos.push_back(backup_set_desc))) {
      LOG_WARN("[DATA_BACKUP]failed to push back job", K(ret), K(backup_set_desc));
    }
  }
  return ret;
}

int ObBackupSetFileOperator::get_backup_set_file(
    common::ObISQLClient &proxy, 
    bool need_lock,
    const int64_t backup_set_id_, 
    const int64_t incarnation, 
    const uint64_t tenant_id, 
    const int64_t dest_id, 
    ObBackupSetFileDesc &backup_set_desc)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSetFileOperator::get_one_backup_set_file(
    common::ObISQLClient &proxy, 
    bool need_lock,
    const int64_t backup_set_id_, 
    const int64_t incarnation, 
    const uint64_t tenant_id, 
    ObBackupSetFileDesc &backup_set_desc)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSetFileOperator::do_parse_backup_set_(ObMySQLResult &result, ObBackupSetFileDesc &backup_set_desc)
{
  int ret = OB_SUCCESS;
  int64_t real_length = 0;
  uint64_t min_restore_scn = 0;
  uint64_t start_replay_scn = 0;
  uint64_t consistent_scn = 0;
  int64_t backup_compatible = 0;
  char backup_path[OB_MAX_BACKUP_DEST_LENGTH] = "";
  char encryption_mode_str[OB_DEFAULT_STATUS_LENTH] = "";
  char passwd[OB_MAX_PASSWORD_LENGTH] = "";
  char backup_type_str[OB_SYS_TASK_TYPE_LENGTH] = "";
  char tenant_version_str[OB_CLUSTER_VERSION_LENGTH] = "";
  char cluster_version_str[OB_CLUSTER_VERSION_LENGTH] = "";
  char file_status_str[OB_DEFAULT_STATUS_LENTH] = "";
  char status_str[OB_DEFAULT_STATUS_LENTH] = "";
  char plus_archivelog_str[OB_DEFAULT_STATUS_LENTH] = "";
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_BACKUP_SET_ID, backup_set_desc.backup_set_id_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INCARNATION, backup_set_desc.incarnation_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TENANT_ID, backup_set_desc.tenant_id_, uint64_t);  
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_DEST_ID, backup_set_desc.dest_id_, int64_t);  
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INPUT_BYTES, backup_set_desc.stats_.input_bytes_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_OUTPUT_BYTES, backup_set_desc.stats_.output_bytes_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TABLET_COUNT, backup_set_desc.stats_.tablet_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FINISH_TABLET_COUNT, backup_set_desc.stats_.finish_tablet_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_MACRO_BLOCK_COUNT, backup_set_desc.stats_.macro_block_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FINISH_MACRO_BLOCK_COUNT, backup_set_desc.stats_.finish_macro_block_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FILE_COUNT, backup_set_desc.stats_.finish_file_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_EXTRA_BYTES, backup_set_desc.stats_.extra_bytes_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_BACKUP_COMPATIBLE, backup_compatible, int64_t);

  EXTRACT_UINT_FIELD_MYSQL(result, OB_STR_MIN_RESTORE_SCN, min_restore_scn, uint64_t);  
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_TYPE, backup_type_str, OB_SYS_TASK_TYPE_LENGTH, real_length);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_PREV_FULL_BACKUP_SET_ID, backup_set_desc.prev_full_backup_set_id_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_PREV_INC_BACKUP_SET_ID, backup_set_desc.prev_inc_backup_set_id_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_START_TS, backup_set_desc.start_time_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_END_TS, backup_set_desc.end_time_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_RESULT, backup_set_desc.result_, int);
  EXTRACT_UINT_FIELD_MYSQL(result, OB_STR_START_REPLAY_SCN, start_replay_scn, uint64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_DATA_TURN_ID, backup_set_desc.data_turn_id_, int64_t); 
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_META_TURN_ID, backup_set_desc.meta_turn_id_, int64_t); 

  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_ENCRYPTION_MODE, encryption_mode_str, OB_DEFAULT_STATUS_LENTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_PASSWD, passwd, OB_MAX_PASSWORD_LENGTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_PATH, backup_path, OB_MAX_BACKUP_DEST_LENGTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_TENANT_COMPATIBLE, tenant_version_str, OB_CLUSTER_VERSION_LENGTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_CLUSTER_VERSION, cluster_version_str, OB_CLUSTER_VERSION_LENGTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_FILE_STATUS, file_status_str, OB_DEFAULT_STATUS_LENTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_STATUS, status_str, OB_DEFAULT_STATUS_LENTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_PLUS_ARCHIVELOG, plus_archivelog_str, OB_DEFAULT_STATUS_LENTH, real_length);
  EXTRACT_UINT_FIELD_MYSQL(result, OB_STR_CONSISTENT_SCN, consistent_scn, uint64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_MINOR_TURN_ID, backup_set_desc.minor_turn_id_, int64_t); 
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_MAJOR_TURN_ID, backup_set_desc.major_turn_id_, int64_t); 

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(backup_set_desc.backup_path_.assign(backup_path))) {
    LOG_WARN("[DATA_BACKUP]failed to set backup path", K(ret), K(backup_path));
  } else if (OB_FAIL(backup_set_desc.passwd_.assign(passwd))) {
    LOG_WARN("[DATA_BACKUP]failed to set passwd", K(ret), K(passwd));
  } else if (OB_FAIL(backup_set_desc.backup_type_.set_backup_type(backup_type_str))) {
    LOG_WARN("[DATA_BACKUP]failed to set backup_type", K(ret), K(backup_type_str));
  } else if (OB_FAIL(ObClusterVersion::get_version(tenant_version_str, backup_set_desc.tenant_compatible_))) {
    LOG_WARN("[DATA_BACKUP]failed to parse tenant version", K(ret), K(tenant_version_str));
  } else if (OB_FAIL(ObClusterVersion::get_version(cluster_version_str, backup_set_desc.cluster_version_))) {
    LOG_WARN("[DATA_BACKUP]failed to parse cluster version", K(ret), K(cluster_version_str));
  } else if (OB_FAIL(backup_set_desc.set_backup_set_status(status_str))) {
    LOG_WARN("[DATA_BACKUP]failed to parse status", K(ret), K(status_str)); 
  } else if (OB_FAIL(backup_set_desc.set_plus_archivelog(plus_archivelog_str))) {
    LOG_WARN("[DATA_BACKUP]failed to parse plus_archivelog", K(ret), K(plus_archivelog_str)); 
  } else if (OB_FAIL(ObBackupUtils::convert_timestamp_to_date(backup_set_desc.start_time_, backup_set_desc.date_))) {
    LOG_WARN("[DATA_BACKUP]failed to parse date", K(ret));
  } else if (OB_FAIL(backup_set_desc.min_restore_scn_.convert_for_inner_table_field(min_restore_scn))) {
    LOG_WARN("fail to set min restore scn", K(ret), K(min_restore_scn));
  } else if (OB_FAIL(backup_set_desc.start_replay_scn_.convert_for_inner_table_field(start_replay_scn))) {
    LOG_WARN("fail to set start_replay_scn", K(ret), K(start_replay_scn));
  } else if (OB_FAIL(backup_set_desc.consistent_scn_.convert_for_inner_table_field(consistent_scn))) {
    LOG_WARN("fail to set tablet snapshot scn", K(ret), K(consistent_scn));
  } else { 
    backup_set_desc.encryption_mode_ = ObBackupEncryptionMode::parse_str(encryption_mode_str);
    backup_set_desc.file_status_ = ObBackupFileStatus::get_status(file_status_str);
    backup_set_desc.backup_compatible_ = static_cast<ObBackupSetFileDesc::Compatible>(backup_compatible);
    if (!ObBackupSetFileDesc::is_backup_compatible_valid(backup_set_desc.backup_compatible_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid backup compatible", K(ret), K(backup_set_desc.backup_compatible_));
    }
  }
  return ret;
}

int ObBackupSetFileOperator::update_backup_set_file(
    common::ObISQLClient &proxy, const ObBackupSetFileDesc &backup_set_desc)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSetFileOperator::get_prev_backup_set_id(
    common::ObISQLClient &proxy, 
    const uint64_t &tenant_id, 
    const int64_t &backup_set_id, 
    const ObBackupType &backup_type, 
    const ObBackupPathString &backup_path,
    int64_t &prev_full_backup_set_id, 
    int64_t &prev_inc_backup_set_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSetFileOperator::get_candidate_obsolete_backup_sets(
    common::ObISQLClient &proxy,
    const uint64_t tenant_id,
    const int64_t expired_time,
    const char *backup_path_str,
    common::ObIArray<ObBackupSetFileDesc> &backup_set_descs)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

// TODO(yangyi.yyy): consider if need fetch this later in 4.1
int ObBackupSetFileOperator::get_all_backup_set_between(
      common::ObISQLClient &proxy,
      const uint64_t tenant_id,
      const int64_t incarnation, 
      const int64_t min_backup_set_id,
      const int64_t max_backup_set_id,
      common::ObIArray<ObBackupSetFileDesc> &backup_set_descs)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSetFileOperator::parse_backup_sets_(
    sqlclient::ObMySQLResult &result, 
    common::ObIArray<ObBackupSetFileDesc> &backup_set_descs)
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObBackupSetFileDesc backup_set_desc;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_backup_set_(result, backup_set_desc))) {
      LOG_WARN("failed to parse backup set result", K(ret));
    } else if (OB_FAIL(backup_set_descs.push_back(backup_set_desc))) {
      LOG_WARN("failed to push back backup_set_desc", K(ret));
    }
  }
  return ret;
}

int ObBackupSetFileOperator::update_backup_set_file_status(
    common::ObISQLClient &proxy,
    const ObBackupSetFileDesc &backup_set_desc)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}
/*
 *------------------------------__all_backup_job----------------------------
 */

int ObBackupJobOperator::insert_job(
    common::ObISQLClient &proxy, 
    const ObBackupJobAttr &job_attr)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupJobOperator::fill_dml_with_job_(const ObBackupJobAttr &job_attr, ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dml.add_pk_column(OB_STR_JOB_ID, job_attr.job_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_TENANT_ID, job_attr.tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_INCARNATION, job_attr.incarnation_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_SET_ID, job_attr.backup_set_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_INITIATOR_TENANT_ID, job_attr.initiator_tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_INITIATOR_JOB_ID, job_attr.initiator_job_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_PLUS_ARCHIVELOG, job_attr.get_plus_archivelog_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_TYPE, job_attr.backup_type_.get_backup_type_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_ENCRYPTION_MODE, ObBackupEncryptionMode::to_str(job_attr.encryption_mode_)))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_PASSWD, job_attr.passwd_.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_PATH, job_attr.backup_path_.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_START_TS, job_attr.start_ts_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_END_TS, job_attr.end_ts_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_STATUS, job_attr.status_.get_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_RESULT, job_attr.result_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_BACKUP_STR_JOB_LEVEL, job_attr.backup_level_.get_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_DESCRIPTION, job_attr.description_.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(job_attr.get_executor_tenant_id_str(dml))) {
    LOG_WARN("fail to get backup tenant id str", K(ret), K(job_attr));
  } 
  return ret;
}

int ObBackupJobOperator::get_jobs(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id,
    bool need_lock, 
    ObIArray<ObBackupJobAttr> &job_attrs)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  HEAP_VAR(ObMySQLProxy::ReadResult, res) {
    ObMySQLResult *result = NULL;
    if (OB_FAIL(fill_select_job_sql_(sql))) {
      LOG_WARN("[DATA_BACKUP]failed to fill select job sql", K(ret));
    } else if (need_lock && OB_FAIL(sql.append_fmt(" for update"))) {
      LOG_WARN("[DATA_BACKUP]failed to append sql", K(ret));
    } else if (OB_FAIL(proxy.read(res, get_exec_tenant_id(tenant_id), sql.ptr()))) {
      LOG_WARN("[DATA_BACKUP]failed to exec sql", K(ret), K(sql), K(tenant_id));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("[DATA_BACKUP]result is null", K(ret), K(sql));
    } else if (OB_FAIL(parse_job_result_(*result, job_attrs))) {
      LOG_WARN("[DATA_BACKUP]failed to parse result", K(ret));
    } else {
      LOG_INFO("[DATA_BACKUP]success get jobs", K(job_attrs));
    }
  }
  return ret;
}

int ObBackupJobOperator::get_job(
    common::ObISQLClient &proxy, 
    bool need_lock, 
    const uint64_t tenant_id,
    const int64_t job_id,
    const bool is_initiator,
    ObBackupJobAttr &job_attrs)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (!is_valid_tenant_id(tenant_id) || job_id <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(tenant_id), K(job_id));
  } else {
    HEAP_VAR(ObMySQLProxy::ReadResult, res) {
      ObMySQLResult *result = NULL;
      if (OB_FAIL(fill_select_job_sql_(sql))) {
        LOG_WARN("[DATA_BACKUP]failed to fill select backup job sql", K(ret));
      } else if (OB_FAIL(sql.append_fmt(" where %s=%lu", OB_STR_TENANT_ID, tenant_id))) {
        LOG_WARN("[DATA_BACKUP]failed to fill select backup job sql", K(ret), K(tenant_id));
      } else if (!is_initiator && OB_FAIL(sql.append_fmt(" and %s=%ld", OB_STR_JOB_ID, job_id))) {
        LOG_WARN("fail to append sql", K(ret));
      } else if (is_initiator && OB_FAIL(sql.append_fmt(" and %s=%ld", OB_STR_INITIATOR_JOB_ID, job_id))) {
        LOG_WARN("fail to append sql", K(ret));
      } else if (need_lock && OB_FAIL(sql.append_fmt(" for update"))) {
        LOG_WARN("[DATA_BACKUP]failed to append sql", K(ret));
      } else if (OB_FAIL(proxy.read(res, get_exec_tenant_id(tenant_id), sql.ptr()))) {
        LOG_WARN("[DATA_BACKUP]failed to exec sql", K(ret), K(sql), K(tenant_id));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("[DATA_BACKUP]result is null", K(ret), K(sql));
      } else if (OB_FAIL(result->next())) {
        if (OB_ITER_END == ret) {
          ret = OB_ENTRY_NOT_EXIST;
        }
        LOG_WARN("[DATA_BACKUP]failed to get next row", K(ret));
      } else if (OB_FAIL(do_parse_job_result_(*result, job_attrs))) {
        LOG_WARN("[DATA_BACKUP]failed to parse job result", K(ret));
      }
    }
  }
  return ret;
}


int ObBackupJobOperator::cnt_jobs(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id, 
    const uint64_t initiator_tenant_id, 
    int64_t &cnt)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObArray<ObBackupJobAttr> job_attrs;
  if (tenant_id == OB_INVALID_TENANT_ID || initiator_tenant_id == OB_INVALID_TENANT_ID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret));
  } else {
    HEAP_VAR(ObMySQLProxy::ReadResult, res) {
      ObMySQLResult *result = NULL;
      if (OB_FAIL(fill_select_job_sql_(sql))) {
        LOG_WARN("[DATA_BACKUP]failed to fill select backup job sql", K(ret));
      } else if (OB_FAIL(sql.append_fmt(" where %s=%lu and %s=%lu", 
          OB_STR_TENANT_ID, tenant_id, OB_STR_INITIATOR_TENANT_ID, initiator_tenant_id))) {
        LOG_WARN("[DATA_BACKUP]failed to append fmt", K(ret));
      } else if (OB_FAIL(proxy.read(res, get_exec_tenant_id(tenant_id), sql.ptr()))) {
        LOG_WARN("[DATA_BACKUP]failed to exec sql", K(ret), K(sql), K(tenant_id));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("[DATA_BACKUP]result is null", K(ret), K(sql));
      } else if (OB_FAIL(parse_job_result_(*result, job_attrs))) {
        LOG_WARN("[DATA_BACKUP]failed to parse result", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      cnt = job_attrs.count();
    }
  }
  return ret;
}

int ObBackupJobOperator::cancel_jobs(common::ObISQLClient &proxy, const uint64_t tenant_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupJobOperator::fill_select_job_sql_(ObSqlString &sql)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupJobOperator::parse_job_result_(
    sqlclient::ObMySQLResult &result, 
    common::ObIArray<ObBackupJobAttr> &jobs)
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObBackupJobAttr job;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("[DATA_BACKUP]failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_job_result_(result, job))) {
      LOG_WARN("[DATA_BACKUP]failed to parse job result", K(ret));
    } else if (OB_FAIL(jobs.push_back(job))) {
      LOG_WARN("[DATA_BACKUP]failed to push back job", K(ret), K(job));
    }
  }
  return ret;
}

int ObBackupJobOperator::do_parse_job_result_(
    ObMySQLResult &result, 
    ObBackupJobAttr &job)
{
  int ret = OB_SUCCESS;
  int64_t real_length = 0;
  int64_t pos = 0;
  char backup_dest[OB_MAX_BACKUP_DEST_LENGTH] = "";
  char status_str[OB_DEFAULT_STATUS_LENTH] = "";
  char encryption_mode_str[OB_DEFAULT_STATUS_LENTH] = "";
  char passwd[OB_MAX_PASSWORD_LENGTH] = "";
  char backup_type_str[OB_SYS_TASK_TYPE_LENGTH] = "";
  char plus_archivelog[OB_SYS_TASK_TYPE_LENGTH] = "";
  char backup_level[OB_SYS_TASK_TYPE_LENGTH] = "";
  char executor_tenant_id_str[OB_INNER_TABLE_DEFAULT_VALUE_LENTH] = "";
  
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_JOB_ID, job.job_id_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TENANT_ID, job.tenant_id_, uint64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INCARNATION, job.incarnation_id_, uint64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_BACKUP_SET_ID, job.backup_set_id_, int64_t);  
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INITIATOR_TENANT_ID, job.initiator_tenant_id_, uint64_t);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_PLUS_ARCHIVELOG, plus_archivelog, OB_SYS_TASK_TYPE_LENGTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_TYPE, backup_type_str, OB_SYS_TASK_TYPE_LENGTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_ENCRYPTION_MODE, encryption_mode_str, OB_DEFAULT_STATUS_LENTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_PASSWD, passwd, OB_MAX_PASSWORD_LENGTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_PATH, backup_dest, OB_MAX_BACKUP_DEST_LENGTH, real_length);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_START_TS, job.start_ts_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_END_TS, job.end_ts_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INITIATOR_JOB_ID, job.initiator_job_id_, int64_t);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_STATUS, status_str, OB_DEFAULT_STATUS_LENTH, real_length);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_RESULT, job.result_, int);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_RETRY_COUNT, job.retry_count_, int);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_BACKUP_STR_JOB_LEVEL, backup_level, OB_SYS_TASK_TYPE_LENGTH, real_length);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_EXECUTOR_TENANT_ID, executor_tenant_id_str, OB_INNER_TABLE_DEFAULT_VALUE_LENTH, real_length);

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(do_parse_desc_result_(result, job))) {
    LOG_WARN("fail to do parse desc result", K(ret));
  } else if (OB_FAIL(job.backup_path_.assign(backup_dest))) {
    LOG_WARN("[DATA_BACKUP]failed to set backup dest", K(ret), K(backup_dest));
  } else if (OB_FAIL(job.status_.set_status(status_str))) {
    LOG_WARN("[DATA_BACKUP]failed to set status", K(ret), K(status_str));
  } else if (OB_FAIL(job.passwd_.assign(passwd))) {
    LOG_WARN("[DATA_BACKUP]failed to set passwd", K(ret), K(passwd));
  } else if (OB_FAIL(job.backup_type_.set_backup_type(backup_type_str))) {
    LOG_WARN("[DATA_BACKUP]failed to set backup_type", K(ret), K(backup_type_str));
  } else if (OB_FAIL(job.set_plus_archivelog(plus_archivelog))) {
    LOG_WARN("[DATA_BACKUP]failed to set plus archivelog", K(ret), K(backup_type_str));
  } else if (OB_FAIL(job.backup_level_.set_level(backup_level))) {
    LOG_WARN("[DATA_BACKUP]failed to set backup level", K(ret), K(backup_level));
  } else if (OB_FAIL(job.set_executor_tenant_id(ObString(executor_tenant_id_str)))) {
    LOG_WARN("[DATA_BACKUP]set backup tenant id failed", K(ret), K(executor_tenant_id_str));
  } else { 
    job.encryption_mode_ = ObBackupEncryptionMode::parse_str(encryption_mode_str);
  } 

  return ret;
}

int ObBackupJobOperator::do_parse_desc_result_(ObMySQLResult &result, ObBackupJobAttr &job)
{
  int ret = OB_SUCCESS;
  int64_t real_length = 0;
  char desc_str[OB_INNER_TABLE_DEFAULT_VALUE_LENTH] = "";
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_DESCRIPTION, desc_str, OB_INNER_TABLE_DEFAULT_VALUE_LENTH, real_length);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(job.description_.assign(desc_str))) {
    LOG_WARN("fail to assign desc str", K(ret));
  } 
  return ret;
}

int ObBackupJobOperator::advance_job_status(
      common::ObISQLClient &proxy, 
      const ObBackupJobAttr &job_attr,
      const ObBackupStatus &next_status, 
      const int result,
      const int64_t end_ts)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupJobOperator::move_job_to_his(common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t job_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupJobOperator::update_retry_count(
    common::ObISQLClient &proxy, 
    const ObBackupJobAttr &job_attr)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupJobOperator::update_comment(common::ObISQLClient &proxy, const ObBackupJobAttr &job_attr)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

/*
 *-----------------------------------__all_tablet_to_ls----------------------------
 */


int ObBackupTabletToLSOperator::get_ls_and_tablet(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id,
    const share::SCN &snapshot,
    common::hash::ObHashMap<ObLSID, ObArray<ObTabletID>> &tablet_to_ls)
{
  int ret = OB_SUCCESS;
  if (tenant_id == OB_INVALID_TENANT_ID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(tenant_id));
  } else {
    const int64_t range_size = 10000;
    ObTabletID start_tablet_id(ObTabletID::MIN_USER_TABLET_ID);
    ObArray<ObTabletLSPair> tablet_ls_pairs;
    while(OB_SUCC(ret)) {
      tablet_ls_pairs.reset();
      if (OB_FAIL(range_get_tablet_(
          proxy, tenant_id, snapshot, start_tablet_id, range_size, tablet_ls_pairs))) {
        LOG_WARN("fail to range get tablet", K(ret), K(tenant_id), K(start_tablet_id), K(range_size));
      } else if (OB_FAIL(group_tablets_by_ls_(tablet_ls_pairs, tablet_to_ls, start_tablet_id))) {
        LOG_WARN("fail to do parse tablet ls pairs", K(ret));
      } else if (tablet_ls_pairs.count() < range_size) {
        break;
      } 
    }
  }
  return ret;
}

int ObBackupTabletToLSOperator::range_get_tablet_(common::ObISQLClient &sql_proxy, const uint64_t tenant_id, 
    const share::SCN &snapshot, const oceanbase::common::ObTabletID &start_tablet_id, const int64_t range_size, 
    common::ObIArray<ObTabletLSPair> &tablet_ls_pairs)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupTabletToLSOperator::group_tablets_by_ls_(const ObIArray<share::ObTabletLSPair> &tablet_ls_pairs, 
    common::hash::ObHashMap<ObLSID, ObArray<ObTabletID>> &tablet_to_ls, ObTabletID &max_tablet_id)
{
  int ret = OB_SUCCESS;
  ObTabletID tmp_max_tablet_id(ObTabletID::MIN_USER_TABLET_ID);
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ls_pairs.count(); ++i) {
    const ObTabletLSPair &tablet_ls_pair = tablet_ls_pairs.at(i);
    ObLSID ls_id = tablet_ls_pair.get_ls_id();
    tmp_max_tablet_id = std::max(tmp_max_tablet_id.id(), tablet_ls_pair.get_tablet_id().id());
    ObArray<ObTabletID> *tablet_ids = nullptr;
    if (OB_ISNULL(tablet_ids = tablet_to_ls.get(ls_id))) {
      ObArray<ObTabletID> cur_tablet_ids;
      if (OB_FAIL(cur_tablet_ids.push_back(tablet_ls_pair.get_tablet_id()))) {
        LOG_WARN("failed to push back tablet ls pair", K(ret));
      } else if (OB_FAIL(tablet_to_ls.set_refactored(tablet_ls_pair.get_ls_id(), cur_tablet_ids, 1))) {
        LOG_WARN("failed to set ls id", K(ret));
      }  
    } else if (OB_FAIL(tablet_ids->push_back(tablet_ls_pair.get_tablet_id()))) {
      LOG_WARN("fail to append tablet ids", K(ret));
    } 
  }
  if (OB_SUCC(ret)) {
    max_tablet_id = tmp_max_tablet_id;
  }
  return ret;
}

int ObBackupTabletToLSOperator::get_ls_of_tablet(
       common::ObISQLClient &proxy, 
       const uint64_t tenant_id, 
       const ObTabletID &tablet_id, 
       ObLSID &ls_id,
       int64_t &transfer_seq)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

/*
 *----------------------------__all_backup_task----------------------------
 */

int ObBackupTaskOperator::insert_backup_task(
    common::ObISQLClient &proxy, 
    const ObBackupSetTaskAttr &backup_set_task)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupTaskOperator::fill_dml_with_backup_task_(
    const ObBackupSetTaskAttr &backup_set_task, 
    ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dml.add_pk_column(OB_STR_TASK_ID, backup_set_task.task_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_TENANT_ID, backup_set_task.tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_JOB_ID, backup_set_task.job_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_SET_ID, backup_set_task.backup_set_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_DATA_TURN_ID, backup_set_task.data_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_META_TURN_ID, backup_set_task.meta_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_STATUS, backup_set_task.status_.get_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_ENCRYPTION_MODE, ObBackupEncryptionMode::to_str(backup_set_task.encryption_mode_)))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_PASSWD, backup_set_task.passwd_.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_PATH, backup_set_task.backup_path_.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_RESULT, backup_set_task.result_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_START_TS, backup_set_task.start_ts_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_uint64_column(OB_STR_USER_LS_START_SCN, backup_set_task.user_ls_start_scn_.get_val_for_inner_table_field()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_END_TS, backup_set_task.end_ts_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_uint64_column(OB_STR_START_SCN, backup_set_task.start_scn_.get_val_for_inner_table_field()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_uint64_column(OB_STR_END_SCN, backup_set_task.end_scn_.get_val_for_inner_table_field()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_INCARNATION, backup_set_task.incarnation_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_MINOR_TURN_ID, backup_set_task.minor_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_MAJOR_TURN_ID, backup_set_task.major_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  }
  return ret;
}

int ObBackupTaskOperator::get_backup_task(
    common::ObISQLClient &proxy, 
    const int64_t job_id, 
    const uint64_t tenant_id, 
    const bool for_update,
    ObBackupSetTaskAttr &set_task_attr)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupTaskOperator::advance_task_status(
    common::ObISQLClient &proxy, 
    const ObBackupSetTaskAttr &set_task_attr,
    const ObBackupStatus &next_status, 
    const int result,
    const SCN &end_scn,
    const int64_t end_ts)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupTaskOperator::move_task_to_his(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id, 
    const int64_t job_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}
 
int ObBackupTaskOperator::update_stats(
    common::ObISQLClient &proxy, 
    const int64_t task_id, 
    const uint64_t tenant_id, 
    const ObBackupStats &stats)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupTaskOperator::update_turn_id(common::ObISQLClient &proxy, share::ObBackupStatus &backup_status, 
    const int64_t task_id, const uint64_t tenant_id, const int64_t turn_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupTaskOperator::update_user_ls_start_scn(common::ObISQLClient &proxy, const int64_t task_id, 
    const uint64_t tenant_id, const SCN &scn)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}
/*
 *--------------------------__all_backup_ls_task---------------------------------
 */

int ObBackupLSTaskOperator::insert_ls_task(
    common::ObISQLClient &proxy,
    const ObBackupLSTaskAttr &ls_attr)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskOperator::report_ls_task(common::ObISQLClient &proxy, const ObBackupLSTaskAttr &ls_attr) 
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskOperator::report_ls_task(
    common::ObISQLClient &proxy, const ObBackupLSTaskAttr &ls_attr, const ObSqlString &extra_condition)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}



int ObBackupLSTaskOperator::fill_dml_with_ls_task_(
    const ObBackupLSTaskAttr &ls_attr, 
    ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;

  char server_ip[OB_MAX_SERVER_ADDR_SIZE] = "";
  char trace_id_str[OB_MAX_TRACE_ID_BUFFER_SIZE] = "";
  if (ls_attr.dst_.is_valid()) {
    ls_attr.dst_.ip_to_string(server_ip, OB_MAX_SERVER_ADDR_SIZE);
  }

  if (!ls_attr.task_trace_id_.is_invalid()) {
    ls_attr.task_trace_id_.to_string(trace_id_str, OB_MAX_TRACE_ID_BUFFER_SIZE);
  }
  ObSqlString black_server_sql_string;
  if (!ls_attr.black_servers_.empty()) {
    if (OB_FAIL(ls_attr.get_black_server_str(ls_attr.black_servers_, black_server_sql_string))) {
      LOG_WARN("failed to get black server str", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_TASK_ID, ls_attr.task_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_TENANT_ID, ls_attr.tenant_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column(OB_STR_LS_ID, ls_attr.ls_id_.id()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_JOB_ID, ls_attr.job_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_SET_ID, ls_attr.backup_set_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BACKUP_TYPE, ls_attr.backup_type_.get_backup_type_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_TASK_TYPE, ls_attr.task_type_.get_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_STATUS, ls_attr.status_.get_str()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_START_TS, ls_attr.start_ts_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_END_TS, ls_attr.end_ts_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_DATE, ls_attr.backup_date_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_BLACK_LIST, black_server_sql_string.empty() ? "" : black_server_sql_string.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_SEVER_IP, server_ip))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_SERVER_PORT, ls_attr.dst_.get_port()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_TASK_TRACE_ID, trace_id_str))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_OUTPUT_BYTES, ls_attr.stats_.output_bytes_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_INPUT_BYTES, ls_attr.stats_.input_bytes_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_TABLET_COUNT, ls_attr.stats_.tablet_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_FINISH_TABLET_COUNT, ls_attr.stats_.finish_tablet_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_MACRO_BLOCK_COUNT, ls_attr.stats_.macro_block_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_FINISH_MACRO_BLOCK_COUNT, ls_attr.stats_.finish_macro_block_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_EXTRA_BYTES, ls_attr.stats_.extra_bytes_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_FILE_COUNT, ls_attr.stats_.finish_file_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_START_TURN_ID, ls_attr.start_turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_TURN_ID, ls_attr.turn_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_RETRY_ID, ls_attr.retry_id_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_RESULT, ls_attr.result_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_COMMENT, ls_attr.comment_.ptr()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_uint64_column(OB_STR_MAX_TABLET_CHECKPOINT_SCN, ls_attr.max_tablet_checkpoint_scn_.get_val_for_inner_table_field()))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_LOG_FILE_COUNT, ls_attr.stats_.log_file_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_column(OB_STR_FINISH_LOG_FILE_COUNT, ls_attr.stats_.finish_log_file_count_))) {
    LOG_WARN("[DATA_BACKUP]failed to add column", K(ret));
  }
  return ret;
}

int ObBackupLSTaskOperator::get_ls_tasks(
    common::ObISQLClient &proxy, 
    const int64_t job_id, 
    const uint64_t tenant_id,
    bool need_lock, 
    ObIArray<ObBackupLSTaskAttr> &ls_attrs)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ls_attrs.reset();
  if (job_id <= 0 || tenant_id == OB_INVALID_TENANT_ID) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(tenant_id), K(tenant_id));
  } else {
    HEAP_VAR(ObMySQLProxy::ReadResult, res) {
      ObMySQLResult *result = NULL;
      if (OB_FAIL(fill_select_ls_task_sql_(sql))) {
        LOG_WARN("[DATA_BACKUP]failed to fill select ls task sql", K(ret));
      } else if (OB_FAIL(sql.append_fmt(" where %s=%ld", OB_STR_JOB_ID, job_id))) {
      } else if (need_lock && OB_FAIL(sql.append_fmt(" for update"))) {
        LOG_WARN("[DATA_BACKUP]failed to append sql", K(ret));
      } else if (OB_FAIL(proxy.read(res, get_exec_tenant_id(tenant_id), sql.ptr()))) {
        LOG_WARN("[DATA_BACKUP]failed to exec sql", K(ret), K(sql), K(tenant_id));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("[DATA_BACKUP]result is null", K(ret), K(sql));
      } else if (OB_FAIL(parse_ls_result_(*result, ls_attrs))) {
        LOG_WARN("[DATA_BACKUP]failed to parse result", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("succ to get ls tasks", K(ls_attrs));
    }
  }
  return ret;
}

int ObBackupLSTaskOperator::get_ls_task(
    common::ObISQLClient &proxy, 
    bool need_lock, 
    const int64_t task_id,
    const uint64_t tenant_id, 
    const ObLSID &ls_id, 
    ObBackupLSTaskAttr &ls_attr)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (task_id <= 0 || tenant_id == OB_INVALID_TENANT_ID || !ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(task_id), K(tenant_id));
  } else {
    HEAP_VAR(ObMySQLProxy::ReadResult, res) {
      ObMySQLResult *result = NULL;
      if (OB_FAIL(fill_select_ls_task_sql_(sql))) {
        LOG_WARN("[DATA_BACKUP]failed to fill select ls task sql", K(ret));
      } else if (OB_FAIL(sql.append_fmt(" where %s=%ld and %s=%ld and %s=%ld",
          OB_STR_TASK_ID, task_id, OB_STR_LS_ID, ls_id.id(), OB_STR_TENANT_ID, tenant_id))) {
        LOG_WARN("[DATA_BACKUP]failed append sql", K(ret),K(sql));
      } else if (need_lock && OB_FAIL(sql.append_fmt(" for update"))) {
        LOG_WARN("[DATA_BACKUP]failed to append sql", K(ret));
      } else if (OB_FAIL(proxy.read(res, get_exec_tenant_id(tenant_id), sql.ptr()))) {
        LOG_WARN("[DATA_BACKUP]failed to exec sql", K(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("[DATA_BACKUP]result is null", K(ret), K(sql));
      } else if (OB_FAIL(result->next())) {
        if (OB_ITER_END == ret) {
          ret = OB_ENTRY_NOT_EXIST;
        } else {
          LOG_WARN("[DATA_BACKUP]failed to get next", K(ret));
        }
      } else if (OB_FAIL(do_parse_ls_result_(*result, ls_attr))) {
        LOG_WARN("[DATA_BACKUP]failed to do parse ls result");
      } else {
        LOG_INFO("[DATA_BACKUP]success get ls task", K(ls_attr));
      }
    }
  }
  return ret;
}

int ObBackupLSTaskOperator::fill_select_ls_task_sql_(ObSqlString &sql)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskOperator::parse_ls_result_(
    sqlclient::ObMySQLResult &result, 
    ObIArray<ObBackupLSTaskAttr> &ls_attrs)
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObBackupLSTaskAttr ls_attr;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("[DATA_BACKUP]failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_ls_result_(result, ls_attr))) {
      LOG_WARN("[DATA_BACKUP]failed to parse ls result", K(ret));
    } else if (OB_FAIL(ls_attrs.push_back(ls_attr))) {
      LOG_WARN("[DATA_BACKUP]failed to push back ls", K(ret));
    }
  }
  return ret;
}

int ObBackupLSTaskOperator::do_parse_ls_result_(ObMySQLResult &result, ObBackupLSTaskAttr &ls_attr)
{

  int ret = OB_SUCCESS;

  HEAP_VARS_2((char[OB_INNER_TABLE_DEFAULT_VALUE_LENTH], black_list_str), (char[MAX_TABLE_COMMENT_LENGTH], comment_str)) {
    int64_t real_length = 0;
    char status_str[OB_DEFAULT_STATUS_LENTH] = "";
    char backup_type_str[OB_SYS_TASK_TYPE_LENGTH] = "";
    char task_type_str[64] = "";
    char trace_id_str[OB_MAX_TRACE_ID_BUFFER_SIZE] = "";
    uint64_t max_tablet_checkpoint_scn = 0;
    
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TASK_ID, ls_attr.task_id_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_JOB_ID, ls_attr.job_id_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TENANT_ID, ls_attr.tenant_id_, uint64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_BACKUP_SET_ID, ls_attr.backup_set_id_, int64_t);  
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_LS_ID, ls_attr.ls_id_, int64_t);  
    EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_TYPE, backup_type_str, OB_SYS_TASK_TYPE_LENGTH, real_length);
    EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_STATUS, status_str, OB_DEFAULT_STATUS_LENTH, real_length);
    EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_TASK_TYPE, task_type_str, 64, real_length);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_START_TS, ls_attr.start_ts_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_END_TS, ls_attr.end_ts_, int64_t);
    EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BLACK_LIST, black_list_str, OB_INNER_TABLE_DEFAULT_VALUE_LENTH, real_length);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_DATE, ls_attr.backup_date_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TURN_ID, ls_attr.turn_id_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_RETRY_ID, ls_attr.retry_id_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_RESULT, ls_attr.result_, int);
    EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_TASK_TRACE_ID, trace_id_str, OB_MAX_TRACE_ID_BUFFER_SIZE, real_length);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INPUT_BYTES, ls_attr.stats_.input_bytes_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_OUTPUT_BYTES, ls_attr.stats_.output_bytes_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TABLET_COUNT, ls_attr.stats_.tablet_count_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FINISH_TABLET_COUNT, ls_attr.stats_.finish_tablet_count_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_MACRO_BLOCK_COUNT, ls_attr.stats_.macro_block_count_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FINISH_MACRO_BLOCK_COUNT, ls_attr.stats_.finish_macro_block_count_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_EXTRA_BYTES, ls_attr.stats_.extra_bytes_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_START_TURN_ID, ls_attr.start_turn_id_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FILE_COUNT, ls_attr.stats_.finish_file_count_, int64_t);
    EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_COMMENT, comment_str, MAX_TABLE_COMMENT_LENGTH, real_length);
    EXTRACT_UINT_FIELD_MYSQL(result, OB_STR_MAX_TABLET_CHECKPOINT_SCN, max_tablet_checkpoint_scn, uint64_t);  
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_LOG_FILE_COUNT, ls_attr.stats_.log_file_count_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FINISH_LOG_FILE_COUNT, ls_attr.stats_.finish_log_file_count_, int64_t);

    char server_str[OB_MAX_SERVER_ADDR_SIZE] = { 0 };
    int64_t port = 0;
    EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_SEVER_IP, server_str, OB_MAX_SERVER_ADDR_SIZE, real_length);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_SERVER_PORT, port, int64_t);

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ls_attr.status_.set_status(status_str))) {
      LOG_WARN("[DATA_BACKUP]failed to set status", K(ret), K(status_str));
    } else if (OB_FAIL(ls_attr.backup_type_.set_backup_type(backup_type_str))) {
      LOG_WARN("[DATA_BACKUP]failed to set backup_type", K(ret), K(backup_type_str));
    } else if (OB_FAIL(ls_attr.task_type_.set_type(task_type_str))) {
      LOG_WARN("[DATA_BACKUP]failed to set task type", K(ret), K(task_type_str));
    } else if (OB_FAIL(ls_attr.set_black_servers(black_list_str))) {
      LOG_WARN("[DATA_BACKUP]failed to parse black list str", K(ret)); 
    } else if (strcmp(trace_id_str, "") != 0 && OB_FAIL(ls_attr.task_trace_id_.set(trace_id_str))) {
      LOG_WARN("[DATA_BACKUP]failed to set task trace id", K(ret), K(trace_id_str));
    } else if (!ls_attr.dst_.set_ip_addr(server_str, static_cast<int32_t>(port))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("fail to set server ip and port", K(ret), K(server_str), K(port));
    } else if (OB_FAIL(ls_attr.comment_.assign(comment_str))) {
      LOG_WARN("failed to assign comment str", K(ret));
    } else if (OB_FAIL(ls_attr.max_tablet_checkpoint_scn_.convert_for_inner_table_field(max_tablet_checkpoint_scn))) {
      LOG_WARN("fail to set max tablet checkpoint scn", K(ret), K(max_tablet_checkpoint_scn));
    } else {
      LOG_INFO("[DATA_BACKUP]success to read ls attr", K(ls_attr));
    }
  }
  return ret;
}

int ObBackupLSTaskOperator::update_dst_and_status(
    common::ObISQLClient &proxy, 
    const int64_t task_id,
    const uint64_t tenant_id, 
    const ObLSID &ls_id, 
    const int64_t turn_id, 
    const int64_t retry_id,
    share::ObTaskId task_trace_id,
    common::ObAddr &dst)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskOperator::update_stats(
    common::ObISQLClient &proxy, 
    const int64_t task_id, 
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    const ObBackupStats &stats) 
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskOperator::insert_build_index_task(
    common::ObISQLClient &proxy, 
    const ObBackupLSTaskAttr &build_index_attr)
{
  int ret = OB_SUCCESS;
  if (!build_index_attr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(build_index_attr));
  } else if (OB_FAIL(insert_ls_task(proxy, build_index_attr))) {
    LOG_WARN("[DATA_BACKUP]failed to insert build index task", K(ret), K(build_index_attr));
  } else {
    LOG_INFO("[DATA_BACKUP]insert build index task succ", K(build_index_attr));
  }
  return ret;
}
int ObBackupLSTaskOperator::delete_build_index_task(
    common::ObISQLClient &proxy, 
    const ObBackupLSTaskAttr &build_index_attr)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskOperator::move_ls_to_his(common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t job_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskOperator::delete_ls_task_without_sys(common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t task_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskOperator::update_max_tablet_checkpoint_scn(
    common::ObISQLClient &proxy, 
    const int64_t task_id, 
    const uint64_t tenant_id,
    const ObLSID &ls_id, 
    const SCN &max_tablet_checkpoint_scn)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


/*
 *--------------------------__all_backup_skipped_tablet------------------------------
 */

int ObBackupSkippedTabletOperator::batch_move_skip_tablet(
    common::ObMySQLProxy &proxy, const uint64_t tenant_id, const int64_t task_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSkippedTabletOperator::get_skip_tablet(
    common::ObISQLClient &proxy, 
    const bool need_lock,
    const uint64_t tenant_id,
    const int64_t task_id,
    const share::ObBackupSkippedType skipped_type,
    common::hash::ObHashSet<ObBackupSkipTabletAttr> &skip_tablets)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_INVALID_TENANT_ID == tenant_id || task_id <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("[DATA_BACKUP]invalid argument", K(ret), K(tenant_id), K(task_id));
  } else {
    HEAP_VAR(ObMySQLProxy::ReadResult, res) {
      ObMySQLResult *result = NULL;
      if (OB_FAIL(fill_select_skip_tablet_sql_(sql))) {
        LOG_WARN("[DATA_BACKUP]failed to fill select ls task sql", K(ret));
      } else if (OB_FAIL(sql.append_fmt(" where %s=%lu and %s=%ld and %s='%s'",
          OB_STR_TENANT_ID, tenant_id, OB_STR_TASK_ID, task_id,
          OB_STR_BACKUP_SKIPPED_TYPE, skipped_type.str()))) {
        LOG_WARN("[DATA_BACKUP]failed to append sql", K(ret), K(tenant_id), K(task_id), K(skipped_type));
      } else if (need_lock && OB_FAIL(sql.append_fmt(" for update"))) {
        LOG_WARN("[DATA_BACKUP]failed to append sql", K(ret));
      } else if (OB_FAIL(proxy.read(res, get_exec_tenant_id(tenant_id), sql.ptr()))) {
        LOG_WARN("[DATA_BACKUP]failed to exec sql", K(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("[DATA_BACKUP]result is null", K(ret), K(sql));
      } else if (OB_FAIL(parse_skip_tablet_result_(*result, skip_tablets))) {
        LOG_WARN("[DATA_BACKUP]failed to parse result", K(ret));
      }
    }
  }
  return ret;
}

int ObBackupSkippedTabletOperator::fill_select_skip_tablet_sql_(ObSqlString &sql)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupSkippedTabletOperator::parse_skip_tablet_result_(
    sqlclient::ObMySQLResult &result, 
    common::hash::ObHashSet<ObBackupSkipTabletAttr> &skip_tablets)
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObBackupSkipTabletAttr tablet_attr;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("[DATA_BACKUP]failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_skip_tablet_result_(result, tablet_attr))) {
      LOG_WARN("[DATA_BACKUP]failed to parse tablet result", K(ret));
    } else if (OB_FAIL(skip_tablets.set_refactored(tablet_attr, 1/*cover exist*/))) {
      LOG_WARN("[DATA_BACKUP]failed to push back tablet attr", K(ret));
    }
  }
  return ret;
}

int ObBackupSkippedTabletOperator::do_parse_skip_tablet_result_(
    ObMySQLResult &result, 
    ObBackupSkipTabletAttr &tablet_attr)
{
  int ret = OB_SUCCESS;
  int64_t real_length = 0;
  int64_t tablet_id = -1;
  char skipped_type_str[OB_INNER_TABLE_DEFAULT_VALUE_LENTH] = "";
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TABLET_ID, tablet_id, int64_t);
  EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_BACKUP_SKIPPED_TYPE, skipped_type_str, OB_INNER_TABLE_DEFAULT_VALUE_LENTH, real_length);
  tablet_attr.tablet_id_ = tablet_id;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(tablet_attr.skipped_type_.parse_from_str(skipped_type_str))) {
    LOG_WARN("failed to parse from str", K(ret), K(skipped_type_str));
  }
  return ret;
}

int ObBackupSkippedTabletOperator::move_skip_tablet_to_his(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id, 
    const int64_t task_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupMViewOperator::get_all_major_compaction_mview_dep_tablet_list(
    common::ObMySQLProxy &proxy,
    const uint64_t tenant_id,
    const share::SCN &snapshot,
    common::ObIArray<common::ObTabletID> &tablet_list,
    common::ObIArray<share::SCN> &tablet_mview_dep_scn_list)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

/*
 *---------------------------__all_backup_ls_task_info------------------------
 */

int ObBackupLSTaskInfoOperator::update_ls_task_info_final(
    common::ObISQLClient &proxy, 
    const int64_t task_id, 
    const uint64_t tenant_id,
    const ObLSID &ls_id, 
    const int64_t turn_id, 
    const int64_t retry_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskInfoOperator::get_statistics_info(
    common::ObISQLClient &proxy, 
    const int64_t task_id, 
    const uint64_t tenant_id, 
    const ObLSID &ls_id, 
    const int64_t turn_id, 
    const int64_t retry_id, 
    ObIArray<ObBackupLSTaskInfoAttr> &task_infos)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObBackupLSTaskInfoOperator::parse_ls_task_info_(
    sqlclient::ObMySQLResult &result, 
    ObIArray<ObBackupLSTaskInfoAttr> &task_infos)
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObBackupLSTaskInfoAttr task_info;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("[DATA_BACKUP]failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_ls_task_info_(result, task_info))) {
      LOG_WARN("[DATA_BACKUP]failed to parse ls result", K(ret));
    } else if (OB_FAIL(task_infos.push_back(task_info))) {
      LOG_WARN("[DATA_BACKUP]failed to push back ls", K(ret));
    }
  }
  return ret;
}

int ObBackupLSTaskInfoOperator::do_parse_ls_task_info_(
    sqlclient::ObMySQLResult &result, 
    ObBackupLSTaskInfoAttr &task_info)
{
  int ret = OB_SUCCESS;
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TENANT_ID, task_info.tenant_id_, uint64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TASK_ID, task_info.task_id_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_LS_ID, task_info.ls_id_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TURN_ID, task_info.turn_id_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_RETRY_ID, task_info.retry_id_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_BACKUP_DATA_TYPE, task_info.backup_data_type_, int64_t);

  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INPUT_BYTES, task_info.input_bytes_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_OUTPUT_BYTES, task_info.output_bytes_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TABLET_COUNT, task_info.tablet_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FINISH_TABLET_COUNT, task_info.finish_tablet_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_MACRO_BLOCK_COUNT, task_info.macro_block_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FINISH_MACRO_BLOCK_COUNT, task_info.finish_macro_block_count_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_EXTRA_BYTES, task_info.extra_bytes_, int64_t);
  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_FILE_COUNT, task_info.file_count_, int64_t);
  return ret;
}

int ObBackupLSTaskInfoOperator::move_ls_task_info_to_his(
    common::ObISQLClient &proxy, 
    const int64_t task_id, 
    const uint64_t tenant_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

/*
 *-----------------------__all_tenant_backup_info----------------------
 */

int ObLSBackupInfoOperator::get_next_job_id(common::ObISQLClient &trans, const uint64_t tenant_id, int64_t &job_id)
{
  int ret = OB_SUCCESS;
  InfoItem item;
  item.name_ = OB_STR_JOB_ID;
  if (OB_FAIL(get_item(trans, get_exec_tenant_id(tenant_id), item, true))) {
    if (ret == OB_BACKUP_INFO_NOT_EXIST) {
      job_id = 1/*first job id*/;
      ret = OB_SUCCESS;
    }
  } else if (OB_FAIL(ob_atoll(item.value_.ptr(), job_id))) {
    LOG_WARN("failed to change char to int64_t", K(ret), K(item));
  } else {
    ++job_id;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(set_item_value(item.value_, job_id))) {
    LOG_WARN("failed to set item value", K(ret), K(job_id));
  } else if (OB_FAIL(insert_item_with_update(trans, get_exec_tenant_id(tenant_id), item))) {
    LOG_WARN("fail to insert item with update", K(ret), K(tenant_id), K(item));
  }
  return ret;
}

int ObLSBackupInfoOperator::get_next_task_id(common::ObISQLClient &trans, const uint64_t tenant_id, int64_t &task_id)
{
  int ret = OB_SUCCESS;
  InfoItem item;
  item.name_ = OB_STR_TASK_ID;
  if (OB_FAIL(get_item(trans, get_exec_tenant_id(tenant_id), item, true))) {
    if (ret == OB_BACKUP_INFO_NOT_EXIST) {
      task_id = 1/*first job id*/;
      ret = OB_SUCCESS;
    }
  } else if (OB_FAIL(ob_atoll(item.value_.ptr(), task_id))) {
    LOG_WARN("failed to change char to int64_t", K(ret), K(item));
  } else {
    ++task_id;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(set_item_value(item.value_, task_id))) {
    LOG_WARN("failed to set item value");
  } else if (OB_FAIL(insert_item_with_update(trans, get_exec_tenant_id(tenant_id), item))) {
    LOG_WARN("fail to insert item with update", K(ret), K(tenant_id), K(item));
  }  
  return ret;
}

int ObLSBackupInfoOperator::get_next_backup_set_id(common::ObISQLClient &trans, const uint64_t &tenant_id, int64_t &backup_set_id)
{
  int ret = OB_SUCCESS;
  InfoItem item;
  item.name_ = OB_STR_BACKUP_SET_ID;
  if (OB_FAIL(get_item(trans, get_exec_tenant_id(tenant_id), item, true))) {
    if (ret == OB_BACKUP_INFO_NOT_EXIST) {
      backup_set_id = 1/*first backup set id*/;
      ret = OB_SUCCESS;
    }
  } else if (OB_FAIL(ob_atoll(item.value_.ptr(), backup_set_id))) {
    LOG_WARN("[DATA_BACKUP]failed to change char to int64_t", K(ret), K(item));
  } else {
    ++backup_set_id;
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(set_item_value(item.value_, backup_set_id))) {
    LOG_WARN("[DATA_BACKUP]failed to set item value");
  } else if (OB_FAIL(insert_item_with_update(trans, get_exec_tenant_id(tenant_id), item))) {
    LOG_WARN("fail to insert item with update", K(ret), K(tenant_id), K(item));
  }
  return ret;
}

int ObLSBackupInfoOperator::get_next_dest_id(common::ObISQLClient &trans, const uint64_t &tenant_id, int64_t &dest_id)
{
  int ret = OB_SUCCESS;
  InfoItem item;
  item.name_ = OB_STR_DEST_ID;
  if (OB_FAIL(get_item(trans, tenant_id, item, true))) {
    if (ret == OB_BACKUP_INFO_NOT_EXIST) {
      dest_id = OB_START_DEST_ID;
      ret = OB_SUCCESS;
    }
  } else if (OB_FAIL(ob_atoll(item.value_.ptr(), dest_id))) {
    LOG_WARN("failed to change char to int64_t", K(ret), K(item));
  } else {
    ++dest_id;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(set_item_value(item.value_, dest_id))) {
    LOG_WARN("failed to set item value", K(ret), K(dest_id));
  } else if (OB_FAIL(insert_item_with_update(trans, tenant_id, item))) {
    LOG_WARN("failed to insert item", K(ret), K(tenant_id), K(item));
  }
  return ret;
}

int ObLSBackupInfoOperator::set_backup_version(common::ObISQLClient &trans, const uint64_t tenant_id, const uint64_t data_version)
{
  int ret = OB_SUCCESS;
  InfoItem item;
  item.name_ = OB_STR_BACKUP_DATA_VERSION;
  if (OB_FAIL(set_item_value(item.value_, data_version))) {
    LOG_WARN("failed to set item value", K(ret), K(data_version));
  } else if (OB_FAIL(insert_item_with_update(trans, tenant_id, item))) {
    LOG_WARN("failed to insert item", K(ret), K(tenant_id), K(item));
  }
  return ret;
}
int ObLSBackupInfoOperator::get_backup_version(common::ObISQLClient &trans, const uint64_t tenant_id, uint64_t &data_version)
{
  int ret = OB_SUCCESS;
  InfoItem item;
  item.name_ = OB_STR_BACKUP_DATA_VERSION;
  char *endptr = NULL;
  if (OB_FAIL(get_item(trans, tenant_id, item, false))) {
    LOG_WARN("failed to get item", K(ret), K(tenant_id));
  } else if (OB_FAIL(ob_strtoull(item.value_.ptr(), endptr, data_version))) {
    LOG_WARN("failed str to ull", K(ret), K(item));
  } else if (OB_ISNULL(endptr) || OB_UNLIKELY('\0' != *endptr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(item));
  }
  return ret;
}

int ObLSBackupInfoOperator::set_cluster_version(common::ObISQLClient &trans, const uint64_t tenant_id, const uint64_t cluster_version)
{
  int ret = OB_SUCCESS;
  InfoItem item;
  item.name_ = OB_STR_CLUSTER_VERSION;
  if (OB_FAIL(set_item_value(item.value_, cluster_version))) {
    LOG_WARN("failed to set item value", K(ret), K(cluster_version));
  } else if (OB_FAIL(insert_item_with_update(trans, tenant_id, item))) {
    LOG_WARN("failed to insert item", K(ret), K(tenant_id), K(item));
  }
  return ret;
}

int ObLSBackupInfoOperator::get_cluster_version(common::ObISQLClient &trans, const uint64_t tenant_id, uint64_t &cluster_version)
{
  int ret = OB_SUCCESS;
  InfoItem item;
  item.name_ = OB_STR_CLUSTER_VERSION;
  char *endptr = NULL;
  if (OB_FAIL(get_item(trans, tenant_id, item, false))) {
    LOG_WARN("failed to get item", K(ret), K(tenant_id));
  } else if (OB_FAIL(ob_strtoull(item.value_.ptr(), endptr, cluster_version))) {
    LOG_WARN("failed str to ull", K(ret), K(item));
  } else if (OB_ISNULL(endptr) || OB_UNLIKELY('\0' != *endptr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(item));
  }
  return ret;
}

int ObLSBackupInfoOperator::get_item(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id, 
    InfoItem &item, 
    const bool need_lock)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupInfoOperator::insert_item_with_update(
    common::ObISQLClient &proxy, 
    const uint64_t tenant_id, 
    InfoItem &item)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupInfoOperator::set_item_value(Value &dst_value, int64_t src_value)
{
  int ret = OB_SUCCESS;
  if (src_value < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("backup info set value get invalid argument", K(ret), K(src_value));
  } else if (0 == src_value) {
    if (OB_FAIL(set_item_value(dst_value, ""))) {
      LOG_WARN("failed to set value", K(ret), K(src_value));
    }
  } else {
    int strlen = sprintf(dst_value.ptr(), "%ld", src_value);
    if (strlen <= 0 || strlen > common::OB_INNER_TABLE_DEFAULT_VALUE_LENTH) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to set value", K(ret), K(src_value), K(strlen));
    }
  }
  return ret;
}

int ObLSBackupInfoOperator::set_item_value(Value &dst_value, uint64_t src_value)
{
  int ret = OB_SUCCESS;
  if (src_value < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("backup info set value get invalid argument", K(ret), K(src_value));
  } else if (0 == src_value) {
    if (OB_FAIL(set_item_value(dst_value, ""))) {
      LOG_WARN("failed to set value", K(ret), K(src_value));
    }
  } else {
    int strlen = sprintf(dst_value.ptr(), "%lu", src_value);
    if (strlen <= 0 || strlen > common::OB_INNER_TABLE_DEFAULT_VALUE_LENTH) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to set value", K(ret), K(src_value), K(strlen));
    }
  }
  return ret;
}

int ObLSBackupInfoOperator::set_item_value(Value &dst_value, const char *src_buf)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src_buf)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("set value get invalid argument", K(ret), KP(src_buf));
  } else {
    const int64_t len = strlen(src_buf);
    if (len >= OB_INNER_TABLE_DEFAULT_VALUE_LENTH) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("buffer len is unexpected", K(ret), K(len));
    } else {
      STRNCPY(dst_value.ptr(), src_buf, len);
      dst_value.ptr()[len] = '\0';
      LOG_DEBUG("set value", K(src_buf), K(strlen(src_buf)), K(dst_value));
    }
  }
  return ret;
}

}
}
