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
#include "share/backup/ob_archive_persist_helper.h"

using namespace oceanbase;
using namespace common;
using namespace share;
using namespace sqlclient;

/**
 * ------------------------------ObArchiveDestParaItem---------------------
 */
ObArchiveDestParaItem::ObArchiveDestParaItem(ObInnerKVItemValue *value)
  : ObInnerKVItem(value), dest_no_(-1)
{

}

int ObArchiveDestParaItem::set_dest_no(const int64_t dest_no)
{
  int ret = OB_SUCCESS;
  if (0 > dest_no) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid dest no", K(ret), K(dest_no));
  } else {
    dest_no_ = dest_no;
  }
  return ret;
}

int64_t ObArchiveDestParaItem::get_dest_no() const
{
  return dest_no_;
}

bool ObArchiveDestParaItem::is_pkey_valid() const
{
  return !name_.is_empty() && dest_no_ >= 0;
}

int ObArchiveDestParaItem::fill_pkey_dml(share::ObDMLSqlSplicer &dml) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dml.add_pk_column(OB_STR_DEST_NO, dest_no_))) {
    LOG_WARN("failed to add column", K(ret));
  } else if (OB_FAIL(dml.add_pk_column("name", name_.ptr()))) {
    LOG_WARN("failed to add column", K(ret));
  }

  return ret;
}

// Parse one full item from sql result, the result has full columns.
int ObArchiveDestParaItem::parse_from(sqlclient::ObMySQLResult &result)
{
  int ret = OB_SUCCESS;
  int64_t real_length = 0;
  char name_str[OB_INNER_TABLE_DEFAULT_KEY_LENTH] = {0};

  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_DEST_NO, dest_no_, int64_t);
  EXTRACT_STRBUF_FIELD_MYSQL(result, "name", name_str, OB_INNER_TABLE_DEFAULT_KEY_LENTH, real_length);

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(set_kv_name(name_str))) {
    LOG_WARN("failed to set name", K(ret), K(name_str));
  } else if (OB_FAIL(value_->parse_value_from(result))) {
    LOG_WARN("failed to parse value", K(ret), K(name_str));
  }

  return ret;
}



/**
 * ------------------------------ObArchivePersistHelper---------------------
 */
ObArchivePersistHelper::ObArchivePersistHelper()
  : is_inited_(false), tenant_id_(OB_INVALID_TENANT_ID)
{

}

uint64_t ObArchivePersistHelper::get_exec_tenant_id() const
{
  return gen_meta_tenant_id(tenant_id_);
}

int ObArchivePersistHelper::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObArchivePersistHelper init twice", K(ret));
  } else if (OB_INVALID_TENANT_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id", K(ret), K(tenant_id));
  } else {
    tenant_id_ = tenant_id;
    is_inited_ = true;
  }
  return ret;
}

int ObArchivePersistHelper::get_archive_mode(
    common::ObISQLClient &proxy, ObArchiveMode &mode) const
{
  int ret = OB_NOT_SUPPORTED;
//  int ret = OB_SUCCESS;
//  ObAllTenantInfo tenant_info;
//  const bool for_update = false;
//  if (IS_NOT_INIT) {
//    ret = OB_NOT_INIT;
//    LOG_WARN("ObArchivePersistHelper not init", K(ret));
//  } else if (OB_FAIL(ObAllTenantInfoProxy::load_tenant_info(tenant_id_, &proxy, for_update, tenant_info))) {
//    LOG_WARN("failed to get tenant info", K(ret), K_(tenant_id));
//  } else {
//    mode = tenant_info.get_log_mode();
//  }
  return ret;
}

int ObArchivePersistHelper::open_archive_mode(common::ObISQLClient &proxy) const
{
  int ret = OB_NOT_SUPPORTED;
//  int ret = OB_SUCCESS;
//  if (IS_NOT_INIT) {
//    ret = OB_NOT_INIT;
//    LOG_WARN("ObArchivePersistHelper not init", K(ret));
//  } else if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_log_mode(tenant_id_, &proxy, NOARCHIVE_MODE, ARCHIVE_MODE))) {
//    LOG_WARN("failed to open archive mode", K(ret), K_(tenant_id));
//  }
  return ret;
}

int ObArchivePersistHelper::close_archive_mode(common::ObISQLClient &proxy) const
{
  int ret = OB_NOT_SUPPORTED;
//  int ret = OB_SUCCESS;
//  if (IS_NOT_INIT) {
//    ret = OB_NOT_INIT;
//    LOG_WARN("ObArchivePersistHelper not init", K(ret));
//  } else if (OB_FAIL(ObAllTenantInfoProxy::update_tenant_log_mode(tenant_id_, &proxy, ARCHIVE_MODE, NOARCHIVE_MODE))) {
//    LOG_WARN("failed to close archive mode", K(ret), K_(tenant_id));
//  }
  return ret;
}

int ObArchivePersistHelper::lock_archive_dest(
    common::ObISQLClient &trans, 
    const int64_t dest_no,
    bool &is_exist) const
{
  int ret = OB_SUCCESS;
  ObBackupPathString path;
  if (OB_FAIL(get_archive_dest(trans, true /* need_lock */, dest_no, path))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      is_exist = false;
    } else {
      LOG_WARN("failed to get archive dest", K(ret), K(dest_no));
    }
  } else {
    is_exist = true;
  }

  return ret;
}


int ObArchivePersistHelper::get_archive_dest(
    common::ObISQLClient &proxy, const bool need_lock, const int64_t dest_no,
    ObBackupPathString &path) const
{
  int ret = OB_SUCCESS;
  common::ObSqlString value;
  const common::ObString str(OB_STR_PATH);
  ObLogArchiveDestAtrr dest_attr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObArchivePersistHelper not init", K(ret));
  } else if (OB_FAIL(get_string_value(proxy, dest_no, need_lock, str, value))) {
    LOG_WARN("fail to get string value", K(ret));
  } else if (OB_FAIL(path.assign(value.ptr()))) {
    LOG_WARN("fail to assign string value", K(ret), K(value));
  }
  return ret;
}


int ObArchivePersistHelper::get_dest_id(
    common::ObISQLClient &proxy, const bool need_lock, const int64_t dest_no,
    int64_t &dest_id) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObArchivePersistHelper::get_piece_switch_interval(
    common::ObISQLClient &proxy, const bool need_lock, const int64_t dest_no,
    int64_t &piece_switch_interval) const
{
  int ret = OB_SUCCESS;
  common::ObSqlString value;
  const common::ObString str(OB_STR_PIECE_SWITCH_INTERVAL);
  ObLogArchiveDestAtrr dest_attr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObArchivePersistHelper not init", K(ret));
  } else if (OB_FAIL(get_string_value(proxy, dest_no, need_lock, str, value))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      // not exist, use default, 1d.
      piece_switch_interval = OB_DEFAULT_PIECE_SWITCH_INTERVAL;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get string value", K(ret));
    }
  } else if (OB_FAIL(dest_attr.set_piece_switch_interval(value.ptr()))) {
    LOG_WARN("fail to set piece switch interval", K(ret), K(value));
  } else {
    piece_switch_interval = dest_attr.piece_switch_interval_;
  }
  return ret;
}

int ObArchivePersistHelper::get_binding(common::ObISQLClient &proxy, const bool need_lock, const int64_t dest_no,
      ObLogArchiveDestAtrr::Binding &binding) const
{
  int ret = OB_SUCCESS;
  common::ObSqlString value;
  const common::ObString str(OB_STR_BINDING);
  ObLogArchiveDestAtrr dest_attr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObArchivePersistHelper not init", K(ret));
  } else if (OB_FAIL(get_string_value(proxy, dest_no, need_lock, str, value))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      // not exist, use default, OPTIONAL.
      binding = ObLogArchiveDestAtrr::Binding::OPTIONAL;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to get string value", K(ret));
    }
  } else if (OB_FAIL(dest_attr.set_binding(value.ptr()))) {
    LOG_WARN("fail to set binding", K(ret), K(value));
  } else {
    binding = dest_attr.binding_;
  }
  return ret;
}

int ObArchivePersistHelper::get_dest_state(
    common::ObISQLClient &proxy, 
    const bool need_lock, 
    const int64_t dest_no,
    ObLogArchiveDestState &state) const
{
  int ret = OB_SUCCESS;
  common::ObSqlString value;
  const common::ObString str(OB_STR_STATE);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObArchivePersistHelper not init", K(ret));
  } else if (OB_FAIL(get_string_value(proxy, dest_no, need_lock, str, value))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("fail to get string value", K(ret));
    }
  } else if (OB_FAIL(state.set_state(value.ptr()))) {
    LOG_WARN("fail to set dest state", K(ret), K(value));
  }
  return ret;
}

int ObArchivePersistHelper::set_kv_item(common::ObISQLClient &proxy,
    const int64_t dest_no, const common::ObSqlString &name,
    const common::ObSqlString &value) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::del_dest(common::ObISQLClient &proxy, const int64_t dest_no)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("not supported", K(ret));
  return ret;
}

int ObArchivePersistHelper::set_dest_state(common::ObISQLClient &proxy, const int64_t dest_no,
    const ObLogArchiveDestState &state)
{
  int ret = OB_SUCCESS;
  common::ObSqlString name;
  common::ObSqlString value;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObArchivePersistHelper not init", K(ret));
  } else if (!state.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid state", K(ret), K(dest_no), K(state));
  } else if (OB_FAIL(name.assign(OB_STR_STATE))) {
    LOG_WARN("failed to assign str", K(ret));
  } else if (OB_FAIL(value.assign(state.get_str()))) {
    LOG_WARN("failed to assign state", K(ret), K(dest_no), K(state));
  } else if (OB_FAIL(set_kv_item(proxy, dest_no, name, value))) {
    LOG_WARN("failed to set state", K(ret), K(dest_no), K(state));
  }

  return ret;
}

int ObArchivePersistHelper::get_string_value(common::ObISQLClient &proxy,
    const int64_t dest_no, const bool need_lock,
    const common::ObString &name, common::ObSqlString &value) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_valid_dest_pairs(common::ObISQLClient &proxy,
    common::ObIArray<std::pair<int64_t, int64_t>> &pair_array) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_valid_dest_pairs(common::ObISQLClient &proxy,
    common::ObIArray<std::pair<int64_t, ObBackupPathString>> &pair_array) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObArchivePersistHelper::get_round(common::ObISQLClient &proxy, const int64_t dest_no,
    const bool need_lock, ObTenantArchiveRoundAttr &round) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_round_by_dest_id(common::ObISQLClient &proxy, const int64_t dest_id,
      const bool need_lock, ObTenantArchiveRoundAttr &round) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_round_stopping_ts(
    common::ObISQLClient &proxy, const int64_t dest_no, int64_t &ts) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::del_round(common::ObISQLClient &proxy, const int64_t dest_no) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::start_new_round(common::ObISQLClient &proxy, const ObTenantArchiveRoundAttr &new_round) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::stop_round(common::ObISQLClient &proxy, const ObTenantArchiveRoundAttr &old_round,
    const ObTenantArchiveRoundAttr &new_round) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObArchivePersistHelper not init", K(ret));
  } else if (!new_round.state_.is_stop()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid round state", K(ret), K(new_round));
  } else if (OB_FAIL(switch_round_state_to(proxy, old_round, new_round))) {
    LOG_WARN("failed to stop round", K(ret), K(old_round), K(new_round));
  }

  return ret;
}

int ObArchivePersistHelper::switch_round_state_to(common::ObISQLClient &proxy, const ObTenantArchiveRoundAttr &round,
    const ObArchiveRoundState &new_state) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::switch_round_state_to(common::ObISQLClient &proxy, const ObTenantArchiveRoundAttr &old_round,
    const ObTenantArchiveRoundAttr &new_round) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_all_active_rounds(common::ObISQLClient &proxy,
    common::ObIArray<ObTenantArchiveRoundAttr> &rounds) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_his_round(common::ObISQLClient &proxy, const int64_t dest_no,
    const int64_t round_id, ObTenantArchiveHisRoundAttr &his_round) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::insert_his_round(common::ObISQLClient &proxy, const ObTenantArchiveHisRoundAttr &his_round) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObArchivePersistHelper::get_piece(common::ObISQLClient &proxy, const int64_t dest_id,
    const int64_t round_id, const int64_t piece_id, const bool need_lock, ObTenantArchivePieceAttr &piece) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_piece(common::ObISQLClient &proxy, const int64_t dest_id,
      const int64_t piece_id, const bool need_lock, ObTenantArchivePieceAttr &piece) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_pieces(
    common::ObISQLClient &proxy,
    const int64_t dest_id,
    common::ObIArray<ObTenantArchivePieceAttr> &piece_list) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_frozen_pieces(
    common::ObISQLClient &proxy,
    const int64_t dest_id,
    const int64_t upper_piece_id,
    common::ObIArray<ObTenantArchivePieceAttr> &piece_list) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_candidate_obsolete_backup_pieces(common::ObISQLClient &proxy, const SCN &end_scn,
    const char *backup_dest_str, ObIArray<ObTenantArchivePieceAttr> &pieces) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::insert_or_update_piece(common::ObISQLClient &proxy, const ObTenantArchivePieceAttr &piece) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::batch_update_pieces(common::ObISQLClient &proxy, const common::ObIArray<ObTenantArchivePieceAttr> &pieces_array) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObArchivePersistHelper not init", K(ret));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < pieces_array.count(); i++) {
    const ObTenantArchivePieceAttr &piece = pieces_array.at(i);
    if (OB_FAIL(insert_or_update_piece(proxy, piece))) {
      LOG_WARN("insert or update piece failed", K(ret), K(piece), K(pieces_array));
    }
  }

  return ret;
}

int ObArchivePersistHelper::mark_new_piece_file_status(common::ObISQLClient &proxy, const int64_t dest_id,
    const int64_t round_id, const int64_t piece_id, const ObBackupFileStatus::STATUS new_status) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObArchivePersistHelper::get_latest_ls_archive_progress(common::ObISQLClient &proxy, const int64_t dest_id, const int64_t round_id,
    const ObLSID &id, ObLSArchivePersistInfo &info, bool &record_exist) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::insert_ls_archive_progress(common::ObISQLClient &proxy,
    const ObLSArchivePersistInfo &info, int64_t &affected_rows) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::set_ls_archive_stop(common::ObISQLClient &proxy, const int64_t dest_id, const int64_t round_id,
    const ObLSID &id, int64_t &affected_rows) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::set_ls_archive_suspend(common::ObISQLClient &proxy, const int64_t dest_id, const int64_t round_id,
    const ObLSID &id, int64_t &affected_rows) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::set_ls_archive_doing(common::ObISQLClient &proxy, const int64_t dest_id, const int64_t round_id,
    const ObLSID &id, int64_t &affected_rows) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_dest_round_summary(common::ObISQLClient &proxy, const int64_t dest_id,
    const int64_t round_id, const int64_t since_piece_id, ObDestRoundSummary &summary) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_piece_by_scn(common::ObISQLClient &proxy, const int64_t dest_id,
    const share::SCN &scn, ObTenantArchivePieceAttr &piece) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObArchivePersistHelper::get_pieces_by_range(common::ObISQLClient &proxy, const int64_t dest_id,
    const int64_t start_piece_id, const int64_t end_piece_id, ObIArray<ObTenantArchivePieceAttr> &pieces) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}


int ObArchivePersistHelper::parse_round_result_(sqlclient::ObMySQLResult &result, common::ObIArray<ObTenantArchiveRoundAttr> &rounds) const
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObTenantArchiveRoundAttr round;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to get next row", K(ret));
      }
    } else if (OB_FAIL(round.parse_from(result))) {
      LOG_WARN("failed to parse round result", K(ret));
    } else if (OB_FAIL(rounds.push_back(round))) {
      LOG_WARN("failed to push back round", K(ret));
    }
  }

  return ret;
}

int ObArchivePersistHelper::parse_piece_result_(sqlclient::ObMySQLResult &result, common::ObIArray<ObTenantArchivePieceAttr> &pieces) const
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObTenantArchivePieceAttr piece;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to get next row", K(ret));
      }
    } else if (OB_FAIL(piece.parse_from(result))) {
      LOG_WARN("failed to parse piece result", K(ret));
    } else if (OB_FAIL(pieces.push_back(piece))) {
      LOG_WARN("failed to push back piece", K(ret));
    }
  }

  return ret;
}


int ObArchivePersistHelper::parse_dest_round_summary_result_(sqlclient::ObMySQLResult &result, ObDestRoundSummary &summary) const
{
  int ret = OB_SUCCESS;
  ObLSDestRoundSummary ls_dest_round_summary;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    ObArchiveLSPieceSummary piece;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_ls_archive_piece_summary_result_(result, piece))) {
      LOG_WARN("failed to parse result", K(ret));
    } else if (ls_dest_round_summary.ls_id_ != piece.ls_id_ && ls_dest_round_summary.is_valid()) {
      if (OB_FAIL(summary.add_ls_dest_round_summary(ls_dest_round_summary))) {
        LOG_WARN("failed to push back ls_dest_round_summary", K(ret));
      } else {
        ls_dest_round_summary.reset();
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ls_dest_round_summary.add_one_piece(piece))) {
      LOG_WARN("failed to add one piece", K(ret));
    }
  }

  // push backup last log stream
  if (OB_FAIL(ret)) {
  } else if (!ls_dest_round_summary.is_valid()) {
  } else if (OB_FAIL(summary.add_ls_dest_round_summary(ls_dest_round_summary))) {
    LOG_WARN("failed to push back ls_dest_round_summary", K(ret));
  }

  return ret;
}

int ObArchivePersistHelper::do_parse_ls_archive_piece_summary_result_(sqlclient::ObMySQLResult &result, ObArchiveLSPieceSummary &piece) const
{
  int ret = OB_SUCCESS;
  int64_t ls_id = 0;
  int64_t ls_id_bak = 0;
  int64_t real_length = 0;
  uint64_t start_scn = 0;
  uint64_t checkpoint_scn = 0;
  char status_str[OB_DEFAULT_STATUS_LENTH] = "";
  
  EXTRACT_INT_FIELD_MYSQL(result, "ls_id", ls_id, int64_t);
  if (OB_SUCC(ret)) {
    // log stream is in table __all_ls_log_archive_progress.
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_TENANT_ID, piece.tenant_id_, uint64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_DEST_ID, piece.dest_id_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_ROUND_ID, piece.round_id_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_PIECE_ID, piece.piece_id_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INCARNATION, piece.incarnation_, int64_t);
    EXTRACT_UINT_FIELD_MYSQL(result, OB_STR_MIN_LSN, piece.min_lsn_, uint64_t);
    EXTRACT_UINT_FIELD_MYSQL(result, OB_STR_START_SCN, start_scn, uint64_t);
    EXTRACT_UINT_FIELD_MYSQL(result, OB_STR_MAX_LSN, piece.max_lsn_, uint64_t);
    EXTRACT_UINT_FIELD_MYSQL(result, OB_STR_CHECKPOINT_SCN, checkpoint_scn, uint64_t);
    EXTRACT_STRBUF_FIELD_MYSQL(result, OB_STR_STATUS, status_str, OB_DEFAULT_STATUS_LENTH, real_length);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_INPUT_BYTES, piece.input_bytes_, int64_t);
    EXTRACT_INT_FIELD_MYSQL(result, OB_STR_OUTPUT_BYTES, piece.output_bytes_, int64_t);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(piece.state_.set_status(status_str))) {
      LOG_WARN("failed to set status", K(ret), K(status_str));
    } else if (OB_FAIL(piece.start_scn_.convert_for_inner_table_field(start_scn))) {
      LOG_WARN("failed to set start scn", K(ret), K(start_scn));
    } else if (OB_FAIL(piece.checkpoint_scn_.convert_for_inner_table_field(checkpoint_scn))) {
      LOG_WARN("failed to set checkpoint scn", K(ret), K(checkpoint_scn));
    } else {
      piece.ls_id_ = ObLSID(ls_id);
      piece.is_archiving_ = true;

      // but not exist in table __all_ls_status, must be deleted.
      EXTRACT_INT_FIELD_MYSQL(result, "ls_id_bak", ls_id_bak, int64_t);
      if (OB_SUCC(ret)) {
        piece.is_deleted_ = false;
      } else if (OB_ERR_NULL_VALUE == ret) {
        piece.is_deleted_ = true;
        ret = OB_SUCCESS;
      }
    }
  } else if (OB_ERR_NULL_VALUE == ret) {
    ret = OB_SUCCESS;
    // log stream is not in table __all_ls_log_archive_progress, but exist in __all_ls_status,
    // must has not been archived.
    EXTRACT_INT_FIELD_MYSQL(result, "ls_id_bak", ls_id_bak, int64_t);
    if (OB_FAIL(ret)) {
    } else {
      piece.tenant_id_ = tenant_id_;
      piece.ls_id_ = ObLSID(ls_id_bak);
      piece.is_archiving_ = false;
      piece.is_deleted_ = false;
      piece.dest_id_ = 0;
      piece.round_id_ = 0;
      piece.piece_id_ = 0;
      piece.incarnation_ = 0;
      piece.state_.set_invalid();
      piece.start_scn_ = SCN::min_scn();
      piece.checkpoint_scn_ = SCN::min_scn();
      piece.min_lsn_ = 0;
      piece.max_lsn_ = 0;
      piece.input_bytes_ = 0;
      piece.output_bytes_ = 0;
      LOG_INFO("encounter a log stream not started.", K(ret), K(piece));
    }
  }

  return ret;
}

int ObArchivePersistHelper::parse_dest_pair_result_(sqlclient::ObMySQLResult &result, common::ObIArray<std::pair<int64_t, int64_t>> &pair_array) const
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    std::pair<int64_t, int64_t> pair;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_dest_pair_(result, pair))) {
      LOG_WARN("failed to parse dest pair", K(ret));
    } else if (OB_FAIL(pair_array.push_back(pair))) {
      LOG_WARN("failed to push back dest pair", K(ret));
    }
  }

  return ret;
}

int ObArchivePersistHelper::parse_dest_pair_result_(sqlclient::ObMySQLResult &result, common::ObIArray<std::pair<int64_t, ObBackupPathString>> &pair_array) const
{
  int ret = OB_SUCCESS;
  // traverse each returned row
  while (OB_SUCC(ret)) {
    std::pair<int64_t, ObBackupPathString> pair;
    if (OB_FAIL(result.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      } else {
        LOG_WARN("failed to get next row", K(ret));
      }
    } else if (OB_FAIL(do_parse_dest_pair_(result, pair))) {
      LOG_WARN("failed to parse dest pair", K(ret));
    } else if (OB_FAIL(pair_array.push_back(pair))) {
      LOG_WARN("failed to push back dest pair", K(ret));
    }
  }

  return ret;
}

int ObArchivePersistHelper::do_parse_dest_pair_(sqlclient::ObMySQLResult &result, std::pair<int64_t, int64_t> &pair) const
{
  int ret = OB_SUCCESS;
  int64_t real_length = 0;
  char value[OB_INNER_TABLE_DEFAULT_VALUE_LENTH] = {0};

  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_DEST_NO, pair.first, int64_t);
  EXTRACT_STRBUF_FIELD_MYSQL(result, "value", value, OB_INNER_TABLE_DEFAULT_VALUE_LENTH, real_length);
  if (OB_SUCC(ret) && OB_FAIL(ob_atoll(value, pair.second))) {
    LOG_WARN("atoll failed", K(ret), K(value));
  }

  return ret;
}

int ObArchivePersistHelper::do_parse_dest_pair_(sqlclient::ObMySQLResult &result, std::pair<int64_t, ObBackupPathString> &pair) const
{
  int ret = OB_SUCCESS;
  int64_t real_length = 0;

  EXTRACT_INT_FIELD_MYSQL(result, OB_STR_DEST_NO, pair.first, int64_t);
  EXTRACT_STRBUF_FIELD_MYSQL(result, "value", pair.second.ptr(), pair.second.capacity(), real_length);

  return ret;
}


int ObArchivePersistHelper::clean_round_comment(common::ObISQLClient &proxy, const int64_t dest_no) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}
