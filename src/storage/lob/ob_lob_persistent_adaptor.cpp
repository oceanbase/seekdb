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

#include "ob_lob_persistent_adaptor.h"
#include "share/rc/ob_module_provider.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "ob_lob_persistent_reader.h"
#include "share/schema/ob_table_dml_param.h"
#include "share/schema/ob_tenant_schema_service.h"
#include "share/ob_tablet_autoincrement_service.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace storage
{

ObPersistentLobApator::~ObPersistentLobApator()
{
  destroy();
}

void ObPersistentLobApator::destroy()
{
  STORAGE_LOG(INFO, "[LOB] destroy lob persist");
  if (OB_NOT_NULL(meta_table_param_)) {
    meta_table_param_->reset();
    meta_table_param_->~ObTableParam();
    allocator_.free(meta_table_param_);
    meta_table_param_ = nullptr;
  }
  if (OB_NOT_NULL(meta_table_dml_param_)) {
    meta_table_dml_param_->reset();
    meta_table_dml_param_->~ObTableDMLParam();
    allocator_.free(meta_table_dml_param_);
    meta_table_dml_param_ = nullptr;
  }
  allocator_.reset();
}

int ObPersistentLobApator::init_meta_column_ids(ObSEArray<uint64_t, 6> &meta_column_ids)
{
  int ret = OB_SUCCESS;
  for (uint32_t i = 0; OB_SUCC(ret) && i < ObLobMetaUtil::LOB_META_COLUMN_CNT; i++) {
    if (OB_FAIL(meta_column_ids.push_back(OB_APP_MIN_COLUMN_ID + i))) {
    }
  }
  return ret;
}

int ObPersistentLobApator::get_meta_table_param(const ObTableParam *&table_param)
{
  int ret = OB_SUCCESS;
  if (! ATOMIC_LOAD(&table_param_inited_)) {
    ObLockGuard<ObSpinLock> guard(lock_);
    if (! ATOMIC_LOAD(&table_param_inited_)) {
      if (OB_FAIL(init_table_param())) {
      } else {
        LOG_INFO("init_table_param success", KR(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    table_param = ATOMIC_LOAD(&meta_table_param_);
  }
  return ret;
}
int ObPersistentLobApator::get_meta_table_dml_param(const ObTableDMLParam *&table_param)
{
  int ret = OB_SUCCESS;
  if (! ATOMIC_LOAD(&table_param_inited_)) {
    ObLockGuard<ObSpinLock> guard(lock_);
    if (! ATOMIC_LOAD(&table_param_inited_)) {
      if (OB_FAIL(init_table_param())) {
      } else {
        LOG_INFO("init_table_param success", KR(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    table_param = ATOMIC_LOAD(&meta_table_dml_param_);
  }
  return ret;
}

int ObPersistentLobApator::init_table_param()
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator("TmpLobPersist", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObSEArray<uint64_t, 6> meta_column_ids;
  HEAP_VAR(ObTableSchema, meta_schema, &tmp_allocator) {
    if (ATOMIC_LOAD(&table_param_inited_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("init again", KR(ret), K(table_param_inited_));
    } else if (nullptr != meta_table_param_ || nullptr != meta_table_dml_param_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("init again", KR(ret), KP(meta_table_param_), KP(meta_table_dml_param_));
    } else if (OB_FAIL(share::ObInnerTableSchema::all_column_aux_lob_meta_schema(meta_schema))) {
    } else if (OB_FAIL(init_meta_column_ids(meta_column_ids))) {
    } else if (OB_FALSE_IT(ATOMIC_STORE(&meta_table_param_, OB_NEWx(ObTableParam, &allocator_, allocator_)))) {
    } else if (OB_ISNULL(meta_table_param_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc meta_table_param fail", KR(ret), "size", sizeof(ObTableParam));
    } else if (OB_FAIL(meta_table_param_->convert(meta_schema, meta_column_ids, sql::ObStoragePushdownFlag()))) {
    } else if (OB_FALSE_IT(ATOMIC_STORE(&meta_table_dml_param_, OB_NEWx(ObTableDMLParam, &allocator_, allocator_)))) {
    } else if (OB_ISNULL(meta_table_dml_param_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc meta_table_param fail", KR(ret), "size", sizeof(ObTableDMLParam));
    } else if (OB_FAIL(meta_table_dml_param_->convert(&meta_schema, meta_schema.get_schema_version(), meta_column_ids))) {
    } else {
      ATOMIC_STORE(&table_param_inited_, true);
    }
  }
  return ret;
}

int ObPersistentLobApator::prepare_table_param(
    const ObLobAccessParam &param,
    ObTableScanParam &scan_param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(scan_param.table_param_ != NULL)) {
  } else if (OB_FAIL(get_meta_table_param(scan_param.table_param_))) {
  }
  return ret;
}

int ObPersistentLobApator::get_lob_data(
    ObLobAccessParam &param,
    uint64_t piece_id,
    ObLobPieceInfo& info)
{
  return OB_NOT_SUPPORTED;
}

int ObPersistentLobApator::revert_scan_iter(ObLobMetaIterator *iter)
{
  int ret = OB_SUCCESS;
  ObAccessService *oas = share::g_mp->access_service();
  if (OB_ISNULL(oas)) {
    ret = OB_ERR_INTERVAL_INVALID;
    LOG_WARN("get access service failed.", K(ret));
  } else if (OB_ISNULL(iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("iter is null", K(ret));
  } else if (nullptr != iter->get_access_ctx()) {
    LOG_DEBUG("alloced from ctx, no need actual release", KPC(iter));
  } else {
    LOG_DEBUG("release scan iter", K(ret), KPC(iter));
    iter->reset();
    iter->~ObLobMetaIterator();
  }
  return ret;
}

/**
 * use lob_meta_tablet_id aux inc to generate lob_id
*/
int ObPersistentLobApator::fetch_lob_id(ObLobAccessParam& param, uint64_t &lob_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(prepare_lob_tablet_id(param))) {
  } else {
    
    share::ObTabletAutoincrementService &auto_inc = share::ObTabletAutoincrementService::get_instance();
    if (OB_FAIL(auto_inc.get_autoinc_seq(param.lob_meta_tablet_id_, lob_id, share::ObTabletAutoincrementService::LOB_CACHE_SIZE))) {
    } else {
      LOG_DEBUG("get lob_id succ", K(lob_id), K(param));
    }

    if (OB_TABLET_IS_SPLIT_SRC == ret) {
      if (OB_FAIL(fetch_lob_id_for_split_src(param, param.lob_meta_tablet_id_, lob_id))) {
      }
    }
  }
  return ret;
}

int ObPersistentLobApator::fetch_lob_id_for_split_src(const ObLobAccessParam& param, const ObTabletID &lob_tablet_id, uint64_t &lob_id)
{
  int ret = OB_SUCCESS;
  
  ObLSHandle ls_handle;
  ObTabletHandle tablet_handle;
  ObTabletID dst_tablet_id;
  share::ObTabletAutoincrementService &auto_inc = share::ObTabletAutoincrementService::get_instance();
  if (OB_ISNULL(param.data_row_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid data row", K(ret), K(lob_tablet_id));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(param.ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
  } else if (OB_ISNULL(ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret));
  } else if (OB_FAIL(ls_handle.get_ls()->get_tablet_with_timeout(lob_tablet_id,
                                                                 tablet_handle,
                                                                 param.timeout_,
                                                                 ObMDSGetTabletMode::READ_ALL_COMMITED,
                                                                 share::SCN::max_scn()))) {
  } else if (OB_FAIL(ObTabletSplitMdsHelper::calc_split_dst_lob(*ls_handle.get_ls(), *tablet_handle.get_obj(), *param.data_row_, param.timeout_, dst_tablet_id))) {
  } else if (OB_FAIL(auto_inc.get_autoinc_seq(dst_tablet_id, lob_id, share::ObTabletAutoincrementService::LOB_CACHE_SIZE))) {
  }
  return ret;
}

int ObPersistentLobApator::prepare_lob_meta_dml(ObLobAccessParam& param)
{
  int ret = OB_SUCCESS;
  if (param.dml_base_param_ == nullptr) {
    ObStoreCtxGuard *store_ctx_guard = nullptr;
    void *buf = param.allocator_->alloc(sizeof(ObDMLBaseParam) + sizeof(ObStoreCtxGuard));

    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc dml base param", K(ret), K(param));
    } else {
      param.dml_base_param_ = new(buf)ObDMLBaseParam();
      store_ctx_guard = new((char*)buf + sizeof(ObDMLBaseParam)) ObStoreCtxGuard();
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(build_lob_meta_table_dml(param, *param.dml_base_param_, store_ctx_guard))) {
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(set_dml_seq_no(param))) {
    LOG_WARN("update_seq_no fail", K(ret), K(param));
  }

  if (OB_SUCC(ret)) {
    param.dml_base_param_->store_ctx_guard_->reset();
    ObAccessService *oas = share::g_mp->access_service();
    if (OB_FAIL(oas->get_write_store_ctx_guard(param.ls_id_,
                                               param.timeout_,
                                               *param.tx_desc_,
                                               param.snapshot_,
                                               0,/*branch_id*/
                                               param.dml_base_param_->write_flag_,
                                               *param.dml_base_param_->store_ctx_guard_,
                                               param.dml_base_param_->spec_seq_no_ ))) {
    }
  }
  return ret;
}

int ObPersistentLobApator::build_lob_meta_table_dml(
    ObLobAccessParam& param,
    ObDMLBaseParam &dml_base_param,
    ObStoreCtxGuard *store_ctx_guard)
{
  int ret = OB_SUCCESS;
  // dml base
  dml_base_param.timeout_ = param.timeout_;
  dml_base_param.is_total_quantity_log_ = param.is_total_quantity_log_;
  dml_base_param.tz_info_ = NULL;
  dml_base_param.sql_mode_ = SMO_DEFAULT;
  dml_base_param.check_schema_version_ = false; // lob tablet should not check schema version
  dml_base_param.data_row_for_lob_ = param.data_row_;
  dml_base_param.schema_version_ = 0;
  dml_base_param.store_ctx_guard_ = store_ctx_guard;
  dml_base_param.write_flag_.reset();
  dml_base_param.write_flag_.set_is_insert_up();
  if (param.skip_flush_redo()) dml_base_param.write_flag_.set_skip_flush_redo();
  dml_base_param.dml_allocator_ = param.allocator_;
  if (OB_FAIL(get_meta_table_dml_param(dml_base_param.table_param_))) {
  } else if (OB_FAIL(dml_base_param.snapshot_.assign(param.snapshot_))) {
  }
  return ret;
}


int ObPersistentLobApator::prepare_table_scan_param(
    const ObLobAccessParam &param,
    const bool is_get,
    ObTableScanParam &scan_param,
    ObIAllocator *stmt_allocator,
    ObIAllocator *scan_allocator)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(build_common_scan_param(param, is_get, ObLobMetaUtil::LOB_META_COLUMN_CNT, scan_param, stmt_allocator, scan_allocator))) {
  } else if (OB_FAIL(prepare_table_param(param, scan_param))) {
  }
  return ret;
}

int ObPersistentLobApator::build_common_scan_param(
    const ObLobAccessParam &param,
    const bool is_get,
    uint32_t col_num,
    ObTableScanParam& scan_param,
    ObIAllocator *stmt_allocator,
    ObIAllocator *scan_allocator)
{
  int ret = OB_SUCCESS;

  ObQueryFlag query_flag(ObQueryFlag::Forward, // scan_order
                          false, // daily_merge
                          false, // optimize
                          false, // sys scan
                          true, // full_row
                          false, // index_back
                          false, // query_stat
                          ObQueryFlag::MysqlMode, // sql_mode
                          param.need_read_latest_ // read_latest
                        );
  query_flag.disable_cache();
  if (param.enable_block_cache()) query_flag.set_use_block_cache();
  query_flag.scan_order_ = param.scan_backward_ ? ObQueryFlag::Reverse : ObQueryFlag::Forward;
  scan_param.scan_flag_.flag_ = query_flag.flag_;
  // set column ids
  scan_param.column_ids_.reuse();
  for (uint32_t i = 0; OB_SUCC(ret) && i < col_num; i++) {
    if (OB_FAIL(scan_param.column_ids_.push_back(OB_APP_MIN_COLUMN_ID + i))) {
    }
  }

  if (OB_SUCC(ret)) {
    scan_param.ls_id_ = param.ls_id_;
    scan_param.tablet_id_ = param.lob_meta_tablet_id_;

    scan_param.reserved_cell_count_ = scan_param.column_ids_.count();
    // table param
    scan_param.index_id_ = 0; // table id
    scan_param.is_get_ = is_get;
    // set timeout
    scan_param.timeout_ = param.timeout_;
    // scan_param.virtual_column_exprs_
    scan_param.limit_param_.limit_ = -1;
    scan_param.limit_param_.offset_ = 0;
    // sessions
    if(param.read_latest_ || param.need_read_latest_) {
      scan_param.tx_id_ = param.snapshot_.core_.tx_id_;
    }
    scan_param.sql_mode_ = param.sql_mode_;
    // common set
    scan_param.allocator_ = stmt_allocator;
    scan_param.for_update_ = false;
    scan_param.for_update_wait_timeout_ = scan_param.timeout_;
    scan_param.scan_allocator_ = scan_allocator;
    scan_param.frozen_version_ = -1;
    scan_param.force_refresh_lc_ = false;
    scan_param.output_exprs_ = nullptr;
    scan_param.aggregate_exprs_ = nullptr;
    scan_param.op_ = nullptr;
    scan_param.row2exprs_projector_ = nullptr;
    scan_param.need_scn_ = false;
    scan_param.pd_storage_flag_ = false;
    scan_param.fb_snapshot_ = param.fb_snapshot_;
    if (OB_FAIL(scan_param.snapshot_.assign(param.snapshot_))) {
    }
  }
  return ret;
}

bool ObPersistentLobApator::check_lob_tablet_id(
    const common::ObTabletID &data_tablet_id,
    const common::ObTabletID &lob_meta_tablet_id,
    const common::ObTabletID &lob_piece_tablet_id)
{
  bool bret = false;
  if (OB_UNLIKELY(!lob_meta_tablet_id.is_valid() || lob_meta_tablet_id == data_tablet_id)) {
    bret = true;
  } else if (OB_UNLIKELY(!lob_piece_tablet_id.is_valid() || lob_piece_tablet_id == data_tablet_id)) {
    bret = true;
  } else if (OB_UNLIKELY(lob_meta_tablet_id == lob_piece_tablet_id)) {
    bret = true;
  }
  return bret;
}

int ObPersistentLobApator::inner_get_tablet(
    const ObLobAccessParam &param,
    const common::ObTabletID &tablet_id,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  if (OB_FAIL(share::g_mp->ls_service()->get_ls(param.ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
  } else if (OB_ISNULL(ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("ls should not be null", K(ret));
  } else if (OB_FAIL(inner_get_tablet(param, tablet_id, ls_handle, handle))) {
  }
  return ret;
}

int ObPersistentLobApator::inner_get_tablet(
    const ObLobAccessParam &param,
    const common::ObTabletID &tablet_id,
    ObLSHandle &ls_handle,
    ObTabletHandle &handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ls_handle.get_ls()->get_tablet_with_timeout(tablet_id,
                                                                 handle,
                                                                 param.timeout_,
                                                                 ObMDSGetTabletMode::READ_READABLE_COMMITED,
                                                                 param.snapshot_.core_.version_))) {
    if (OB_TABLET_IS_SPLIT_SRC == ret) {
      if (OB_FAIL(ls_handle.get_ls()->get_tablet_with_timeout(tablet_id,
                                                              handle,
                                                              param.timeout_,
                                                              ObMDSGetTabletMode::READ_ALL_COMMITED,
                                                              share::SCN::max_scn()))) {
      }
    } else {
      LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id), K(param));
    }
  }
  return ret;
}

void ObPersistentLobApator::set_lob_meta_row(
    blocksstable::ObDatumRow& datum_row,
    ObLobMetaInfo& in_row)
{
  datum_row.reuse();
  datum_row.storage_datums_[ObLobMetaUtil::LOB_ID_COL_ID].set_string(reinterpret_cast<char*>(&in_row.lob_id_), sizeof(ObLobId));
  datum_row.storage_datums_[ObLobMetaUtil::SEQ_ID_COL_ID].set_string(in_row.seq_id_);
  datum_row.storage_datums_[ObLobMetaUtil::BYTE_LEN_COL_ID].set_uint32(in_row.byte_len_);
  datum_row.storage_datums_[ObLobMetaUtil::CHAR_LEN_COL_ID].set_uint32(in_row.char_len_);
  datum_row.storage_datums_[ObLobMetaUtil::PIECE_ID_COL_ID].set_uint(in_row.piece_id_);
  datum_row.storage_datums_[ObLobMetaUtil::LOB_DATA_COL_ID].set_string(in_row.lob_data_);
}


int ObPersistentLobApator::scan_lob_meta(
    ObLobAccessParam &param,
    ObLobMetaIterator *&iter)
{
  int ret = OB_SUCCESS;
  ObLobMetaIterator *tmp_iter = nullptr;
  if (OB_NOT_NULL(iter)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("input iter is not null", K(ret), KPC(iter), K(param));
  } else if (nullptr != param.access_ctx_) {
    if (OB_FAIL(scan_with_ctx(param, iter))) {
    }
  } else if (OB_ISNULL(param.allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("allocator is null", K(ret), K(param));
  } else if (OB_ISNULL(tmp_iter = OB_NEWx(ObLobMetaIterator, param.allocator_, nullptr))) {
    ret = ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc iter fail", K(ret), K(param), "alloc_size", sizeof(ObLobMetaIterator));
  } else if (OB_FAIL(tmp_iter->open(param, this, param.allocator_))) {
  } else {
    iter = tmp_iter;
  }

  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(tmp_iter)) {
      LOG_INFO("release iter after failed", KPC(tmp_iter));
      tmp_iter->reset();
      tmp_iter->~ObLobMetaIterator();
    }
  }
  return ret;
}

int ObPersistentLobApator::scan_with_ctx(
    ObLobAccessParam &param,
    ObLobMetaIterator *&iter)
{
  int ret = OB_SUCCESS;
  bool is_hit = false;
  ObPersistLobReaderCacheKey key;
  ObLobMetaIterator *reader = nullptr;
  ObPersistLobReaderCache &cache = param.access_ctx_->reader_cache_;
  key.ls_id_ = param.ls_id_;
  key.snapshot_ = param.snapshot_.core_.version_.get_val_for_tx();
  key.tablet_id_ = param.tablet_id_;
  key.is_get_ = param.has_single_chunk();
  if (OB_FAIL(cache.get(key, reader))) {
  } else if (nullptr != reader) { // use cache
    is_hit = true;
    if (OB_FAIL(reader->rescan(param))) {
    }
  } else if (OB_ISNULL(reader = cache.alloc_reader(param.access_ctx_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc_reader fail", K(ret));
  } else if (OB_FAIL(reader->open(param, this, &(param.access_ctx_->reader_cache_.get_allocator())))) {
  } else if (OB_FAIL(cache.put(key, reader))) {
  }

  if (OB_FAIL(ret)) {
    if (! is_hit && OB_NOT_NULL(reader)) {
      LOG_INFO("release iter after failed", KPC(reader));
      reader->reset();
      reader->~ObLobMetaIterator();
    }
  } else {
    iter = reader;
  }
  return ret;
}

// TODO aozeliu.azl read need?
int ObPersistentLobApator::prepare_scan_param_schema_version(
    ObLobAccessParam &param,
    ObTableScanParam &scan_param)
{
  int ret = OB_SUCCESS;
  ObTabletHandle lob_meta_tablet;
  if (! param.lob_meta_tablet_id_.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("lob_meta_tablet_id is invalid", K(ret), K(param));
  } else if (OB_FAIL(inner_get_tablet(param, param.lob_meta_tablet_id_, lob_meta_tablet))) {
  } else {
    scan_param.schema_version_ = lob_meta_tablet.get_obj()->get_tablet_meta().max_sync_storage_schema_version_;
  }
  return ret;
}

int ObPersistentLobApator::prepare_lob_tablet_id(ObLobAccessParam& param)
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObTabletHandle data_tablet;
  ObTabletBindingMdsUserData ddl_data;
  if (! param.tablet_id_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet_id of main table is invalid", K(ret), K(param));
  } else if (param.lob_meta_tablet_id_.is_valid() && param.lob_piece_tablet_id_.is_valid()) {
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(param.ls_id_, ls_handle, ObLSGetMod::STORAGE_MOD))) {
  } else if (OB_ISNULL(ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls should not be null", K(ret));
  } else if (OB_FAIL(inner_get_tablet(param, param.tablet_id_, ls_handle, data_tablet))) {
  } else if (OB_FAIL(data_tablet.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) {
  } else if (OB_UNLIKELY(check_lob_tablet_id(param.tablet_id_, ddl_data.lob_meta_tablet_id_, ddl_data.lob_piece_tablet_id_))) {
    if (ls_handle.get_ls()->is_offline()) {
      ret = OB_LS_OFFLINE;
      LOG_WARN("ls is offline, can not get lob tablet id", K(ret), K(param), K(ddl_data.lob_meta_tablet_id_), K(ddl_data.lob_piece_tablet_id_));
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid lob tablet id", K(ret), K(param), K(ddl_data.lob_meta_tablet_id_), K(ddl_data.lob_piece_tablet_id_));
    }
  } else {
    param.lob_meta_tablet_id_ = ddl_data.lob_meta_tablet_id_;
    param.lob_piece_tablet_id_ = ddl_data.lob_piece_tablet_id_;
  }
  return ret;
}

int ObPersistentLobApator::set_dml_seq_no(ObLobAccessParam &param)
{
  int ret = OB_SUCCESS;
  if (param.seq_no_st_.is_valid()) {
    if (param.used_seq_cnt_ < param.total_seq_cnt_) {
      param.dml_base_param_->spec_seq_no_ = param.seq_no_st_ + param.used_seq_cnt_;
      // param.used_seq_cnt_++;
      LOG_DEBUG("dml lob meta with seq no", K(param.dml_base_param_->spec_seq_no_));
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("failed to get seq no from param", K(ret), K(param));
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid seq no from param", K(ret), K(param));
  }
  return ret;
}

int ObPersistentLobApator::erase_lob_meta(ObLobAccessParam &param, ObDatumRowIterator& row_iter)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObAccessService *oas = share::g_mp->access_service();
  if (OB_ISNULL(oas)) {
    ret = OB_ERR_INTERVAL_INVALID;
    LOG_WARN("get access service failed", K(ret), KP(oas));
  } else if (OB_ISNULL(param.tx_desc_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("get tx desc null", K(ret), K(param));
  } else if (OB_FAIL(prepare_lob_tablet_id(param))) {
  } else if (OB_FAIL(prepare_lob_meta_dml(param))) {
  } else {
    ObSEArray<uint64_t, 6> column_ids;
    for (int i = 0; OB_SUCC(ret) && i < ObLobMetaUtil::LOB_META_COLUMN_CNT; ++i) {
      if (OB_FAIL(column_ids.push_back(OB_APP_MIN_COLUMN_ID + i))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(oas->delete_rows(
        param.ls_id_,
        param.lob_meta_tablet_id_,
        *param.tx_desc_,
        *param.dml_base_param_,
        column_ids,
        &row_iter,
        affected_rows))) {
    }
  }
  return ret;
}

int ObPersistentLobApator::write_lob_meta(ObLobAccessParam& param, ObDatumRowIterator& row_iter)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObAccessService *oas = share::g_mp->access_service();
  if (OB_ISNULL(oas)) {
    ret = OB_ERR_INTERVAL_INVALID;
    LOG_WARN("get access service failed", K(ret), KP(oas));
  } else if (OB_ISNULL(param.tx_desc_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("get tx desc null", K(ret), K(param));
  } else if (OB_FAIL(prepare_lob_tablet_id(param))) {
  } else if (OB_FAIL(prepare_lob_meta_dml(param))) {
  } else {
    ObSEArray<uint64_t, 6> column_ids;
    for (int i = 0; OB_SUCC(ret) && i < ObLobMetaUtil::LOB_META_COLUMN_CNT; ++i) {
      if (OB_FAIL(column_ids.push_back(OB_APP_MIN_COLUMN_ID + i))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(oas->insert_rows(
        param.ls_id_,
        param.lob_meta_tablet_id_,
        *param.tx_desc_,
        *param.dml_base_param_,
        column_ids,
        &row_iter,
        affected_rows))) {
    }
  }
  return ret;
}

int ObPersistentLobApator::update_lob_meta(ObLobAccessParam& param, ObDatumRowIterator &row_iter)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObAccessService *oas = share::g_mp->access_service();
  if (OB_ISNULL(oas)) {
    ret = OB_ERR_INTERVAL_INVALID;
    LOG_WARN("get access service failed", K(ret), KP(oas));
  } else if (OB_ISNULL(param.tx_desc_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("get tx desc null", K(ret), K(param));
  } else if (OB_FAIL(prepare_lob_tablet_id(param))) {
  } else if (OB_FAIL(prepare_lob_meta_dml(param))) {
  } else {
    ObSEArray<uint64_t, 6> column_ids;
    for (int i = 0; OB_SUCC(ret) && i < ObLobMetaUtil::LOB_META_COLUMN_CNT; ++i) {
      if (OB_FAIL(column_ids.push_back(OB_APP_MIN_COLUMN_ID + i))) {
      }
    }
    ObSEArray<uint64_t, 6> update_column_ids;
    for (int i = 2; OB_SUCC(ret) && i < ObLobMetaUtil::LOB_META_COLUMN_CNT; ++i) {
      if (OB_FAIL(update_column_ids.push_back(OB_APP_MIN_COLUMN_ID + i))) {
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(oas->update_rows(
        param.ls_id_,
        param.lob_meta_tablet_id_,
        *param.tx_desc_,
        *param.dml_base_param_,
        column_ids,
        update_column_ids,
        &row_iter,
        affected_rows))) {
    }
  }
  return ret;
}

int ObPersistentLobApator::write_lob_meta(ObLobAccessParam &param, ObLobMetaInfo& row_info)
{
  int ret = OB_SUCCESS;
  ObDatumRow new_row;
  ObLobPersistInsertSingleRowIter single_iter;
  if (OB_FAIL(new_row.init(ObLobMetaUtil::LOB_META_COLUMN_CNT))) {
  } else if (FALSE_IT(set_lob_meta_row(new_row, row_info))) {
  } else if (OB_FAIL(single_iter.init(&param, &new_row))) {
  } else if (OB_FAIL(write_lob_meta(param, single_iter))) {
  }
  return ret;
}

int ObPersistentLobApator::erase_lob_meta(ObLobAccessParam &param, ObLobMetaInfo& row_info)
{
  int ret = OB_SUCCESS;
  ObDatumRow new_row;
  ObLobPersistDeleteSingleRowIter single_iter;
  if (OB_FAIL(new_row.init(ObLobMetaUtil::LOB_META_COLUMN_CNT))) {
  } else if (FALSE_IT(set_lob_meta_row(new_row, row_info))) {
  } else if (OB_FAIL(single_iter.init(&param, &new_row))) {
  } else if (OB_FAIL(erase_lob_meta(param, single_iter))) {
  }
  return ret;
}

int ObPersistentLobApator::update_lob_meta(ObLobAccessParam& param, ObLobMetaInfo& old_row, ObLobMetaInfo& new_row)
{
  int ret = OB_SUCCESS;
  ObDatumRow new_datum_row;
  ObDatumRow old_datum_row;
  ObLobPersistUpdateSingleRowIter upd_iter;
  if (OB_FAIL(new_datum_row.init(ObLobMetaUtil::LOB_META_COLUMN_CNT))) {
  } else if (OB_FAIL(old_datum_row.init(ObLobMetaUtil::LOB_META_COLUMN_CNT))) {
  } else if (FALSE_IT(set_lob_meta_row(new_datum_row, new_row))) {
  } else if (FALSE_IT(set_lob_meta_row(old_datum_row, old_row))) {
  } else if (OB_FAIL(upd_iter.init(&param, &old_datum_row, &new_datum_row))) {
  } else if (OB_FAIL(update_lob_meta(param, upd_iter))) {
  }
  return ret;
}

} // storage
} // oceanbase

