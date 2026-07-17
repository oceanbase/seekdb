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

#include "ob_i_store.h"
#include "storage/access/ob_table_param.h"  // ObColumnParam complete type(needed by relocated functions)
#include "share/rc/ob_module_provider.h"
#include "storage/tx/ob_trans_part_ctx.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
using namespace transaction;
using namespace share;
namespace storage
{
using namespace common;

int ObMultiVersionRowkeyHelpper::add_extra_rowkey_cols(ObColDescIArray &store_out_cols)
{
  int ret = OB_SUCCESS;
  share::schema::ObColDesc desc;
  for (int64_t i = 0; OB_SUCC(ret) && i < get_extra_rowkey_col_cnt(); ++i) {
    const ObMultiVersionExtraRowkey &mv_ext_rowkey = OB_MULTI_VERSION_EXTRA_ROWKEY[i];
    desc.col_id_ = mv_ext_rowkey.column_index_;
    (desc.col_type_.*mv_ext_rowkey.set_meta_type_func_)();
    // by default the trans_version column value would be multiplied by -1
    // so in effect we store the latest version first
    desc.col_order_ = ObOrderType::ASC;
    if (OB_FAIL(store_out_cols.push_back(desc))) {
      STORAGE_LOG(WARN, "add store utput columns failed", K(ret));
    }
  }
  return ret;
}

void ObStoreCtx::reset()
{
  ls_id_.reset();
  ls_ = nullptr;
  branch_ = 0;
  tablet_id_.reset();
  table_iter_ = nullptr;
  table_version_ = INT64_MAX;
  timeout_ = 0;
  mvcc_acc_ctx_.reset();
  tablet_stat_.reset();
  is_read_store_ctx_ = false;
  update_full_column_ = false;
  is_fork_ctx_ = false;
  fork_snapshot_map_.destroy();
  fork_snapshot_map_inited_ = false;
  check_seq_ = 0;
}

int ObStoreCtx::init_for_read(const ObLSID &ls_id,
                              const common::ObTabletID tablet_id,
                              const int64_t timeout,
                              const int64_t tx_lock_timeout,
                              const SCN &snapshot_version)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_svr = share::g_mp->ls_service();
  ObLSHandle ls_handle;
  if (OB_FAIL(ls_svr->get_ls(ls_id, ls_handle, ObLSGetMod::STORAGE_MOD))) {
    STORAGE_LOG(WARN, "get_ls from ls service fail.", K(ret), K(*ls_svr));
  } else {
    tablet_id_ = tablet_id;
    ret = init_for_read(ls_handle, timeout, tx_lock_timeout, snapshot_version);
  }
  return ret;
}

int ObStoreCtx::init_for_read(const ObLSHandle &ls_handle,
                              const int64_t timeout,
                              const int64_t tx_lock_timeout,
                              const SCN &snapshot_version)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTxTable *tx_table = nullptr;
  if (!ls_handle.is_valid() || timeout < 0 || !snapshot_version.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "get invalid arguments", K(ret), K(ls_handle), K(timeout), K(tx_lock_timeout), K(snapshot_version));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "ls is null", K(ret), K(ls_id_));
  } else if (OB_ISNULL(tx_table = ls->get_tx_table())) {
    ret = OB_ERR_NULL_VALUE;
    STORAGE_LOG(WARN, "get_tx_table from log stream fail.", K(ret), K(*ls));
  } else if (OB_FAIL(mvcc_acc_ctx_.init_read(tx_table, snapshot_version, timeout, tx_lock_timeout))) {
    STORAGE_LOG(WARN, "mvcc_acc_ctx init read fail", KR(ret), K(mvcc_acc_ctx_));
  } else {
    ls_id_ = ls->get_ls_id();
    timeout_ = timeout;
  }
  return ret;
}

void ObStoreCtx::force_print_trace_log()
{
  if (NULL != mvcc_acc_ctx_.tx_desc_) {
    mvcc_acc_ctx_.tx_desc_->print_trace();
  }
  if (NULL != mvcc_acc_ctx_.tx_ctx_) {
    mvcc_acc_ctx_.tx_ctx_->print_trace_log();
  }
}

bool ObStoreCtx::is_uncommitted_data_rollbacked() const
{
  bool bret = false;
  if (NULL != mvcc_acc_ctx_.tx_ctx_) {
    bret = mvcc_acc_ctx_.tx_ctx_->is_data_rollbacked();
  }
  return bret;
}

int ObStoreCtx::enter_fork_snapshot(const share::SCN &fork_snapshot_scn,
                                    share::SCN &saved_snapshot_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!fork_snapshot_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid fork snapshot scn", K(ret), K(fork_snapshot_scn));
  } else {
    saved_snapshot_version = mvcc_acc_ctx_.snapshot_.version_;
    is_fork_ctx_ = true;
    mvcc_acc_ctx_.is_fork_ctx_ = true;  // sync to mvcc_acc_ctx for internal use
    mvcc_acc_ctx_.snapshot_.version_ = fork_snapshot_scn;
  }
  return ret;
}

void ObStoreCtx::exit_fork_snapshot(const share::SCN &saved_snapshot_version)
{
  mvcc_acc_ctx_.snapshot_.version_ = saved_snapshot_version;
  is_fork_ctx_ = false;
  mvcc_acc_ctx_.is_fork_ctx_ = false;  // sync to mvcc_acc_ctx
}

ObStoreCtxForkGuard::ObStoreCtxForkGuard(ObStoreCtx &ctx)
    : ctx_(ctx), saved_snapshot_version_(), opened_(false)
{}

ObStoreCtxForkGuard::~ObStoreCtxForkGuard()
{
  if (opened_) {
    ctx_.exit_fork_snapshot(saved_snapshot_version_);
  }
}

int ObStoreCtxForkGuard::enter_fork_snapshot(const share::SCN &fork_snapshot_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(opened_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "fork snapshot entered twice", K(ret));
  } else if (OB_FAIL(ctx_.enter_fork_snapshot(fork_snapshot_scn,
                                              saved_snapshot_version_))) {
    STORAGE_LOG(WARN, "enter fork snapshot failed", K(ret), K(fork_snapshot_scn));
  } else {
    opened_ = true;
  }
  return ret;
}

int ObStoreCtx::get_all_tables(ObIArray<ObITable *> &iter_tables)
{
  int ret = OB_SUCCESS;
  table_iter_->resume();
  while (OB_SUCC(ret)) {
    ObITable *table_ptr = nullptr;
    if (OB_FAIL(table_iter_->get_next(table_ptr))) {
      if (OB_ITER_END != ret) {
        TRANS_LOG(WARN, "failed to get next tables", K(ret));
      } else {
        ret = OB_SUCCESS;
        break;
      }
    } else if (OB_ISNULL(table_ptr)) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "table must not be null", K(ret), KPC(table_iter_));
    } else if (OB_FAIL(iter_tables.push_back(table_ptr))) {
      TRANS_LOG(WARN, "rowkey_exists check::", K(ret), KPC(table_ptr));
    }
  }
  return ret;
}

int ObStoreCtx::get_fork_snapshot_scn(const common::ObTabletID &tablet_id,
                                      share::SCN &fork_snapshot_scn)
{
  int ret = OB_SUCCESS;
  fork_snapshot_scn.reset();
  if (OB_ISNULL(table_iter_)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "table iter is null when get fork snapshot", K(ret));
  } else if (!fork_snapshot_map_inited_) {
    const ObIArray<share::ObForkTabletInfo> *fork_infos = table_iter_->get_fork_infos();
    if (OB_ISNULL(fork_infos) || fork_infos->empty()) {
      fork_snapshot_map_inited_ = true;
    } else if (OB_FAIL(fork_snapshot_map_.create(fork_infos->count() * 2, "ForkSnapMap"))) {
      STORAGE_LOG(WARN, "failed to create fork snapshot map", K(ret), K(fork_infos->count()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < fork_infos->count(); ++i) {
        const share::ObForkTabletInfo &fork_info = fork_infos->at(i);
        share::SCN fork_snapshot_scn;
        if (OB_FAIL(fork_snapshot_scn.convert_for_tx(fork_info.get_fork_snapshot_version()))) {
          STORAGE_LOG(WARN, "failed to convert fork snapshot version", K(ret), K(fork_info));
        } else if (OB_FAIL(fork_snapshot_map_.set_refactored(
                     fork_info.get_fork_src_tablet_id(), fork_snapshot_scn, true))) {
          STORAGE_LOG(WARN, "failed to set fork snapshot map", K(ret), K(fork_info));
        }
      }
      if (OB_SUCC(ret)) {
        fork_snapshot_map_inited_ = true;
      } else {
        fork_snapshot_map_.destroy();
      }
    }
  }

  if (OB_SUCC(ret) && fork_snapshot_map_.created()) {
    int tmp_ret = fork_snapshot_map_.get_refactored(tablet_id, fork_snapshot_scn);
    if (OB_HASH_NOT_EXIST == tmp_ret) {
      tmp_ret = OB_SUCCESS;
    } else if (OB_SUCCESS != tmp_ret) {
      ret = tmp_ret;
      STORAGE_LOG(WARN, "failed to get fork snapshot scn", K(ret), K(tablet_id));
    }
  }
  return ret;
}

void ObStoreRowLockState::reset()
{
  is_locked_ = false;
  trans_version_ = SCN::min_scn();
  lock_trans_id_.reset();
  lock_data_sequence_.reset();
  lock_dml_flag_ = blocksstable::ObDmlFlag::DF_NOT_EXIST;
  is_delayed_cleanout_ = false;
  mvcc_row_ = NULL;
  trans_scn_ = SCN::max_scn();
}

OB_DEF_SERIALIZE(ObStoreRow)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              row_val_,
              flag_,
              is_get_,
              scan_index_,
              from_base_,
              row_type_flag_.flag_,
              is_sparse_row_);
  if (OB_SUCC(ret) && is_sparse_row_) {
    if (OB_ISNULL(column_ids_)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "sparse row's column id is null", K(ret));
    } else {
      OB_UNIS_ENCODE_ARRAY(column_ids_, row_val_.count_);
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObStoreRow)
{
  int ret = OB_SUCCESS;
  int col_id_count = 0;
  LST_DO_CODE(OB_UNIS_DECODE,
              row_val_,
              flag_,
              is_get_,
              scan_index_,
              from_base_,
              row_type_flag_.flag_,
              is_sparse_row_);
  if (OB_SUCC(ret) && is_sparse_row_) {
    OB_UNIS_DECODE(col_id_count);
    if (OB_SUCC(ret)) {
      if (row_val_.count_ != col_id_count) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "column id count is not equal", K(ret), K(col_id_count),
            K(row_val_.count_));
      } else {
        OB_UNIS_DECODE_ARRAY(column_ids_, row_val_.count_);
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObStoreRow)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              row_val_,
              flag_,
              is_get_,
              scan_index_,
              from_base_,
              row_type_flag_.flag_,
              is_sparse_row_);
  if (is_sparse_row_) {
    OB_UNIS_ADD_LEN_ARRAY(column_ids_, row_val_.count_);
  }
  return len;
}

void ObStoreRow::reset()
{
  row_val_.reset();
  flag_.reset();
  capacity_ = 0;
  is_get_ = false;
  from_base_ = false;
  scan_index_ = 0;
  row_type_flag_.reset();
  is_sparse_row_ = false;
  column_ids_ = NULL;
  snapshot_version_ = 0;
  group_idx_ = 0;
  trans_id_.reset();
  fast_filter_skipped_ = false;
  last_purge_ts_ = 0;
}


int64_t ObStoreRow::to_string(char *buffer, const int64_t length) const
{
  int64_t pos = 0;

  if (NULL != buffer && length >= 0) {

    pos += flag_.to_string(buffer + pos, length - pos);
    common::databuff_printf(buffer, length, pos, " capacity_=%ld ", capacity_);
    common::databuff_printf(buffer, length, pos, "is_get=%d ", is_get_);
    common::databuff_printf(buffer, length, pos, "from_base=%d ", from_base_);
    common::databuff_printf(buffer, length, pos, "trans_id=%ld ", trans_id_.hash());
    common::databuff_printf(buffer, length, pos, "scan_index=%ld ", scan_index_);
    common::databuff_printf(buffer, length, pos, "multi_version_row_flag=%d ", row_type_flag_.flag_);
    pos += row_type_flag_.to_string(buffer + pos, length - pos);
    common::databuff_printf(buffer, length, pos, " row_val={count=%ld,", row_val_.count_);
    common::databuff_printf(buffer, length, pos, "cells=[");
    if (NULL != row_val_.cells_) {
      for (int64_t i = 0; i < row_val_.count_; ++i) {
        common::databuff_print_obj(buffer, length, pos, row_val_.cells_[i]);
        common::databuff_printf(buffer, length, pos, ",");
      }
    }
    common::databuff_printf(buffer, length, pos, "] ");
    common::databuff_printf(buffer, length, pos, "is_sparse_row=%d ", is_sparse_row_);
    common::databuff_printf(buffer, length, pos, "snapshot_version=%ld ", snapshot_version_);
    common::databuff_printf(buffer, length, pos, "fast_filtered=%d ", fast_filter_skipped_);
    common::databuff_printf(buffer, length, pos, "group_idx_=%ld ", group_idx_);
    common::databuff_printf(buffer, length, pos, "last_purge_ts=%ld ", last_purge_ts_);
    if (is_sparse_row_ && NULL != column_ids_) {
      common::databuff_printf(buffer, length, pos, "column_id=[");
      for (int64_t i = 0; i < row_val_.count_; ++i) {
        common::databuff_printf(buffer, length, pos, "%u,", column_ids_[i]);
      }
      common::databuff_printf(buffer, length, pos, "] ");
    }
  }
  return pos;
}

int ObLockRowChecker::check_lock_row_valid(
    const ObDatumRow &row,
    const int64_t rowkey_cnt,
    bool is_memtable_iter_row_check)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row.get_column_count() < rowkey_cnt)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "row count is less than rowkey count", K(row), K(rowkey_cnt));
  } else if (row.is_uncommitted_row() || is_memtable_iter_row_check) {
    bool pure_empty_row = true;
    for (int i = rowkey_cnt; pure_empty_row && i < row.get_column_count(); ++i) {
      if (!row.storage_datums_[i].is_nop()) { // not nop value
        pure_empty_row = false;
      }
    }
    if (row.is_uncommitted_row()) {
      if (!pure_empty_row) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "uncommitted lock row have normal cells", K(ret), K(row), K(rowkey_cnt));
      }
    } else if (is_memtable_iter_row_check && pure_empty_row) { // a pure lock committed row from memtable
      // a pure empty lock row from upgrade sstable need to be compatible
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "a committed lock row only have rowkey", K(ret), K(row), K(rowkey_cnt));
    }
  }
  return ret;
}

int ObLockRowChecker::check_lock_row_valid(const blocksstable::ObDatumRow &row, const ObITableReadInfo &read_info)
{
  int ret = OB_SUCCESS;
  int64_t rowkey_read_cnt = MIN(read_info.get_seq_read_column_count(), read_info.get_rowkey_count());
  if (OB_UNLIKELY(!read_info.is_valid() || row.get_column_count() < rowkey_read_cnt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(read_info), K(row));
  } else if (row.is_uncommitted_row()) {
    const ObColumnIndexArray &col_index = read_info.get_columns_index();
    for (int i = rowkey_read_cnt; i < row.get_column_count(); ++i) {
      if (col_index.at(i) < read_info.get_rowkey_count()) {
        // not checking rowkey col
      } else if (!row.storage_datums_[i].is_nop()) { // not nop value
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "uncommitted lock row have normal cells", K(ret), K(row),
          K(rowkey_read_cnt), K(read_info));
        break;
      }
    }
  }
  return ret;
}

}
}

// definition moved from share/schema/ob_table_schema.cpp: uses ObMultiVersionRowkeyHelpper, share must not depend upward on storage
namespace oceanbase
{
namespace share
{
namespace schema
{
int ObMergeSchema::get_mulit_version_rowkey_column_ids(common::ObIArray<share::schema::ObColDesc> &column_ids) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_rowkey_column_ids(column_ids))) {
    SHARE_SCHEMA_LOG(WARN, "failed to add rowkey cols", K(ret));
  } else if (OB_FAIL(storage::ObMultiVersionRowkeyHelpper::add_extra_rowkey_cols(column_ids))) {
    SHARE_SCHEMA_LOG(WARN, "failed to add extra rowkey cols", K(ret));
  } else if (OB_FAIL(set_precision_to_column_desc(column_ids))) {
    SHARE_SCHEMA_LOG(WARN, "failed to set precision to cols", K(ret));
  }
  return ret;
}
}  // namespace schema
}  // namespace share
}  // namespace oceanbase

// definition moved from share/schema/ob_table_schema.cpp: accesses blocksstable typed members, share must not depend upward on storage
namespace oceanbase
{
namespace share
{
namespace schema
{
using blocksstable::ObSSTableColumnMeta;
// get_orig_default_row has been demoted from ObTableSchema to storage free function(see end of file namespace storage), removes cross-module splitting of class members

int ObTableSchema::init_column_meta_array(
    common::ObIArray<blocksstable::ObSSTableColumnMeta> &meta_array) const
{
  int ret = OB_SUCCESS;
  ObArray<ObColDesc> columns;
  ObSSTableColumnMeta col_meta;
  if (OB_FAIL(get_multi_version_column_descs(columns))) {
    STORAGE_LOG(WARN, "fail to get store column ids", K(ret));
  } else {
    blocksstable::ObStorageDatum datum;
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
      const uint64_t col_id = columns.at(i).col_id_;
      col_meta.column_id_ = col_id;
      col_meta.column_checksum_ = 0;

      if (common::OB_HIDDEN_TRANS_VERSION_COLUMN_ID == col_id ||
          common::OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID == col_id) {
        col_meta.column_default_checksum_ = 0;
      } else {
        const ObColumnSchemaV2 *col_schema = get_column_schema(col_id);
        if (OB_ISNULL(col_schema)) {
          ret = OB_ERR_SYS;
          STORAGE_LOG(ERROR, "col_schema must not null", K(ret), K(col_id));
        } else if (!col_schema->is_valid()) {
          ret = OB_ERR_SYS;
          STORAGE_LOG(ERROR, "invalid col schema", K(ret), K(col_schema));
        } else if (!col_schema->is_column_stored_in_sstable()
                  && !is_storage_index_table()) {
          ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(WARN, "virtual generated column should be filtered already", K(ret), K(col_schema));
        } else {
          if (ob_is_large_text(col_schema->get_data_type())) {
            col_meta.column_default_checksum_ = 0;
          } else if (OB_FAIL(datum.from_obj_enhance(col_schema->get_orig_default_value()))) {
            STORAGE_LOG(WARN, "Failed to transefer obj to datum", K(ret));
          } else {
            col_meta.column_default_checksum_ = datum.checksum(0);
          }
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(meta_array.push_back(col_meta))) {
        STORAGE_LOG(WARN, "Fail to push column meta", K(ret));
      }
    } // end for
  }
  return ret;
}
}  // namespace schema
}  // namespace share
}  // namespace oceanbase

namespace oceanbase
{
namespace share
{
namespace schema
{
int ObTableSchema::get_column_group_index(
    const share::schema::ObColumnParam &param,
    const bool need_calculate_cg_idx,
    int32_t &cg_idx) const
{
  int ret = OB_SUCCESS;
  const uint64_t column_id = param.get_column_id();
  cg_idx = -1;
  if (OB_UNLIKELY(1 >= column_group_cnt_ && !need_calculate_cg_idx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("No column group exist", K(ret), K(need_calculate_cg_idx), K_(is_column_store_supported), K_(column_group_cnt));
  } else if (param.is_virtual_gen_col()) {
    cg_idx = -1;
  } else if ((column_id < OB_END_RESERVED_COLUMN_ID_NUM || common::OB_MAJOR_REFRESH_MVIEW_OLD_NEW_COLUMN_ID == column_id) &&
      common::OB_HIDDEN_SESS_CREATE_TIME_COLUMN_ID != column_id &&
      common::OB_HIDDEN_SESSION_ID_COLUMN_ID != column_id &&
      common::OB_HIDDEN_PK_INCREMENT_COLUMN_ID != column_id) { // this has its own column group now
    if (common::OB_HIDDEN_TRANS_VERSION_COLUMN_ID == column_id ||
        common::OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID == column_id) {
      if (need_calculate_cg_idx) {
        cg_idx = OB_CS_COLUMN_REPLICA_ROWKEY_CG_IDX;
      } else if (OB_FAIL(get_base_rowkey_column_group_index(cg_idx))) {
        LOG_WARN("Fail to get base/rowkey column group index", K(ret), K(column_id));
      }
    } else {
      // TODO: check the following
      // TODO: after check, also see ObStorageSchema::get_column_group_index
      // common::OB_HIDDEN_GROUP_IDX_COLUMN_ID == column_id
      cg_idx = -1;
    }
  } else if (need_calculate_cg_idx) {
    if (OB_FAIL(calc_column_group_index_(column_id, cg_idx))) {
      LOG_WARN("Fail to calc_column_group_index", K(ret), K(column_id));
    }
  } else {
    bool found = false;
    int64_t cg_column_cnt = 0;
    int32_t iter_cg_idx = 0;
    uint64_t *cg_column_ids = nullptr;
    for (int64_t i = 0; OB_SUCC(ret) && !found && i < column_group_cnt_; i++) {
      if (OB_ISNULL(column_group_arr_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column_group should not be null", K(ret), K(i), K_(column_group_cnt));
      } else if (FALSE_IT(cg_column_cnt = column_group_arr_[i]->get_column_id_count())) {
      } else if (0 == cg_column_cnt) {
        if (column_group_arr_[i]->get_column_group_type() != DEFAULT_COLUMN_GROUP) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Unexpected column group type", K(ret), KPC(column_group_arr_[i]));
        }
      } else if (1 < cg_column_cnt || column_group_arr_[i]->get_column_group_type() != ObColumnGroupType::SINGLE_COLUMN_GROUP) {
        iter_cg_idx++;
        // ignore column group with more than one column or not each column group cg
      } else if (OB_ISNULL(cg_column_ids = column_group_arr_[i]->get_column_ids())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected error for null column ids", K(ret), KPC(column_group_arr_[i]));
      } else if (cg_column_ids[0] != column_id) {
        iter_cg_idx++;
      } else {
        cg_idx = iter_cg_idx;
        found = true;
      }
    }

    if (OB_SUCC(ret) && !found) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected, can not find cg idx", K(ret), K(column_id), K_(max_used_column_group_id));
    }
  }
  LOG_TRACE("[CS-Replica] get column group index", K(ret), K(need_calculate_cg_idx), K(param), K(cg_idx), KPC(this));
  return ret;
}
}  // namespace schema
}  // namespace share
}  // namespace oceanbase


// === demoted from ObTableSchema  storage free function(old member definition was split across modules, now fully moved out of the share class) ===
namespace oceanbase
{
namespace storage
{
int get_orig_default_row(const share::schema::ObTableSchema &table_schema,
                         const common::ObIArray<share::schema::ObColDesc> &column_ids,
                         blocksstable::ObDatumRow &default_row)
{
  int ret = OB_SUCCESS;
  const int64_t column_cnt = table_schema.get_column_count();
  if (OB_UNLIKELY(!default_row.is_valid() || default_row.count_ != column_ids.count() || column_ids.count() > column_cnt + 2)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(column_cnt), K(default_row), K(column_ids.count()));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
    if (column_ids.at(i).col_id_ == OB_HIDDEN_TRANS_VERSION_COLUMN_ID ||
        column_ids.at(i).col_id_ == OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID) {
      default_row.storage_datums_[i].set_int(0);
    } else {
      bool found = false;
      for (int64_t j = 0; OB_SUCC(ret) && !found && j < column_cnt; ++j) {
        const share::schema::ObColumnSchemaV2 *column = table_schema.column_begin()[j];
        if (NULL == column) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("column must not null", K(ret), K(j), K(column_cnt));
        } else if (column->get_column_id() == column_ids.at(i).col_id_) {
          if (OB_FAIL(default_row.storage_datums_[i].from_obj_enhance(column->get_orig_default_value()))) {
            STORAGE_LOG(WARN, "Failed to transefer obj to datum", K(ret));
          } else {
            found = true;
          }
        }
      }
      if (OB_SUCC(ret) && !found) {
        ret = OB_ERR_SYS;
        LOG_WARN("column id not found", K(ret), K(column_ids.at(i)));
      }
    }
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase
