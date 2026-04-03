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

#include "storage/backup/ob_backup_operator.h"
#include "share/backup/ob_backup_data_table_operator.h"


using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase {
namespace backup {

int ObLSBackupOperator::insert_ls_backup_task_info(const uint64_t tenant_id, const int64_t task_id, const int64_t turn_id,
    const int64_t retry_id, const share::ObLSID &ls_id, const int64_t backup_set_id,
    const share::ObBackupDataType &backup_data_type, common::ObISQLClient &sql_client)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::report_ls_backup_task_info(const uint64_t tenant_id, const int64_t task_id, const int64_t turn_id,
    const int64_t retry_id, const share::ObBackupDataType &backup_data_type, const ObLSBackupStat &stat,
    common::ObISQLClient &sql_client)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::get_backup_ls_task_info(const uint64_t tenant_id, const int64_t task_id,
    const share::ObLSID &ls_id, const int64_t turn_id, const int64_t retry_id,
    const share::ObBackupDataType &backup_data_type, const bool for_update, ObBackupLSTaskInfo &task_info,
    common::ObISQLClient &sql_client)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::get_all_retries(const int64_t task_id, const uint64_t tenant_id,
    const share::ObBackupDataType &backup_data_type, const share::ObLSID &ls_id,
    common::ObIArray<ObBackupRetryDesc> &retry_list, common::ObISQLClient &sql_client)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::mark_ls_task_info_final(const int64_t task_id, const uint64_t tenant_id,
    const share::ObLSID &ls_id, const int64_t turn_id, const int64_t retry_id,
    const share::ObBackupDataType &backup_data_type, common::ObISQLClient &sql_client)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::get_prev_backup_set_desc(const uint64_t tenant_id, const int64_t backup_set_id, const int64_t dest_id,
    share::ObBackupSetFileDesc &prev_desc, common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  share::ObBackupSetFileDesc cur_desc;
  const bool for_update = false;
  if (OB_FAIL(ObBackupSetFileOperator::get_backup_set_file(
          sql_client, for_update, backup_set_id, OB_START_INCARNATION, tenant_id, dest_id, cur_desc))) {
    LOG_WARN("failed to get backup set", K(ret), K(backup_set_id), K(tenant_id));
  } else if (0 == cur_desc.prev_inc_backup_set_id_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur backup set is full backup", K(cur_desc));
  } else if (OB_FAIL(ObBackupSetFileOperator::get_backup_set_file(sql_client,
                 for_update,
                 cur_desc.prev_inc_backup_set_id_,
                 OB_START_INCARNATION,
                 tenant_id,
                 dest_id,
                 prev_desc))) {
    LOG_WARN("failed to get backup set", K(ret), K(cur_desc.prev_inc_backup_set_id_), K(tenant_id));
  }
  return ret;
}

int ObLSBackupOperator::report_ls_task_finish(const uint64_t tenant_id, const int64_t task_id, 
    const share::ObLSID &ls_id, const int64_t turn_id, const int64_t retry_id, const int64_t result, 
    common::ObISQLClient &sql_client)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::get_all_backup_ls_id(const uint64_t tenant_id, const int64_t task_id,
    common::ObIArray<share::ObLSID> &ls_array, common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_INVALID_ID == tenant_id || task_id < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", K(ret), K(task_id), K(tenant_id));
  } else if (OB_FAIL(construct_query_backup_sql_(tenant_id, task_id, sql))) {
    LOG_WARN("failed to construct query backup sql", K(ret), K(tenant_id), K(task_id));
  } else if (OB_FAIL(get_distinct_ls_id_(tenant_id, sql, ls_array, sql_client))) {
    LOG_WARN("failed to get distinct ls id", K(ret), K(tenant_id), K(sql));
  }
  return ret;
}

int ObLSBackupOperator::get_all_archive_ls_id(const uint64_t tenant_id, const int64_t dest_id,
    const share::SCN &start_scn, const share::SCN &end_scn, common::ObIArray<share::ObLSID> &ls_array,
    common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t start_piece_id = 0;
  int64_t end_piece_id = 0;
  if (OB_INVALID_ID == tenant_id || dest_id < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", K(ret), K(tenant_id));
  } else if (OB_FAIL(get_start_piece_id_(tenant_id, dest_id, start_scn, sql_client, start_piece_id))) {
    LOG_WARN("failed to get start piece id", K(ret), K(tenant_id), K(dest_id), K(start_scn));
  } else if (OB_FAIL(get_end_piece_id_(tenant_id, dest_id, end_scn, sql_client, end_piece_id))) {
    LOG_WARN("failed to get end piece id", K(ret), K(tenant_id), K(dest_id), K(end_scn));
  } else if (OB_FAIL(construct_query_archive_sql_(tenant_id, dest_id, start_piece_id, end_piece_id, sql))) {
    LOG_WARN("failed to construct query archive sql", K(ret), K(tenant_id), K(dest_id), K(start_piece_id), K(end_piece_id));
  } else if (OB_FAIL(get_distinct_ls_id_(tenant_id, sql, ls_array, sql_client))) {
    LOG_WARN("failed to get distinct ls id", K(ret), K(tenant_id), K(sql));
  }
  return ret;
}

int ObLSBackupOperator::report_tablet_skipped(
    const uint64_t tenant_id, const ObBackupSkippedTablet &skipped_tablet, common::ObISQLClient &sql_client)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::check_tablet_skipped_by_reorganize(common::ObISQLClient &sql_proxy,
    const uint64_t tenant_id, const common::ObTabletID &tablet_id, bool &has_skipped)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::get_tablet_to_ls_info(common::ObISQLClient &sql_proxy,
    const uint64_t tenant_id, const common::ObTabletID &tablet_id, int64_t &tablet_count, int64_t &tmp_ls_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::check_ls_has_sys_data(common::ObISQLClient &sql_proxy,
    const uint64_t tenant_id, const int64_t task_id, const share::ObLSID &ls_id, bool &has_sys_data)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::fill_ls_task_info_(const ObBackupLSTaskInfo &task_info, share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dml.add_pk_column("task_id", task_info.task_id_))) {
    LOG_WARN("failed to add pk column", K(task_info));
  } else if (OB_FAIL(dml.add_pk_column("turn_id", task_info.turn_id_))) {
    LOG_WARN("failed to add pk column", K(task_info));
  } else if (OB_FAIL(dml.add_pk_column("retry_id", task_info.retry_id_))) {
    LOG_WARN("failed to add pk column", K(task_info));
  } else if (OB_FAIL(dml.add_pk_column("data_type", task_info.backup_data_type_))) {
    LOG_WARN("failed to add pk column", K(task_info));
  } else if (OB_FAIL(dml.add_column("backup_set_id", task_info.backup_set_id_))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("input_bytes", task_info.input_bytes_))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("output_bytes", task_info.output_bytes_))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("tablet_count", task_info.tablet_count_))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("finish_tablet_count", task_info.finish_tablet_count_))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("macro_block_count", task_info.macro_block_count_))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("finish_macro_block_count", task_info.finish_macro_block_count_))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("max_file_id", task_info.max_file_id_))) {
    LOG_WARN("failed to add column", K(task_info));
  }
  return ret;
}

int ObLSBackupOperator::parse_ls_task_info_results_(
    sqlclient::ObMySQLResult &result, common::ObIArray<ObBackupLSTaskInfo> &task_infos)
{
  int ret = OB_SUCCESS;
  ObBackupLSTaskInfo task_info;
  while (OB_SUCC(ret)) {
    task_info.reset();
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to get next row", K(ret));
      }
    } else {
      int64_t tmp_ls_id = 0;
      EXTRACT_INT_FIELD_MYSQL(result, "task_id", task_info.task_id_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "tenant_id", task_info.tenant_id_, uint64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "ls_id", tmp_ls_id, int64_t);
      task_info.ls_id_ = tmp_ls_id;
      EXTRACT_INT_FIELD_MYSQL(result, "turn_id", task_info.turn_id_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "retry_id", task_info.retry_id_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "data_type", task_info.backup_data_type_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "backup_set_id", task_info.backup_set_id_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "input_bytes", task_info.input_bytes_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "output_bytes", task_info.output_bytes_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "tablet_count", task_info.tablet_count_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "finish_tablet_count", task_info.finish_tablet_count_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "macro_block_count", task_info.macro_block_count_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "finish_macro_block_count", task_info.finish_macro_block_count_, int64_t);
      EXTRACT_INT_FIELD_MYSQL(result, "max_file_id", task_info.max_file_id_, int64_t);

      int64_t tmp_real_str_len = 0;
      char final_str[common::OB_INNER_TABLE_DEFAULT_VALUE_LENTH] = "";
      EXTRACT_STRBUF_FIELD_MYSQL(result, "final", final_str, static_cast<int64_t>(sizeof(final_str)), tmp_real_str_len);
      if (OB_SUCC(ret)) {
        if (0 == strcmp(final_str, "True")) {
          task_info.is_final_ = true;
        } else {
          task_info.is_final_ = false;
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(task_infos.push_back(task_info))) {
          LOG_WARN("failed to push back task info", K(ret), K(task_info));
        }
      }
    }
  }
  return ret;
}

int ObLSBackupOperator::fill_backup_skipped_tablet_(const ObBackupSkippedTablet &task_info, share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  int turn_id = task_info.data_type_.is_minor_backup() ? task_info.turn_id_ : share::ObBackupSkipTabletAttr::BASE_MAJOR_TURN_ID + task_info.turn_id_;
  if (OB_FAIL(dml.add_pk_column("task_id", task_info.task_id_))) {
    LOG_WARN("failed to add pk column", K(task_info));
  } else if (OB_FAIL(dml.add_pk_column("turn_id", turn_id))) {
    LOG_WARN("failed to add pk column", K(task_info));
  } else if (OB_FAIL(dml.add_pk_column("retry_id", task_info.retry_id_))) {
    LOG_WARN("failed to add pk column", K(task_info));
  } else if (OB_FAIL(dml.add_pk_column("tablet_id", task_info.tablet_id_.id()))) {
    LOG_WARN("failed to add pk column", K(task_info));
  } else if (OB_FAIL(dml.add_column("ls_id", task_info.ls_id_.id()))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("backup_set_id", task_info.backup_set_id_))) {
    LOG_WARN("failed to add column", K(task_info));
  } else if (OB_FAIL(dml.add_column("skipped_type", task_info.skipped_type_.str()))) {
    LOG_WARN("failed to add column", K(task_info));
  }
  return ret;
}

int ObLSBackupOperator::get_distinct_ls_id_(const uint64_t tenant_id, const common::ObSqlString &sql,
    common::ObIArray<share::ObLSID> &ls_array, common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    common::sqlclient::ObMySQLResult *result = NULL;
    if (OB_FAIL(sql_client.read(res, gen_meta_tenant_id(tenant_id), sql.ptr()))) {
      LOG_WARN("failed to exec sql", K(ret), K(sql), K(tenant_id));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result set from read is NULL", K(ret));
    } else {/*do nothing*/}

    while (OB_SUCC(ret) && OB_SUCC(result->next())) {
      int64_t ls_id = 0;
      EXTRACT_INT_FIELD_MYSQL(*result, "ls_id", ls_id, int64_t);
      if (OB_FAIL(ls_array.push_back(ObLSID(ls_id)))) {
        LOG_WARN("failed to push back ls id", K(ret), K(ls_id));
      }
    }

    if (OB_LIKELY(OB_ITER_END == ret)) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObLSBackupOperator::get_piece_id_(const uint64_t tenant_id, const common::ObSqlString &sql,
    int64_t &piece_id, common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  ObArray<int64_t> piece_array;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    common::sqlclient::ObMySQLResult *result = NULL;
    if (OB_FAIL(sql_client.read(res, gen_meta_tenant_id(tenant_id), sql.ptr()))) {
      LOG_WARN("failed to exec sql", K(ret), K(sql), K(tenant_id));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result set from read is NULL", K(ret));
    } else {/*do nothing*/}

    while (OB_SUCC(ret) && OB_SUCC(result->next())) {
      int64_t piece_id = 0;
      EXTRACT_INT_FIELD_MYSQL(*result, "piece_id", piece_id, int64_t);
      if (OB_FAIL(piece_array.push_back(piece_id))) {
        LOG_WARN("failed to push back", K(piece_id), K(ret));
      }
    }
    if (OB_LIKELY(OB_ITER_END == ret)) {
      ret = OB_SUCCESS;
    }

    if (OB_SUCC(ret)) {
      if (piece_array.empty()) {
        ret = OB_ENTRY_NOT_EXIST;
        LOG_WARN("piece array empty", K(ret));
      } else if (piece_array.count() > 1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("piece array count not correct", K(ret), K(piece_array));
      } else {
        piece_id = piece_array.at(0);

      }
    }
  }
  return ret;
}

// TODO(yangyi.yyy): currently, __all_ls_log_archive_progress is not cleaned,
// fix later if __all_ls_log_archive_progress is cleaned
int ObLSBackupOperator::get_start_piece_id_(const uint64_t tenant_id, const uint64_t dest_id,
    const share::SCN &start_scn, common::ObISQLClient &sql_client, int64_t &start_piece_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::get_end_piece_id_(const uint64_t tenant_id, const uint64_t dest_id,
    const share::SCN &checkpoint_scn, common::ObISQLClient &sql_client, int64_t &end_piece_id)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::construct_query_backup_sql_(const uint64_t tenant_id, const int64_t task_id, common::ObSqlString &sql)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObLSBackupOperator::construct_query_archive_sql_(const uint64_t tenant_id, const int64_t dest_id,
    const int64_t start_piece_id, const int64_t end_piece_id, common::ObSqlString &sql)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

}  // namespace backup
}  // namespace oceanbase
