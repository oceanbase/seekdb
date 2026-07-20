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

#include "ob_direct_load_struct.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "share/rc/ob_module_provider.h"
#include "share/ob_ddl_error_message_table_operator.h"
#include "storage/ob_tablet_autoincrement_service.h"
#include "sql/engine/pdml/static/ob_px_sstable_insert_op.h"
#include "storage/ob_storage_schema_util.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/das/ob_das_utils.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "storage/blocksstable/index_block/ob_macro_meta_temp_store.h"
#include "storage/ddl/ob_direct_load_mgr_v3.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_ddl_merge_helper.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;
using namespace oceanbase::transaction;

int ObTabletDirectLoadInsertParam::assign(const ObTabletDirectLoadInsertParam &other_param)
{
  int ret = OB_SUCCESS;
  if (other_param.common_param_.is_valid()) {
    common_param_ = other_param.common_param_;
  }
  if (other_param.runtime_only_param_.is_valid()) {
    runtime_only_param_ = other_param.runtime_only_param_;
  }
  is_replay_ = other_param.is_replay_;
  return ret;
}

ObLobMetaRowIterator::ObLobMetaRowIterator()
  : is_inited_(false), iter_(nullptr), trans_version_(0),
    tmp_row_(), lob_meta_write_result_()
{
}

ObLobMetaRowIterator::~ObLobMetaRowIterator()
{
  reset();
}

int ObLobMetaRowIterator::init(ObLobMetaWriteIter *iter,
                                const int64_t trans_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_ISNULL(iter) || OB_UNLIKELY(trans_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(iter), K(trans_version));
  } else if (!tmp_row_.is_valid() && OB_FAIL(tmp_row_.init(ObLobMetaUtil::LOB_META_COLUMN_CNT + ObLobMetaUtil::SKIP_INVALID_COLUMN))) {
    LOG_WARN("Failed to init datum row", K(ret));
  } else {
    iter_ = iter;
    trans_version_ = trans_version;
    is_inited_ = true;
  }
  return ret;
}

void ObLobMetaRowIterator::reset()
{
  is_inited_ = false;
  iter_ = nullptr;
  trans_version_ = 0;
  tmp_row_.reset();
}

void ObLobMetaRowIterator::reuse()
{
  is_inited_ = false;
  iter_ = nullptr;
  trans_version_ = 0;
  tmp_row_.reuse();
}

int ObLobMetaRowIterator::get_next_row(const blocksstable::ObDatumRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObLobMetaWriteIter is nullptr", K(ret));
  } else if (OB_FAIL(iter_->get_next_row(lob_meta_write_result_))) {
    if (OB_UNLIKELY(ret != OB_ITER_END)) {
      LOG_WARN("failed to get next row", K(ret));
    }
  } else {
    if (OB_FAIL(ObLobMetaUtil::transform_from_info_to_row(lob_meta_write_result_.info_, &tmp_row_, true))) {
      LOG_WARN("transform failed", K(ret), K(lob_meta_write_result_.info_));
    } else {
      tmp_row_.storage_datums_[ObLobMetaUtil::SEQ_ID_COL_ID + 1].set_int(-trans_version_);
      tmp_row_.storage_datums_[ObLobMetaUtil::SEQ_ID_COL_ID + 2].set_int(0);
      tmp_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
      tmp_row_.mvcc_row_flag_.set_compacted_multi_version_row(true);
      tmp_row_.mvcc_row_flag_.set_first_multi_version_row(true);
      tmp_row_.mvcc_row_flag_.set_last_multi_version_row(true);
      tmp_row_.mvcc_row_flag_.set_uncommitted_row(false);
      row = &tmp_row_;
    }
  }
  return ret;
}

ObTabletDDLParam::ObTabletDDLParam()
  : direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID),
    start_scn_(SCN::min_scn()),
    commit_scn_(SCN::min_scn()),
    data_format_version_(0),
    table_key_(),
    snapshot_version_(0)
{

}

ObTabletDDLParam::~ObTabletDDLParam()
{

}

/**
 * ObChunkSliceStore
 */
int ObChunkSliceStore::init(const int64_t rowkey_column_count, const ObStorageSchema *storage_schema,
    ObArenaAllocator &allocator, const ObIArray<ObColumnSchemaItem> &col_array, const int64_t dir_id,
    const int64_t parallelism)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_ISNULL(storage_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null schema", K(ret), K(*this));
  } else if (OB_UNLIKELY(rowkey_column_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalida argument", K(ret), K(rowkey_column_count));
  } else if (FALSE_IT(arena_allocator_ = &allocator)) {
  } else if (OB_FAIL(prepare_datum_store(storage_schema, allocator, col_array, dir_id, parallelism))) {
    LOG_WARN("fail to prepare datum store", K(ret));
  } else {
    rowkey_column_count_ = rowkey_column_count;
    is_inited_ = true;
    LOG_DEBUG("init chunk slice store", K(ret), KPC(this));
  }
  return ret;
}

void ObChunkSliceStore::reset()
{
  if (OB_NOT_NULL(arena_allocator_) && OB_NOT_NULL(datum_store_)) {
    datum_store_->~ObCompactStore();
    arena_allocator_->free(datum_store_);
    datum_store_ = nullptr;
  }
  endkey_.reset();
  row_cnt_ = 0;
  arena_allocator_ = nullptr;
  is_canceled_ = false;
  is_inited_ = false;
}

int ObChunkSliceStore::prepare_datum_store(const ObStorageSchema *storage_schema,
                                           ObIAllocator &allocator,
                                           const ObIArray<ObColumnSchemaItem> &col_array,
                                           const int64_t dir_id,
                                           const int64_t parallelism)
{
  int ret = OB_SUCCESS;
  const int64_t chunk_mem_limit = 64 * 1024L; // 64K
  void *buf = nullptr;
  ObCompressorType compressor_type = NONE_COMPRESSOR;
  if (OB_UNLIKELY(nullptr == storage_schema || col_array.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(storage_schema), K(col_array.count()));
  } else if (FALSE_IT(compressor_type = storage_schema->get_compressor_type())) {
  } else if (OB_FAIL(ObDDLUtil::get_temp_store_compress_type(
                 compressor_type, parallelism, compressor_type))) {
    LOG_WARN("fail to get temp store compress type", K(ret));
  } else if (OB_ISNULL(buf = allocator.alloc(sizeof(ObCompactStore)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory failed", K(ret));
  } else if (FALSE_IT(datum_store_ = new (buf) ObCompactStore())) {
  } else if (OB_FAIL(datum_store_->init(chunk_mem_limit,
                                        col_array,
                                        ObCtxIds::DEFAULT_CTX_ID,
                                        "DL_SLICE_STORE",
                                        true/*enable_dump*/,
                                        0,
                                        false/*disable truncate*/,
                                        compressor_type))) {
    LOG_WARN("failed to init chunk datum store", K(ret));
  } else {
    datum_store_->set_dir_id(dir_id);
    LOG_INFO("set dir id", K(dir_id));
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(datum_store_)) {
    datum_store_->~ObCompactStore();
    allocator.free(datum_store_);
    datum_store_ = nullptr;
  }
  LOG_INFO("init ObChunkSliceStore", K(*this));
  return ret;
}

int ObChunkSliceStore::append_row(const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!datum_row.is_valid() || datum_row.get_column_count() < rowkey_column_count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(datum_row), K(rowkey_column_count_));
  } else if (OB_FAIL(datum_store_->add_row(datum_row, 0/*extra_size*/))) {
    LOG_WARN("chunk datum store add row failed", K(ret), K(datum_row.get_column_count()));
  } else {
    ++row_cnt_;
  }
  return ret;
}

int ObChunkSliceStore::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_NOT_NULL(datum_store_) && datum_store_->get_row_cnt() > 0) { // save endkey
    const ObChunkDatumStore::StoredRow *stored_row = nullptr;
    if (OB_FAIL(datum_store_->get_last_stored_row(stored_row))) {
      LOG_WARN("fail to get last stored row", K(ret));
    } else if (OB_UNLIKELY(nullptr == stored_row || stored_row->cnt_ < rowkey_column_count_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("last stored row is null", K(ret), KPC(stored_row));
    } else {
      void *buf = arena_allocator_->alloc(sizeof(ObStorageDatum) * rowkey_column_count_);
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory for endkey datums failed", K(ret), KPC(stored_row));
      } else {
        endkey_.datums_ = new (buf) ObStorageDatum[rowkey_column_count_];
        endkey_.datum_cnt_ = rowkey_column_count_;
        ObStorageDatum tmp_datum;
        for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_column_count_; ++i) {
          tmp_datum.shallow_copy_from_datum(stored_row->cells()[i]);
          if (OB_FAIL(endkey_.datums_[i].deep_copy(tmp_datum, *arena_allocator_))) {
            LOG_WARN("deep copy storage datum failed", K(ret));
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(datum_store_->dump(true/*all_dump*/))) {
    LOG_WARN("dump failed", K(ret));
  } else if (OB_SUCC(ret) && OB_FAIL(datum_store_->finish_add_row(true/*need_dump*/))) {
    LOG_WARN("finish add row failed", K(ret));
  }
  LOG_DEBUG("chunk slice store closed", K(ret), K(endkey_));
  return ret;
}

/**
 * ObChunkBatchSliceStore
 */

void ObChunkBatchSliceStore::reset()
{
  is_inited_ = false;
  if (OB_NOT_NULL(arena_allocator_) && OB_NOT_NULL(row_ctx_)) {
    row_ctx_->~RowStoreCtx();
    arena_allocator_->free(row_ctx_);
    row_ctx_ = nullptr;
  }
  arena_allocator_ = nullptr;
  column_count_ = 0;
  rowkey_column_count_ = 0;
  row_cnt_ = 0;
  start_key_.reset();
  is_canceled_ = false;
}

int ObChunkBatchSliceStore::init(const int64_t rowkey_column_count,
                                 const ObStorageSchema *storage_schema,
                                 ObArenaAllocator &allocator,
                                 const ObIArray<ObColumnSchemaItem> &col_array,
                                 const int64_t dir_id,
                                 const int64_t parallelism,
                                 const int64_t max_batch_size)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObChunkBatchSliceStore init twice", KR(ret), KP(this));
  } else if (OB_ISNULL(storage_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null schema", KR(ret), K(*this));
  } else if (OB_UNLIKELY(rowkey_column_count <= 0 || max_batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(rowkey_column_count), K(max_batch_size));
  } else if (FALSE_IT(arena_allocator_ = &allocator)) {  
  } else if (OB_FAIL(prepare_row_ctx(
                 storage_schema, allocator, col_array, dir_id, parallelism, max_batch_size))) {
    LOG_WARN("fail to prepare row store", K(ret));
  } else {
    column_count_ = col_array.count();
    rowkey_column_count_ = rowkey_column_count;
    is_inited_ = true;
  }
  LOG_DEBUG("init chunk batch slice store", KR(ret), KPC(this));
  return ret;
}

int ObChunkBatchSliceStore::prepare_row_ctx(const ObStorageSchema *storage_schema,
    ObIAllocator &allocator,
    const ObIArray<ObColumnSchemaItem> &col_array,
    const int64_t dir_id,
    const int64_t parallelism,
    const int64_t max_batch_size)
{
  int ret = OB_SUCCESS;
  const int64_t chunk_mem_limit = 64 * 1024L; // 64K
  ObCompressorType compressor_type = NONE_COMPRESSOR;
  const int64_t skip_size = ObBitVector::memory_size(max_batch_size);
  void *skip_mem = nullptr;
  if (OB_UNLIKELY(nullptr == storage_schema || col_array.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(storage_schema), K(col_array.count()));
  } else if (FALSE_IT(compressor_type = storage_schema->get_compressor_type())) {
  } else if (OB_FAIL(ObDDLUtil::get_temp_store_compress_type(
                 compressor_type, parallelism, compressor_type))) {
    LOG_WARN("fail to get temp store compress type", KR(ret));
  } else if (OB_ISNULL(row_ctx_ = OB_NEWx(RowStoreCtx, &allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to new row store context", KR(ret));
  } else if (OB_FAIL(ObTempColumnStore::init_vectors(
                 col_array, row_ctx_->allocator_, row_ctx_->vectors_))) {
    LOG_WARN("fail to init vectors", KR(ret), K(col_array));
  } else if (OB_FAIL(row_ctx_->store_.init(row_ctx_->vectors_,
                                           max_batch_size,
                                           ObMemAttr("DL_CK_VEC_STORE"),
                                           chunk_mem_limit,
                                           true/*enable_dump*/,
                                           compressor_type))) {
    LOG_WARN("failed to init temp column store", KR(ret), K(col_array));
  } else if (OB_FAIL(row_ctx_->append_vectors_.prepare_allocate(col_array.count()))) {
    LOG_WARN("fail to prepare allocate", KR(ret), K(col_array.count()));
  } else if (OB_ISNULL(skip_mem = row_ctx_->allocator_.alloc(skip_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc skip buf", KR(ret));
  } else {
    row_ctx_->store_.set_dir_id(dir_id);
    row_ctx_->brs_.skip_ = to_bit_vector(skip_mem);
    row_ctx_->brs_.skip_->reset(max_batch_size);
    row_ctx_->brs_.size_ = 0;
    row_ctx_->brs_.set_all_rows_active(true);
    LOG_INFO("set dir id", K(dir_id));
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(row_ctx_)) {
    row_ctx_->~RowStoreCtx();
    allocator.free(row_ctx_);
    row_ctx_ = nullptr;
  }
  return ret;
}

int ObChunkBatchSliceStore::init_start_key()
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (OB_ISNULL(buf = arena_allocator_->alloc(sizeof(ObStorageDatum) * rowkey_column_count_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc datums", KR(ret), K(rowkey_column_count_));
  } else {
    start_key_.datums_ = new (buf) ObStorageDatum[rowkey_column_count_];
    start_key_.datum_cnt_ = rowkey_column_count_;
  }
  return ret;
}

int ObChunkBatchSliceStore::close()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObChunkBatchSliceStore not init", KR(ret), KP(this));
  } else if (OB_FAIL(row_ctx_->store_.dump(true/*all_dump*/))) {
    LOG_WARN("fail to dump", KR(ret));
  } else if (OB_FAIL(row_ctx_->store_.finish_add_row(true/*need_dump*/))) {
    LOG_WARN("fail to finish add row", KR(ret));
  } else {
    row_ctx_->store_.reset_batch_ctx();
    row_ctx_->append_vectors_.reset();
    row_ctx_->vectors_.reset();
    row_ctx_->brs_.skip_ = nullptr;
    row_ctx_->allocator_.reset();
  }
  LOG_DEBUG("chunk batch slice store closed", KR(ret), K(start_key_));
  return ret;
}

int ObChunkBatchSliceStore::append_batch(const ObBatchDatumRows &datum_rows)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObChunkBatchSliceStore not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(datum_rows.get_column_count() < column_count_ || datum_rows.row_count_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(column_count_), K(datum_rows.get_column_count()), K(datum_rows.row_count_));
  } else {
    int64_t stored_rows_count = 0;
    for (int64_t i = 0; i < column_count_; ++i) {
      row_ctx_->append_vectors_.at(i) = datum_rows.vectors_.at(i);
    }
    row_ctx_->brs_.size_ = datum_rows.row_count_;
    if (OB_FAIL(row_ctx_->store_.add_batch(
            row_ctx_->append_vectors_, row_ctx_->brs_, stored_rows_count))) {
      LOG_WARN("fail to add batch", KR(ret));
    } else if (OB_UNLIKELY(stored_rows_count != datum_rows.row_count_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected rows count", KR(ret), K(stored_rows_count), K(datum_rows.row_count_));
    }
    // save start_key_
    if (OB_SUCC(ret) && 0 == row_cnt_) {
      bool is_null = false;
      const char *payload = nullptr;
      ObLength length = 0;
      ObStorageDatum tmp_datum;
      if (OB_FAIL(init_start_key())) {
        LOG_WARN("fail to init start key", KR(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_column_count_; ++i) {
        ObIVector *vec = datum_rows.vectors_.at(i);
        vec->get_payload(0, is_null, payload, length);
        tmp_datum.shallow_copy_from_datum(ObDatum(payload, length, is_null));
        if (OB_FAIL(start_key_.datums_[i].deep_copy(tmp_datum, *arena_allocator_))) {
          LOG_WARN("fail to deep copy storage datum", KR(ret), K(tmp_datum));
        }
      }
    }
    if (OB_SUCC(ret)) {
      row_cnt_ += datum_rows.row_count_;
    }
  }
  return ret;
}

/**
 * ObMacroBlockSliceStore
 */
int ObMacroBlockSliceStore::init(
    ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
    const blocksstable::ObMacroDataSeq &data_seq,
    const SCN &start_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(nullptr == tablet_direct_load_mgr || !data_seq.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(tablet_direct_load_mgr), K(data_seq));
  } else {
    const ObITable::TableKey &table_key = tablet_direct_load_mgr->get_table_key(); // TODO(cangdi): fix it with right table key
    const int64_t ddl_task_id = tablet_direct_load_mgr->get_ddl_task_id();
    const uint64_t data_format_version = tablet_direct_load_mgr->get_tenant_data_version();
    const ObDirectLoadType direct_load_type = tablet_direct_load_mgr->get_direct_load_type();
    const ObWholeDataStoreDesc &data_desc = tablet_direct_load_mgr->get_data_block_desc();
    ObDDLRedoLogWriterCallbackInitParam init_param;
    init_param.tablet_id_ = table_key.tablet_id_;
    init_param.direct_load_type_ = direct_load_type;
    init_param.table_key_ = table_key;
    init_param.start_scn_ = start_scn;
    init_param.task_id_ = ddl_task_id;
    init_param.data_format_version_ = data_format_version;
    init_param.parallel_cnt_ = tablet_direct_load_mgr->get_task_cnt();
    init_param.block_type_ = tablet_direct_load_mgr->get_is_no_logging() ? DDL_MB_SS_EMPTY_DATA_TYPE : DDL_MB_DATA_TYPE;
    if (OB_ISNULL(ddl_redo_callback_ = OB_NEW(ObDDLRedoLogWriterCallback, ObMemAttr("DDL_MBSS")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret));
    } else if (OB_FAIL(static_cast<ObDDLRedoLogWriterCallback *>(ddl_redo_callback_)->init(init_param))) {
      LOG_WARN("fail to init ddl_redo_callback_", K(ret), K(init_param));
    }
    if (OB_SUCC(ret)) {
      ObMacroSeqParam macro_seq_param;
      macro_seq_param.seq_type_ = ObMacroSeqParam::SEQ_TYPE_INC;
      macro_seq_param.start_ = data_seq.macro_data_seq_;
      ObPreWarmerParam pre_warm_param;
      ObSSTablePrivateObjectCleaner *object_cleaner = nullptr;
      if (OB_FAIL(pre_warm_param.init(table_key.tablet_id_))) {
        LOG_WARN("failed to init pre warm param", K(ret), "tablet_id", table_key.tablet_id_);
      } else if (OB_FAIL(ObSSTablePrivateObjectCleaner::get_cleaner_from_data_store_desc(
                                 tablet_direct_load_mgr->get_data_block_desc().get_desc(),
                                 object_cleaner))) {
        LOG_WARN("failed to get cleaner from data store desc", K(ret));
      } else if (OB_FAIL(macro_block_writer_.open(
                     data_desc.get_desc(), data_seq.get_parallel_idx(),
                     macro_seq_param, pre_warm_param, *object_cleaner,
                     ddl_redo_callback_))) {
        LOG_WARN("open macro bock writer failed", K(ret), K(macro_seq_param), KPC(object_cleaner));
      } else {
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObMacroBlockSliceStore::append_row(const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(macro_block_writer_.append_row(datum_row))) {
    LOG_WARN("macro block writer append row failed", K(ret), K(datum_row));
  }
  return ret;
}

int ObMacroBlockSliceStore::append_batch(const blocksstable::ObBatchDatumRows &datum_rows)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObMacroBlockSliceStore not init", KR(ret), KP(this));
  } else if (OB_FAIL(macro_block_writer_.append_batch(datum_rows))) {
    LOG_WARN("macro block writer append batch failed", K(ret));
  }
  return ret;
}

int ObMacroBlockSliceStore::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(macro_block_writer_.close())) {
    LOG_WARN("close macro block writer failed", K(ret));
  }
  return ret;
}

bool ObTabletDDLParam::is_valid() const
{
  return is_valid_direct_load(direct_load_type_)
    && table_key_.is_valid()
    && start_scn_.is_valid_and_not_min()
    && commit_scn_.is_valid() && commit_scn_ != SCN::max_scn()
    && snapshot_version_ > 0
    && data_format_version_ > 0;
}

ObDirectLoadSliceWriter::ObDirectLoadSliceWriter()
  : is_inited_(false), is_canceled_(false), start_seq_(), slice_idx_(0), merge_slice_idx_(0), tablet_direct_load_mgr_(nullptr),
    slice_store_(nullptr), meta_write_iter_(nullptr), row_iterator_(nullptr), 
    allocator_(lib::ObLabel("SliceWriter"), OB_MALLOC_NORMAL_BLOCK_SIZE), 
    lob_allocator_(nullptr), rowkey_lengths_()
{
}

void ObDirectLoadSliceWriter::reset()
{
  ObDirectLoadSliceWriter::~ObDirectLoadSliceWriter();
  is_inited_ = false;
  is_canceled_ = false;
  start_seq_ = ObMacroDataSeq();
  tablet_direct_load_mgr_ = nullptr;
  slice_store_ = nullptr;
  meta_write_iter_ = nullptr;
  row_iterator_ = nullptr;
}

ObDirectLoadSliceWriter::~ObDirectLoadSliceWriter()
{
  if (nullptr != slice_store_) {
    slice_store_->~ObTabletSliceStore();
    allocator_.free(slice_store_);
    slice_store_ = nullptr;
  }
  if (nullptr != meta_write_iter_) {
    meta_write_iter_->~ObLobMetaWriteIter();
    allocator_.free(meta_write_iter_);
    meta_write_iter_ = nullptr;
  }
  if (nullptr != row_iterator_) {
    row_iterator_->~ObLobMetaRowIterator();
    allocator_.free(row_iterator_);
    row_iterator_ = nullptr;
  }
  if (nullptr != lob_allocator_) {
    lob_allocator_->reset();
    allocator_.free(lob_allocator_);
    lob_allocator_= nullptr;
  }
  allocator_.reset();
  rowkey_lengths_.destroy();
}

//for test
int ObDirectLoadSliceWriter::mock_chunk_store(const int64_t row_cnt)
{
  int ret = OB_SUCCESS;
  if (row_cnt < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid row cnt", K(ret), K(row_cnt));
  } else {
    ObChunkSliceStore *chunk_slice_store = nullptr;
    if (OB_ISNULL(chunk_slice_store = OB_NEWx(ObChunkSliceStore, &allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory for chunk slice store failed", K(ret));
    } else {
      chunk_slice_store->row_cnt_ = row_cnt;
      slice_store_ = chunk_slice_store;

    }
    if (OB_FAIL(ret) && nullptr != chunk_slice_store) {
      chunk_slice_store->~ObChunkSliceStore();
      allocator_.free(chunk_slice_store);
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::prepare_vector_slice_store(
    const ObStorageSchema *storage_schema,
    const ObString vec_idx_param,
    const int64_t vec_dim,
    const int64_t context_id)
{
  int ret = OB_SUCCESS;
  ObVectorIndexBaseSliceStore *vec_idx_slice_store = nullptr;
  if (OB_ISNULL(storage_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null schema", K(ret), K(*this));
  } else if (schema::is_vec_index_snapshot_data_type(storage_schema->get_index_type()) &&
              OB_ISNULL(vec_idx_slice_store = OB_NEWx(ObVectorIndexSliceStore, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory for chunk slice store failed", K(ret));
  } else if (schema::is_local_vec_ivf_centroid_index(storage_schema->get_index_type())
            && OB_ISNULL(vec_idx_slice_store = OB_NEWx(ObIvfCenterSliceStore, &allocator_))) {
    // NOTE(liyao): pq/sq8/flat use same centroid index
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory for chunk slice store failed", K(ret));
  } else if (schema::is_vec_ivfsq8_meta_index(storage_schema->get_index_type())
          && OB_ISNULL(vec_idx_slice_store = OB_NEWx(ObIvfSq8MetaSliceStore, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory for chunk slice store failed", K(ret));
  } else if (schema::is_vec_ivfpq_pq_centroid_index(storage_schema->get_index_type())
          && OB_ISNULL(vec_idx_slice_store = OB_NEWx(ObIvfPqSliceStore, &allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate memory for chunk slice store failed", K(ret));
  } else if (OB_ISNULL(vec_idx_slice_store)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid index type with sclice store", K(ret), K(storage_schema->get_index_type()));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(vec_idx_slice_store->init(tablet_direct_load_mgr_, vec_idx_param, vec_dim,
                                                tablet_direct_load_mgr_->get_column_info(),
                                                context_id))) {
    LOG_WARN("init vector index slice store failed", K(ret), KPC(storage_schema));
  } else {
    slice_store_ = vec_idx_slice_store;
  }
  if (OB_FAIL(ret) && nullptr != vec_idx_slice_store) {
    if (schema::is_vec_index_snapshot_data_type(storage_schema->get_index_type())) {
      ObVectorIndexSliceStore *slice_store = nullptr;
      if (OB_NOT_NULL(slice_store = dynamic_cast<ObVectorIndexSliceStore*>(vec_idx_slice_store))) {
        slice_store->~ObVectorIndexSliceStore();
      }
    } else if (schema::is_local_vec_ivf_centroid_index(storage_schema->get_index_type())) {
      ObIvfCenterSliceStore *slice_store = nullptr;
      if (OB_NOT_NULL(slice_store = dynamic_cast<ObIvfCenterSliceStore*>(vec_idx_slice_store))) {
        slice_store->~ObIvfCenterSliceStore();
      }
    } else if (schema::is_vec_ivfsq8_meta_index(storage_schema->get_index_type())) {
      ObIvfSq8MetaSliceStore *slice_store = nullptr;
      if (OB_NOT_NULL(slice_store = dynamic_cast<ObIvfSq8MetaSliceStore*>(vec_idx_slice_store))) {
        slice_store->~ObIvfSq8MetaSliceStore();
      }
    } else if (schema::is_vec_ivfpq_pq_centroid_index(storage_schema->get_index_type())) {
      ObIvfPqSliceStore *slice_store = nullptr;
      if (OB_NOT_NULL(slice_store = dynamic_cast<ObIvfPqSliceStore*>(vec_idx_slice_store))) {
        slice_store->~ObIvfPqSliceStore();
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid index type with sclice store", K(ret), K(storage_schema->get_index_type()));
    }
    allocator_.free(vec_idx_slice_store);
    {  // what ever fail or success, we need to release ivf build helper
      int tmp_ret = OB_SUCCESS;
      // is tablet_direct_load_mgr_ is null, no need to erase ivf_build_helper
      if (OB_NOT_NULL(tablet_direct_load_mgr_)) {
        ObIvfHelperKey key(tablet_direct_load_mgr_->get_tablet_id(), context_id);
        if (OB_TMP_FAIL(ObPluginVectorIndexUtils::erase_ivf_build_helper(key))) {
          LOG_WARN("failed to erase ivf build helper", K(tmp_ret),
                   K(tablet_direct_load_mgr_->get_tablet_id()));
        }
        if (tmp_ret != OB_SUCCESS && tmp_ret != OB_HASH_NOT_EXIST) {
          ret = ret != OB_SUCCESS ? ret : tmp_ret;
        }
      }
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::prepare_slice_store_if_need(
    const ObStorageSchema *storage_schema, 
    const SCN &start_scn,
    const ObString vec_idx_param,
    const int64_t vec_dim,
    const int64_t context_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (nullptr != slice_store_) {
    // do nothing
  } else if (is_full_direct_load(tablet_direct_load_mgr_->get_direct_load_type()) && 
             OB_NOT_NULL(storage_schema) &&
             (schema::is_vec_index_snapshot_data_type(storage_schema->get_index_type()) ||
              schema::is_local_vec_ivf_centroid_index(storage_schema->get_index_type()) ||
              schema::is_vec_ivfsq8_meta_index(storage_schema->get_index_type()) ||
              schema::is_vec_ivfpq_pq_centroid_index(storage_schema->get_index_type()))) {
    if (OB_FAIL(prepare_vector_slice_store(storage_schema, vec_idx_param, vec_dim, context_id))) {
      LOG_WARN("failed to prepare vector slice_store", K(ret));
    }
  } else {
    ObMacroBlockSliceStore *macro_block_slice_store = nullptr;
    if (OB_ISNULL(macro_block_slice_store = OB_NEWx(ObMacroBlockSliceStore, &allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory for macro block slice store failed", K(ret));
    } else if (OB_FAIL(macro_block_slice_store->init(tablet_direct_load_mgr_, start_seq_, start_scn))) {
      LOG_WARN("init macro block slice store failed", K(ret), KPC(tablet_direct_load_mgr_), K(start_seq_));
    } else {
      slice_store_ = macro_block_slice_store;
    }
    if (OB_FAIL(ret) && nullptr != macro_block_slice_store) {
      macro_block_slice_store->~ObMacroBlockSliceStore();
      allocator_.free(macro_block_slice_store);
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::init(
    ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
    const blocksstable::ObMacroDataSeq &start_seq,
    const int64_t slice_idx,
    const int64_t merge_slice_idx)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(nullptr == tablet_direct_load_mgr || !start_seq.is_valid() || slice_idx < 0 || merge_slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KPC(tablet_direct_load_mgr), K(start_seq), K(slice_idx), K(merge_slice_idx));
  } else {
    tablet_direct_load_mgr_ = tablet_direct_load_mgr;
    start_seq_ = start_seq;
    slice_idx_ = slice_idx;
    merge_slice_idx_ = merge_slice_idx;
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadSliceWriter::prepare_iters(
    ObIAllocator &allocator,
    ObIAllocator &iter_allocator,
    blocksstable::ObStorageDatum &datum,
    const ObTabletID &tablet_id,
    const int64_t trans_version,
    const ObObjType &obj_type,
    const ObCollationType &cs_type,
    const int64_t timeout_ts,
    const ObLobStorageParam &lob_storage_param,
    share::ObTabletCacheInterval &pk_interval,
    ObLobMetaRowIterator *&row_iter)
{
  int ret = OB_SUCCESS;
  row_iter = nullptr;

  if (OB_ISNULL(lob_allocator_)) {
    void *buf = nullptr;
    if (OB_ISNULL(buf = allocator_.alloc(sizeof(common::ObArenaAllocator)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc lob allocator failed", K(ret));
    } else {
      lob_allocator_ = new (buf) common::ObArenaAllocator("LobWriter", OB_MALLOC_NORMAL_BLOCK_SIZE);
    }
  }

  if (OB_SUCC(ret) && OB_ISNULL(meta_write_iter_)) {
    void *buf = nullptr;
    if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObLobMetaWriteIter)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc lob meta write iter failed", K(ret));
    } else {
      // keep allocator is same as insert_lob_column
      meta_write_iter_ = new (buf) ObLobMetaWriteIter(lob_allocator_, ObLobMetaUtil::LOB_OPER_PIECE_DATA_SIZE);
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(row_iterator_)) {
      void *buf = nullptr;
      if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObLobMetaRowIterator)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc lob meta row iter failed", K(ret));
      } else {
        row_iterator_ = new (buf) ObLobMetaRowIterator();
      }
    }
  }
  if (OB_SUCC(ret)) {
    int64_t unused_affected_rows = 0;
    if (OB_FAIL(ObInsertLobColumnHelper::insert_lob_column(
        allocator, *lob_allocator_, nullptr, pk_interval, tablet_id/* tablet id of main table */, tablet_direct_load_mgr_->get_tablet_id()/*tablet id of lob meta table*/,
        obj_type, cs_type, lob_storage_param, datum, timeout_ts, true/*has_lob_header*/, *meta_write_iter_))) {
      LOG_WARN("fail to insert_lob_col", K(ret), K(tablet_id));
    } else if (OB_FAIL(row_iterator_->init(meta_write_iter_, trans_version))) {
      LOG_WARN("fail to lob meta row iterator", K(ret), K(trans_version));
    } else {
      row_iter = row_iterator_;
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::fill_lob_sstable_slice(
    const uint64_t table_id,
    ObIAllocator &allocator,
    ObIAllocator &iter_allocator,
    const SCN &start_scn,
    const ObBatchSliceWriteInfo &info,
    share::ObTabletCacheInterval &pk_interval,
    const ObArray<int64_t> &lob_column_idxs,
    const ObArray<common::ObObjMeta> &col_types,
    const ObTableSchemaItem &schema_item,
    blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  const uint64_t data_format_version = tablet_direct_load_mgr_->get_tenant_data_version();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadSliceWriter not init", KR(ret), KP(this));
  } else {
    ObLobStorageParam lob_storage_param;
    lob_storage_param.inrow_threshold_ = schema_item.lob_inrow_threshold_;
    lob_storage_param.is_index_table_ = schema_item.is_index_table_;
    for (int64_t i = 0; OB_SUCC(ret) && i < lob_column_idxs.count(); i++) {
      const int64_t idx = lob_column_idxs.at(i);
      const ObObjMeta &col_type = col_types.at(i);
      ObStorageDatum &datum = datum_row.storage_datums_[idx];
      lob_storage_param.is_rowkey_col_ = idx < schema_item.rowkey_column_num_;
      if (OB_FAIL(fill_lob_into_macro_block(allocator, iter_allocator, start_scn, info,
          pk_interval, col_type, lob_storage_param, datum))) {
        LOG_WARN("fill lob into macro block failed", K(ret), K(data_format_version));
      }
    }
  } 
  return ret;
}

int ObDirectLoadSliceWriter::fill_lob_into_memtable(
    ObIAllocator &allocator,
    const ObBatchSliceWriteInfo &info,
    const common::ObObjMeta &col_type,
    const ObLobStorageParam &lob_storage_param,
    blocksstable::ObStorageDatum &datum)
{
  // to insert lob data into memtable.
  int ret = OB_SUCCESS;
  const int64_t timeout_ts = ObTimeUtility::fast_current_time() + ObInsertLobColumnHelper::LOB_ACCESS_TX_TIMEOUT;
  if (OB_FAIL(ObInsertLobColumnHelper::insert_lob_column(
    allocator, info.data_tablet_id_, col_type.get_type(), col_type.get_collation_type(),
    lob_storage_param, datum, timeout_ts, true/*has_lob_header*/))) {
    LOG_WARN("fail to insert_lob_col", K(ret), K(datum));
  }
  return ret;
}

int ObDirectLoadSliceWriter::fill_lob_into_macro_block(
    ObIAllocator &allocator,
    ObIAllocator &iter_allocator,
    const SCN &start_scn,
    const ObBatchSliceWriteInfo &info,
    share::ObTabletCacheInterval &pk_interval,
    const common::ObObjMeta &col_type,
    const ObLobStorageParam &lob_storage_param,
    blocksstable::ObStorageDatum &datum)
{
  // to insert lob data into macro block.
  int ret = OB_SUCCESS;
  int64_t unused_affected_rows = 0;
  const int64_t timeout_ts = ObTimeUtility::fast_current_time() + ObInsertLobColumnHelper::LOB_ACCESS_TX_TIMEOUT;
  if (!datum.is_nop() && !datum.is_null()) {
    {
      ObLobMetaRowIterator *row_iter = nullptr;
      if (OB_FAIL(prepare_iters(allocator, iter_allocator, datum,
          info.data_tablet_id_, info.trans_version_, col_type.get_type(), col_type.get_collation_type(),
          timeout_ts, lob_storage_param, pk_interval, row_iter))) {
        LOG_WARN("fail to prepare iters", K(ret), KP(row_iter), K(datum));
      } else {
        while (OB_SUCC(ret)) {
          const blocksstable::ObDatumRow *cur_row = nullptr;
          if (OB_FAIL(THIS_WORKER.check_status())) {
            LOG_WARN("check status failed", K(ret));
          } else if (ATOMIC_LOAD(&is_canceled_)) {
            ret = OB_CANCELED;
            LOG_WARN("fil lob task canceled", K(ret), K(is_canceled_));
          } else if (OB_FAIL(row_iter->get_next_row(cur_row))) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
              break;
            } else {
              LOG_WARN("get next row failed", K(ret));
            }
          } else if (OB_ISNULL(cur_row) || !cur_row->is_valid()) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid args", KR(ret), KPC(cur_row));
          } else if (OB_FAIL(check_null_and_length(false/*is_index_table*/, false/*has_lob_rowkey*/, 
                                                   ObLobMetaUtil::LOB_META_SCHEMA_ROWKEY_COL_CNT, *cur_row))) {
            LOG_WARN("fail to check rowkey null value and length in row", KR(ret), KPC(cur_row));
          } else if (OB_FAIL(prepare_slice_store_if_need(nullptr /*storage_schema*/, start_scn,
              ObString()/*unsued*/, 0/*unsued*/, 0/*unsued*/))) {
            LOG_WARN("prepare macro block writer failed", K(ret));
          } else if (OB_FAIL(slice_store_->append_row(*cur_row))) {
            LOG_WARN("macro block writer append row failed", K(ret), KPC(cur_row));
          }
          if (OB_SUCC(ret)) {
            ++unused_affected_rows;
            LOG_DEBUG("sstable insert op append row", K(unused_affected_rows), KPC(cur_row));
          }
        }
        if (OB_SUCC(ret) && OB_NOT_NULL(meta_write_iter_) && OB_FAIL(meta_write_iter_->check_write_length())) {
          LOG_WARN("check_write_length fail", K(ret), KPC(meta_write_iter_));
        }
        if (OB_SUCC(ret)) {
          if (OB_NOT_NULL(meta_write_iter_)) {
            meta_write_iter_->reuse();
          }
          if (OB_NOT_NULL(row_iterator_)) {
            row_iterator_->reuse();
          }
          if (OB_NOT_NULL(lob_allocator_)) {
            lob_allocator_->reuse();
          }
        }
      }
    }
  }
  return ret;
}

static bool fast_check_vector_is_all_null(ObIVector *vector, const int64_t batch_size)
{
  bool is_all_null = false;
  VectorFormat format = vector->get_format();
  switch (format) {
    case VEC_FIXED:
    case VEC_DISCRETE:
    case VEC_CONTINUOUS:
      is_all_null = static_cast<ObBitmapNullVectorBase *>(vector)->get_nulls()->is_all_true(batch_size);
      break;
    default:
      break;
  }
  return is_all_null;
}

static int new_discrete_vector(VecValueTypeClass value_tc,
                               const int64_t max_batch_size,
                               ObIAllocator &allocator,
                               ObDiscreteBase *&result_vec)
{
  int ret = OB_SUCCESS;
  result_vec = nullptr;
  ObIVector *vector = nullptr;
  switch (value_tc) {
#define DISCRETE_VECTOR_INIT_SWITCH(value_tc)                           \
  case value_tc: {                                                      \
    using VecType = RTVectorType<VEC_DISCRETE, value_tc>;               \
    static_assert(sizeof(VecType) <= ObIVector::MAX_VECTOR_STRUCT_SIZE, \
                  "vector size exceeds MAX_VECTOR_STRUCT_SIZE");        \
    vector = OB_NEWx(VecType, &allocator, nullptr, nullptr, nullptr);   \
    break;                                                              \
  }
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_NUMBER);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_EXTEND);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_STRING);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET_INNER);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_RAW);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ROWID);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_LOB);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_JSON);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_GEO);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_UDT);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_COLLECTION);
#undef DISCRETE_VECTOR_INIT_SWITCH
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected discrete vector value type class", KR(ret), K(value_tc));
      break;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(vector)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc vecttor", KR(ret));
  } else {
    ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
    const int64_t nulls_size = ObBitVector::memory_size(max_batch_size);
    const int64_t lens_size = sizeof(int32_t) * max_batch_size;
    const int64_t ptrs_size = sizeof(char *) * max_batch_size;
    ObBitVector *nulls = nullptr;
    int32_t *lens = nullptr;
    char **ptrs = nullptr;
    if (OB_ISNULL(nulls = to_bit_vector(allocator.alloc(nulls_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc mem", KR(ret), K(nulls_size));
    } else if (OB_ISNULL(lens = static_cast<int32_t *>(allocator.alloc(lens_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc mem", KR(ret), K(lens_size));
    } else if (OB_ISNULL(ptrs = static_cast<char **>(allocator.alloc(ptrs_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc mem", KR(ret), K(ptrs_size));
    } else {
      nulls->reset(max_batch_size);
      discrete_vec->set_nulls(nulls);
      discrete_vec->set_lens(lens);
      discrete_vec->set_ptrs(ptrs);
      result_vec = discrete_vec;
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::fill_lob_sstable_slice(
    const uint64_t table_id,
    ObIAllocator &allocator,
    ObIAllocator &iter_allocator,
    const SCN &start_scn,
    const ObBatchSliceWriteInfo &info,
    share::ObTabletCacheInterval &pk_interval,
    const ObArray<int64_t> &lob_column_idxs,
    const ObArray<common::ObObjMeta> &col_types,
    const ObTableSchemaItem &schema_item,
    blocksstable::ObBatchDatumRows &datum_rows)
{
  int ret = OB_SUCCESS;
  const uint64_t data_format_version = tablet_direct_load_mgr_->get_tenant_data_version();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadSliceWriter not init", KR(ret), KP(this));
  } else {
    ObStorageDatum temp_datum;
    ObLobStorageParam lob_storage_param;
    lob_storage_param.inrow_threshold_ = schema_item.lob_inrow_threshold_;
    lob_storage_param.is_index_table_ = schema_item.is_index_table_;
    for (int64_t i = 0; OB_SUCC(ret) && i < lob_column_idxs.count(); i++) {
      const int64_t idx = lob_column_idxs.at(i);
      const ObObjMeta &col_type = col_types.at(i);
      ObIVector *vector = datum_rows.vectors_.at(idx);
      const VectorFormat format = vector->get_format();
      lob_storage_param.is_rowkey_col_ = idx < schema_item.rowkey_column_num_;
      if (fast_check_vector_is_all_null(vector, datum_rows.row_count_)) {
        // do nothing
        continue;
      }
      switch (format) {
        case VEC_CONTINUOUS:
        {
          ObContinuousBase *continuous_vec = static_cast<ObContinuousBase *>(vector);
          ObDiscreteBase *discrete_vec = nullptr;
          char *data = continuous_vec->get_data();
          uint32_t *offsets = continuous_vec->get_offsets();
          char **ptrs = nullptr;
          ObLength *lens = nullptr;
          VecValueTypeClass value_tc = get_vec_value_tc(col_type.get_type(),
                                                        col_type.get_scale(),
                                                        PRECISION_UNKNOWN_YET);
          if (OB_FAIL(new_discrete_vector(value_tc, datum_rows.row_count_, allocator, discrete_vec))) {
            LOG_WARN("fail to new discrete vector", KR(ret));
          } else {
            ptrs = discrete_vec->get_ptrs();
            lens = discrete_vec->get_lens();
          }
          for (int64_t j = 0; OB_SUCC(ret) && j < datum_rows.row_count_; ++j) {
            if (continuous_vec->is_null(j)) {
              discrete_vec->set_null(j);
            } else {
              temp_datum.ptr_ = data + offsets[j];
              temp_datum.len_ = offsets[j + 1] - offsets[j];
              if (OB_FAIL(fill_lob_into_macro_block(allocator, iter_allocator, start_scn, info,
                  pk_interval, col_type, lob_storage_param, temp_datum))) {
                LOG_WARN("fill lob into macro block failed", K(ret), K(data_format_version));
              }
              if (OB_SUCC(ret)) {
                ptrs[j] = const_cast<char *>(temp_datum.ptr_);
                lens[j] = temp_datum.len_;
              }
            }
          }
          if (OB_SUCC(ret)) {
            datum_rows.vectors_.at(idx) = discrete_vec;
          }
          break;
        }
        case VEC_DISCRETE:
        {
          ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
          char **ptrs = discrete_vec->get_ptrs();
          ObLength *lens =discrete_vec->get_lens();
          for (int64_t j = 0; OB_SUCC(ret) && j < datum_rows.row_count_; ++j) {
            if (!discrete_vec->is_null(j)) {
              temp_datum.ptr_ = ptrs[j];
              temp_datum.len_ = lens[j];
              if (OB_FAIL(fill_lob_into_macro_block(allocator, iter_allocator, start_scn, info,
                pk_interval, col_type, lob_storage_param, temp_datum))) {
                LOG_WARN("fill lob into macro block failed", K(ret), K(data_format_version));
              }
              if (OB_SUCC(ret)) {
                ptrs[j] = const_cast<char *>(temp_datum.ptr_);
                lens[j] = temp_datum.len_;
              }
            }
          }
          break;
        }
        case VEC_UNIFORM:
        {
          ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
          ObDatum *datums = uniform_vec->get_datums();
          for (int64_t j = 0; OB_SUCC(ret) && j < datum_rows.row_count_; ++j) {
            ObDatum &datum = datums[j];
            if (!datum.is_null()) {
              temp_datum.ptr_ = datum.ptr_;
              temp_datum.len_ = datum.len_;
              if (OB_FAIL(fill_lob_into_macro_block(allocator, iter_allocator, start_scn, info,
                  pk_interval, col_type, lob_storage_param, temp_datum))) {
                LOG_WARN("fill lob into macro block failed", K(ret), K(data_format_version));
              }
              if (OB_SUCC(ret)) {
                datum.ptr_ = temp_datum.ptr_;
                datum.len_ = temp_datum.len_;
              }
            }
          }
          break;
        }
        default:
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected lob vector format", KR(ret), K(i), K(format));
          break;
      }
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::fill_sstable_slice(
    const SCN &start_scn,
    const uint64_t table_id,
    const ObTabletID &tablet_id,
    const ObStorageSchema *storage_schema, 
    ObIStoreRowIterator *row_iter,
    const ObTableSchemaItem &schema_item,
    const ObDirectLoadType &direct_load_type,
    const ObArray<ObColumnSchemaItem> &column_items,
    const int64_t dir_id,
    const int64_t parallelism,
    const int64_t context_id,
    int64_t &affected_rows,
    ObInsertMonitor *insert_monitor)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  const bool is_full_direct_load_task = is_full_direct_load(direct_load_type);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadSliceWriter not init", KR(ret), KP(this));
  } else if (OB_ISNULL(storage_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null schema", K(ret), K(*this));
  } else {
    ObArenaAllocator arena("SliceW_sst", OB_MALLOC_NORMAL_BLOCK_SIZE);
    const ObDataStoreDesc &data_desc = tablet_direct_load_mgr_->get_data_block_desc().get_desc();

    while (OB_SUCC(ret)) {
      arena.reuse();
      const blocksstable::ObDatumRow *cur_row = nullptr;
      if (OB_FAIL(share::dag_yield())) {
        LOG_WARN("dag yield failed", K(ret), K(affected_rows)); // exit for dag task as soon as possible after canceled.
      } else if (OB_FAIL(THIS_WORKER.check_status())) {
        LOG_WARN("check status failed", K(ret));
      } else if (ATOMIC_LOAD(&is_canceled_)) {
        ret = OB_CANCELED;
        LOG_WARN("fil sstable task canceled", K(ret), K(is_canceled_));
      } else if (OB_FAIL(row_iter->get_next_row(cur_row))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("get next row failed", K(ret));
        }
      } else if (OB_ISNULL(cur_row) || !cur_row->is_valid() || cur_row->get_column_count() != data_desc.get_col_desc_array().count()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid args", KR(ret), KPC(cur_row), K(data_desc.get_col_desc_array()));
      } else { // row reshape
        for (int64_t i = 0; OB_SUCC(ret) && i < cur_row->get_column_count(); ++i) {
          const ObColDesc &col_desc = data_desc.get_col_desc_array().at(i);
          ObStorageDatum &datum_cell = cur_row->storage_datums_[i];
          if (i >= schema_item.rowkey_column_num_ && i < schema_item.rowkey_column_num_ + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()) {
            // skip multi version column
          } else if (datum_cell.is_null() || datum_cell.is_nop()) {
            //ignore null
          } else if (OB_UNLIKELY(i >= column_items.count()) || OB_UNLIKELY(!column_items.at(i).is_valid_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("column schema is wrong", K(ret), K(i), K(column_items));
          } else if (OB_FAIL(ObDASUtils::reshape_datum_value(column_items.at(i).col_type_, column_items.at(i).col_accuracy_, arena, datum_cell))) {
            LOG_WARN("reshape storage datum failed", K(ret));
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(check_null_and_length(schema_item.is_index_table_, schema_item.has_lob_rowkey_, 
                                               schema_item.rowkey_column_num_, *cur_row))) {
        LOG_WARN("fail to check rowkey null value and length in row", KR(ret), KPC(cur_row));
      } else if (OB_FAIL(prepare_slice_store_if_need(storage_schema,
                                                     start_scn,
                                                     schema_item.vec_idx_param_,
                                                     schema_item.vec_dim_,
                                                     context_id))) {
        LOG_WARN("prepare macro block writer failed", K(ret));
      } else if (OB_FAIL(slice_store_->append_row(*cur_row))) {
        if (is_full_direct_load_task && OB_ERR_PRIMARY_KEY_DUPLICATE == ret && schema_item.is_unique_index_) {
          int report_ret_code = OB_SUCCESS;
          LOG_USER_ERROR(OB_ERR_PRIMARY_KEY_DUPLICATE, "", static_cast<int>(sizeof("UNIQUE IDX") - 1), "UNIQUE IDX");
          (void) report_unique_key_dumplicated(ret, table_id, *cur_row, tablet_direct_load_mgr_->get_tablet_id(), report_ret_code); // ignore ret
          if (OB_ERR_DUPLICATED_UNIQUE_KEY == report_ret_code) {
            // Report direct-load unique index conflicts with the dedicated duplicate-key code.
            ret = OB_ERR_DUPLICATED_UNIQUE_KEY;
          }
        } else {
          LOG_WARN("macro block writer append row failed", K(ret), KPC(cur_row));
        }
      }
      if (OB_SUCC(ret)) {
        ++affected_rows;
        LOG_DEBUG("sstable insert op append row", KPC(cur_row));
        if ((affected_rows % 100 == 0) && OB_NOT_NULL(insert_monitor)) {
          (void) ATOMIC_AAF(&insert_monitor->scanned_row_cnt_, 100);
          (void) ATOMIC_AAF(&insert_monitor->inserted_row_cnt_, 100);
        } 
      }
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(insert_monitor)) {
      (void) ATOMIC_AAF(&insert_monitor->scanned_row_cnt_, affected_rows % 100);
      (void) ATOMIC_AAF(&insert_monitor->inserted_row_cnt_, affected_rows % 100);
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::fill_sstable_slice(
    const SCN &start_scn,
    const uint64_t table_id,
    const ObTabletID &tablet_id,
    const ObStorageSchema *storage_schema, 
    const blocksstable::ObBatchDatumRows &datum_rows,
    const ObTableSchemaItem &schema_item,
    const ObDirectLoadType &direct_load_type,
    const ObArray<ObColumnSchemaItem> &column_items,
    const int64_t dir_id,
    const int64_t parallelism,
    const int64_t context_id,
    ObInsertMonitor *insert_monitor)
{
  int ret = OB_SUCCESS;
  const bool is_full_direct_load_task = is_full_direct_load(direct_load_type);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadSliceWriter not init", KR(ret), KP(this));
  } else if (OB_ISNULL(storage_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("null schema", K(ret), K(*this));
  } else if (OB_UNLIKELY(ATOMIC_LOAD(&is_canceled_))) {
    ret = OB_CANCELED;
    LOG_WARN("fil sstable task canceled", K(ret), K(is_canceled_));
  } else {
    ObArenaAllocator arena("SliceW_sst", OB_MALLOC_NORMAL_BLOCK_SIZE);
    const ObDataStoreDesc &data_desc = tablet_direct_load_mgr_->get_data_block_desc().get_desc();
    if (OB_UNLIKELY(datum_rows.get_column_count() != data_desc.get_col_desc_array().count())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid args", KR(ret), K(datum_rows.get_column_count()), K(data_desc.get_col_desc_array()));
    } else { // row reshape
      ObBatchSelector selector(static_cast<int64_t>(0), datum_rows.row_count_);
      for (int64_t i = 0; OB_SUCC(ret) && i < datum_rows.get_column_count(); ++i) {
        const ObColDesc &col_desc = data_desc.get_col_desc_array().at(i);
        ObIVector *vector = datum_rows.vectors_.at(i);
        selector.rescan();
        if (i >= schema_item.rowkey_column_num_ && i < schema_item.rowkey_column_num_ + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()) {
          // skip multi version column
        } else if (OB_UNLIKELY(i >= column_items.count()) || OB_UNLIKELY(!column_items.at(i).is_valid_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("column schema is wrong", K(ret), K(i), K(column_items));
        } else if (OB_FAIL(ObDASUtils::reshape_vector_value(column_items.at(i).col_type_,
                                                            column_items.at(i).col_accuracy_,
                                                            arena,
                                                            vector,
                                                            selector))) {
          LOG_WARN("fail to reshape vector value", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(check_null_and_length(schema_item.is_index_table_, schema_item.has_lob_rowkey_, 
                                             schema_item.rowkey_column_num_, datum_rows))) {
      LOG_WARN("fail to check rowkey null value and length in row", KR(ret));
    } else if (OB_FAIL(prepare_slice_store_if_need(storage_schema,
                                                   start_scn,
                                                   schema_item.vec_idx_param_,
                                                   schema_item.vec_dim_,
                                                   context_id))) {
      LOG_WARN("prepare macro block writer failed", K(ret));
    } else if (OB_FAIL(slice_store_->append_batch(datum_rows))) {
      if (is_full_direct_load_task && OB_ERR_PRIMARY_KEY_DUPLICATE == ret && schema_item.is_unique_index_) {
        int report_ret_code = OB_SUCCESS;
        LOG_USER_ERROR(OB_ERR_PRIMARY_KEY_DUPLICATE, "", static_cast<int>(sizeof("UNIQUE IDX") - 1), "UNIQUE IDX");
        (void) report_unique_key_dumplicated(ret, table_id, datum_rows, tablet_direct_load_mgr_->get_tablet_id(), report_ret_code); // ignore ret
        if (OB_ERR_DUPLICATED_UNIQUE_KEY == report_ret_code) {
          // Report direct-load unique index conflicts with the dedicated duplicate-key code.
          ret = OB_ERR_DUPLICATED_UNIQUE_KEY;
        }
      } else {
        LOG_WARN("macro block writer append batch failed", K(ret));
      }
    } else {
      LOG_DEBUG("sstable insert op append batch", K(datum_rows.row_count_));
      if (OB_NOT_NULL(insert_monitor)) {
        (void) ATOMIC_AAF(&insert_monitor->scanned_row_cnt_, datum_rows.row_count_);
        (void) ATOMIC_AAF(&insert_monitor->inserted_row_cnt_, datum_rows.row_count_);
      }
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::report_unique_key_dumplicated(
    const int ret_code, const uint64_t table_id, const ObDatumRow &datum_row,
    const ObTabletID &tablet_id, int &report_ret_code)
{
  int ret = OB_SUCCESS;
  report_ret_code = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
          schema_guard))) {
    LOG_WARN("get tenant schema failed", K(ret), K(table_id), K(table_id));
  } else if (OB_FAIL(schema_guard.get_table_schema(
          table_id, table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(table_id));
  } else {
    const int64_t rowkey_column_num = table_schema->get_rowkey_column_num();
    char index_key_buffer[OB_TMP_BUF_SIZE_256] = { 0 };
    int64_t task_id = 0;
    ObDatumRowkey index_key;
    ObDDLErrorMessageTableOperator::ObDDLErrorInfo error_info;
    index_key.assign(datum_row.storage_datums_, rowkey_column_num);
    if (OB_FAIL(ObDDLStorageUtil::extract_index_key(*table_schema, index_key, index_key_buffer, OB_TMP_BUF_SIZE_256))) {   // read the unique key that violates the unique constraint
      LOG_WARN("extract unique index key failed", K(ret), K(index_key), K(index_key_buffer));
    } else if (OB_FAIL(ObDDLErrorMessageTableOperator::get_index_task_info(*GCTX.sql_proxy_, *table_schema, error_info))) {
      LOG_WARN("get task id of index table failed", K(ret), K(task_id), K(table_schema));
    } else if (OB_FAIL(ObDDLErrorMessageTableOperator::generate_index_ddl_error_message(ret_code, *table_schema, ObCurTraceId::get_trace_id_str(),
            error_info.task_id_, error_info.parent_task_id_, tablet_id.id(), GCTX.self_addr(), *GCTX.sql_proxy_, index_key_buffer, report_ret_code))) {
      LOG_WARN("generate index ddl error message", K(ret), K(ret), K(report_ret_code));
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::report_unique_key_dumplicated(
    const int ret_code,
    const uint64_t table_id,
    const ObBatchDatumRows &datum_rows,
    const ObTabletID &tablet_id,
    int &report_ret_code)
{
  int ret = OB_SUCCESS;
  report_ret_code = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;
  if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
          schema_guard))) {
    LOG_WARN("get tenant schema failed", K(ret), K(table_id), K(table_id));
  } else if (OB_FAIL(schema_guard.get_table_schema(
          table_id, table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist", K(ret), K(table_id));
  } else {
    const int64_t rowkey_column_num = table_schema->get_rowkey_column_num();
    ObMemAttr mem_attr("DL_Temp");
    ObArenaAllocator allocator(mem_attr);
    ObArray<ObColDesc> col_descs;
    blocksstable::ObStorageDatumUtils datum_utils;
    char index_key_buffer[OB_TMP_BUF_SIZE_256] = { 0 };
    int64_t task_id = 0;
    ObStorageDatumBuffer datum_buffer1(&allocator);
    ObStorageDatumBuffer datum_buffer2(&allocator);
    ObDatumRowkey key1, key2;
    ObDatumRowkey *index_key = nullptr, *prev_key = nullptr, *next_key = nullptr;
    ObDDLErrorMessageTableOperator::ObDDLErrorInfo error_info;
    col_descs.set_block_allocator(ModulePageAllocator(allocator));

    if (OB_FAIL(table_schema->get_rowkey_column_ids(col_descs))) {
      LOG_WARN("fail to get rowkey column ids", KR(ret));
    } else if (OB_FAIL(datum_utils.init(col_descs,
                                        rowkey_column_num,
                                        allocator,
                                        true/*no need compare multiple version cols*/))) {
      LOG_WARN("fail to init datum utils", KR(ret), K(col_descs), K(rowkey_column_num));
    }

    // init keys for compare
    if (OB_SUCC(ret)) {
      if (OB_FAIL(datum_buffer1.reserve(rowkey_column_num))) {
        LOG_WARN("reserve datum buffer failed", K(ret));
      } else if (OB_FAIL(datum_buffer2.reserve(rowkey_column_num))) {
        LOG_WARN("reserve datum buffer failed", K(ret));
      } else {
        key1.assign(datum_buffer1.get_datums(), rowkey_column_num);
        key2.assign(datum_buffer2.get_datums(), rowkey_column_num);
        prev_key = &key1;
        next_key = &key2;
      }
    }
    // find dumplicated key
    if (OB_SUCC(ret)) {
      bool is_null = false;
      const char *payload = nullptr;
      ObLength length = 0;
      ObStorageDatum tmp_datum;
      int cmp_ret = 0;
      bool find_dumplicated_key = false;
      // init prev key
      for (int64_t j = 0; OB_SUCC(ret) && j < rowkey_column_num; ++j) {
        ObIVector *vector = datum_rows.vectors_.at(j);
        vector->get_payload(0, is_null, payload, length);
        prev_key->datums_[j].shallow_copy_from_datum(ObDatum(payload, length, is_null));
      }
      for (int64_t i = 1; OB_SUCC(ret) && !find_dumplicated_key && i < datum_rows.row_count_; ++i) {
        // set next key
        for (int64_t j = 0; OB_SUCC(ret) && j < rowkey_column_num; ++j) {
          ObIVector *vector = datum_rows.vectors_.at(j);
          vector->get_payload(i, is_null, payload, length);
          next_key->datums_[j].shallow_copy_from_datum(ObDatum(payload, length, is_null));
        }
        // compare key
        if (OB_FAIL(next_key->compare(*prev_key, datum_utils, cmp_ret))) {
          LOG_WARN("fail to compare rowkey", KR(ret), KPC(prev_key), KPC(next_key));
        } else if (0 == cmp_ret) {
          find_dumplicated_key = true;
        } else {
          ObDatumRowkey *tmp_key = prev_key;
          prev_key = next_key;
          next_key = tmp_key;
        }
      }
      if (OB_SUCC(ret)) {
        index_key = prev_key;
        if (!find_dumplicated_key) {
          // first key is dumplicated key
          for (int64_t j = 0; OB_SUCC(ret) && j < rowkey_column_num; ++j) {
            ObIVector *vector = datum_rows.vectors_.at(j);
            vector->get_payload(0, is_null, payload, length);
            index_key->datums_[j].shallow_copy_from_datum(ObDatum(payload, length, is_null));
          }
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObDDLStorageUtil::extract_index_key(*table_schema, *index_key, index_key_buffer, OB_TMP_BUF_SIZE_256))) {   // read the unique key that violates the unique constraint
      LOG_WARN("extract unique index key failed", K(ret), KPC(index_key), K(index_key_buffer));
    } else if (OB_FAIL(ObDDLErrorMessageTableOperator::get_index_task_info(*GCTX.sql_proxy_, *table_schema, error_info))) {
      LOG_WARN("get task id of index table failed", K(ret), K(task_id), K(table_schema));
    } else if (OB_FAIL(ObDDLErrorMessageTableOperator::generate_index_ddl_error_message(ret_code, *table_schema, ObCurTraceId::get_trace_id_str(),
            error_info.task_id_, error_info.parent_task_id_, tablet_id.id(), GCTX.self_addr(), *GCTX.sql_proxy_, index_key_buffer, report_ret_code))) {
      LOG_WARN("generate index ddl error message", K(ret), K(ret), K(report_ret_code));
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::check_null_and_length(
    const bool is_index_table,
    const bool has_lob_rowkey, 
    const int64_t rowkey_column_num, 
    const ObDatumRow &row_val) const
{
  int ret = OB_SUCCESS;
  if (is_index_table && !has_lob_rowkey) {
    // index table is index-organized but can have null values in index column
  } else if (OB_UNLIKELY(rowkey_column_num > row_val.get_column_count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey column number", KR(ret), K(rowkey_column_num), K(row_val));
  } else {
    int64_t rowkey_length = 0;
    bool has_null = false;
    for (int64_t i = 0; i < rowkey_column_num; i++) {
      const ObStorageDatum &cell = row_val.storage_datums_[i];
      rowkey_length += cell.len_;
      has_null |= cell.is_null();
    }
    if (!is_index_table && has_null) {
      ret = OB_ER_INVALID_USE_OF_NULL;
      LOG_WARN("invalid null cell for row key column", KR(ret), K(row_val));
    }
    if (OB_SUCC(ret) && has_lob_rowkey && rowkey_length > OB_MAX_VARCHAR_LENGTH_KEY) {
      ret = OB_ERR_TOO_LONG_KEY_LENGTH;
      LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_VARCHAR_LENGTH_KEY);
      STORAGE_LOG(WARN, "rowkey is too long", K(ret), K(rowkey_length), K(rowkey_column_num), K(row_val));
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::check_null_and_length(
    const bool is_index_table,
    const bool has_lob_rowkey,
    const int64_t rowkey_column_num,
    const ObBatchDatumRows &datum_rows)
{
  int ret = OB_SUCCESS;
  if (is_index_table && !has_lob_rowkey) {
    // index table is index-organized but can have null values in index column
  } else if (OB_UNLIKELY(rowkey_column_num > datum_rows.get_column_count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey column number", KR(ret), K(rowkey_column_num), K(datum_rows.get_column_count()));
  } else {
    rowkey_lengths_.reuse();
    bool has_null = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_column_num; ++i) {
      ObIVector *vector = datum_rows.vectors_.at(i);
      for (int64_t j = 0; OB_SUCC(ret) && j < datum_rows.row_count_; ++j) {
        const int64_t col_length = vector->get_length(j);
        has_null |= vector->is_null(j);
        if (i == 0) {
          if (OB_FAIL(rowkey_lengths_.push_back(col_length))) {
            LOG_WARN("fail to push back column length", K(ret), K(col_length));
          }
        } else {
          rowkey_lengths_[j] += col_length;
        }
      }
    }
    if (OB_SUCC(ret) && !is_index_table && has_null) {
      ret = OB_ER_INVALID_USE_OF_NULL;
      LOG_WARN("invalid null cell for row key column", KR(ret), K(datum_rows));
    }
    for (int64_t i = 0; OB_SUCC(ret) && has_lob_rowkey && i < datum_rows.row_count_; ++i) {
      if (rowkey_lengths_.at(i) > OB_MAX_VARCHAR_LENGTH_KEY) {
        ret = OB_ERR_TOO_LONG_KEY_LENGTH;
        LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_VARCHAR_LENGTH_KEY);
        STORAGE_LOG(WARN, "rowkey is too long", K(ret), K(i), K(rowkey_lengths_.at(i)));
      }
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::close()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadSliceWriter not init", KR(ret), KP(this));
  } else if (nullptr != slice_store_) {
    if (OB_FAIL(slice_store_->close())) {
      LOG_WARN("close slice store failed", K(ret));
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::inner_fill_vector_index_data(
    ObMacroBlockSliceStore *&macro_block_slice_store,
    ObVectorIndexBaseSliceStore *vec_idx_slice_store,
    const int64_t snapshot_version,
    const ObStorageSchema *storage_schema,
    const SCN &start_scn,
    ObVectorIndexAlgorithmType index_type,
    ObInsertMonitor* insert_monitor)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(vec_idx_slice_store) || OB_ISNULL(storage_schema) || snapshot_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(vec_idx_slice_store), KP(storage_schema), K(snapshot_version));
  } else {
    // build macro slice
    if (OB_ISNULL(macro_block_slice_store = OB_NEWx(ObMacroBlockSliceStore, &allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory for macro block slice store failed", K(ret));
    } else if (OB_FAIL(macro_block_slice_store->init(tablet_direct_load_mgr_, start_seq_, start_scn))) {
      LOG_WARN("init macro block slice store failed", K(ret), KPC(tablet_direct_load_mgr_), K(start_seq_));
    } else {
      const int64_t rk_cnt = storage_schema->get_rowkey_column_num();
      const int64_t col_cnt = storage_schema->get_column_count();
      blocksstable::ObDatumRow *datum_row = nullptr;
      // do write
      while (OB_SUCC(ret)) {
        // build row
        if (OB_FAIL(vec_idx_slice_store->get_next_vector_data_row(rk_cnt, col_cnt, snapshot_version, index_type, datum_row))) {
          if (ret != OB_ITER_END) {
            LOG_WARN("fail to get next vector data row", K(ret), KPC(vec_idx_slice_store));
          }
        } else if (OB_FAIL(macro_block_slice_store->append_row(*datum_row))) {
          LOG_WARN("fail to append row to macro block slice store", K(ret), KPC(macro_block_slice_store));
        } else {
          LOG_INFO("[vec index debug] append one row into vec data tablet", K(tablet_direct_load_mgr_->get_tablet_id()), KPC(datum_row));
          if (OB_NOT_NULL(insert_monitor)) {
            insert_monitor->inserted_row_cnt_ =  insert_monitor->inserted_row_cnt_ + 1;
          }
        }
      }
      if (ret == OB_ITER_END) {
        ret = OB_SUCCESS;
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(macro_block_slice_store->close())) {
          LOG_WARN("fail to close macro_block_slice_store", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObDirectLoadSliceWriter::inner_fill_hnsw_vector_index_data(
    ObVectorIndexSliceStore &vec_idx_slice_store,
    const int64_t snapshot_version,
    const ObStorageSchema *storage_schema,
    const SCN &start_scn,
    const int64_t lob_inrow_threshold,
    ObInsertMonitor* insert_monitor)
{
  int ret = OB_SUCCESS;
  int end_trans_ret = OB_SUCCESS;
  ObTxDesc *tx_desc = nullptr;
  ObMacroBlockSliceStore *macro_block_slice_store = nullptr;
  ObVectorIndexAlgorithmType index_type = VIAT_MAX;
  const uint64_t timeout_us = ObTimeUtility::current_time() + ObInsertLobColumnHelper::LOB_TX_TIMEOUT;
  if (OB_ISNULL(storage_schema) || snapshot_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(vec_idx_slice_store), KP(storage_schema), K(snapshot_version));
  } else if (OB_FAIL(ObInsertLobColumnHelper::start_trans(false/*is_for_read*/, timeout_us, tx_desc))) {
    LOG_WARN("fail to get tx_desc", K(ret));
  } else if (OB_FAIL(vec_idx_slice_store.serialize_vector_index(&allocator_, tx_desc, lob_inrow_threshold, index_type, snapshot_version))) {
    LOG_WARN("fail to do vector index snapshot data serialize", K(ret));
  } else if (OB_FAIL(inner_fill_vector_index_data(macro_block_slice_store, &vec_idx_slice_store, snapshot_version, storage_schema, start_scn, index_type, insert_monitor))) {
    LOG_WARN("fail to inner fill vector index data", K(ret));
  }
  if (OB_NOT_NULL(tx_desc)) {
    if (OB_SUCCESS != (end_trans_ret = ObInsertLobColumnHelper::end_trans(tx_desc, OB_SUCCESS != ret, INT64_MAX))) {
      LOG_WARN("fail to end read trans", K(ret), K(end_trans_ret));
      ret = end_trans_ret;
    }
  }
  if (nullptr != macro_block_slice_store) {
    macro_block_slice_store->~ObMacroBlockSliceStore();
    allocator_.free(macro_block_slice_store);
  }
  return ret;
}

int ObDirectLoadSliceWriter::fill_vector_index_data(
    const int64_t snapshot_version,
    const ObStorageSchema *storage_schema,
    const SCN &start_scn,
    const ObTableSchemaItem &schema_item,
    ObInsertMonitor* insert_monitor,
    const int64_t context_id)
{
#define FILL_VECTOR_INDEX_DATA(type_str, slice_store_type) \
  slice_store_type *vec_idx_slice_store = static_cast<slice_store_type *>(slice_store_); \
  if (OB_ISNULL(vec_idx_slice_store)) { \
    slice_store_type tmp_slice_store; \
    if (OB_FAIL(tmp_slice_store.init(tablet_direct_load_mgr_, schema_item.vec_idx_param_, schema_item.vec_dim_, \
                                                tablet_direct_load_mgr_->get_column_info(), context_id))) { \
      LOG_WARN("init vector index slice store failed", K(ret), KPC(storage_schema)); \
    } else if (OB_FAIL(inner_fill_##type_str##_vector_index_data( \
        tmp_slice_store, snapshot_version, storage_schema, start_scn, schema_item.lob_inrow_threshold_, insert_monitor))) { \
      LOG_WARN("failed to fill vector index data", K(ret), K(tmp_slice_store)); \
    } \
  } else if (OB_FAIL(inner_fill_##type_str##_vector_index_data( \
      *vec_idx_slice_store, snapshot_version, storage_schema, start_scn, schema_item.lob_inrow_threshold_, insert_monitor))) { \
    LOG_WARN("failed to fill vector index data", K(ret), K(*vec_idx_slice_store)); \
  }

  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(storage_schema) || snapshot_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(storage_schema), KP(slice_store_), K(snapshot_version));
  } else if (schema::is_vec_index_snapshot_data_type(storage_schema->get_index_type())) {
    FILL_VECTOR_INDEX_DATA(hnsw, ObVectorIndexSliceStore);
  } else if (schema::is_local_vec_ivf_centroid_index(storage_schema->get_index_type())) {
    FILL_VECTOR_INDEX_DATA(ivf, ObIvfCenterSliceStore);
  } else if (schema::is_vec_ivfsq8_meta_index(storage_schema->get_index_type())) {
    FILL_VECTOR_INDEX_DATA(ivf, ObIvfSq8MetaSliceStore);
  } else if (schema::is_vec_ivfpq_pq_centroid_index(storage_schema->get_index_type())) {
    FILL_VECTOR_INDEX_DATA(ivf, ObIvfPqSliceStore);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected index type", K(ret), K(storage_schema->get_index_type()));
  }
#undef FILL_VECTOR_INDEX_DATA
  
  return ret;
}

int ObDirectLoadSliceWriter::inner_fill_ivf_vector_index_data(
    ObIvfSliceStore &vec_idx_slice_store,
    const int64_t snapshot_version,
    const ObStorageSchema *storage_schema,
    const SCN &start_scn,
    const int64_t lob_inrow_threshold,
    ObInsertMonitor* insert_monitor)
{
  UNUSED(lob_inrow_threshold);
  int ret = OB_SUCCESS;
  bool is_empty = false;
  ObMacroBlockSliceStore *macro_block_slice_store = nullptr;
  if (OB_ISNULL(storage_schema) || snapshot_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(vec_idx_slice_store), KP(storage_schema), K(snapshot_version));
  } else if (OB_FAIL(vec_idx_slice_store.is_empty(is_empty))) {
    LOG_WARN("failed to check vec_idx_slice_store", K(ret));
  } else if (is_empty) {
    // do nothing
    LOG_INFO("[vec index debug] maybe no data for this tablet", K(tablet_direct_load_mgr_->get_tablet_id()));
  } else if (OB_FAIL(vec_idx_slice_store.build_clusters(insert_monitor))) {
    LOG_WARN("fail to build clusters", K(ret));
  } else if (FALSE_IT(vec_idx_slice_store.set_lob_inrow_threshold(lob_inrow_threshold))) {
  } else if (OB_FAIL(inner_fill_vector_index_data(macro_block_slice_store, &vec_idx_slice_store, snapshot_version, storage_schema, start_scn, VIAT_MAX/*index_type*/, insert_monitor))) {
    LOG_WARN("fail to inner fill vector index data", K(ret));
  }
  { // what ever fail or success, we need to release ivf build helper
    int tmp_ret = OB_SUCCESS;
    ObIvfHelperKey key(vec_idx_slice_store.tablet_id_, vec_idx_slice_store.get_context_id());
    if (OB_TMP_FAIL(ObPluginVectorIndexUtils::erase_ivf_build_helper(key))) {
      LOG_WARN("failed to erase ivf build helper", K(tmp_ret), K(vec_idx_slice_store.tablet_id_));
    }
    if (tmp_ret != OB_SUCCESS && tmp_ret != OB_HASH_NOT_EXIST) {
      ret = ret != OB_SUCCESS ? ret : tmp_ret;
    }
  }
  if (nullptr != macro_block_slice_store) {
    macro_block_slice_store->~ObMacroBlockSliceStore();
    allocator_.free(macro_block_slice_store);
  }
  return ret;
}

void ObDirectLoadSliceWriter::cancel()
{
  ATOMIC_SET(&is_canceled_, true);
  if (OB_NOT_NULL(slice_store_)) {
    slice_store_->cancel();
  }
}

void ObTabletDirectLoadSliceGroup::reset()
{
  int ret = OB_SUCCESS;
  {
    ObBucketWLockAllGuard all_lock(bucket_lock_);
    for (auto iter = batch_slice_map_.begin(); OB_SUCC(ret) && iter != batch_slice_map_.end(); ++iter) {
      ObArray<int64_t> *cur_array = iter->second;
      cur_array->~ObArray<int64_t>();
      allocator_.free(cur_array);
      cur_array = nullptr;
    }
  }
  bucket_lock_.destroy();
  allocator_.reset();
  is_inited_ = false;
}

int ObTabletDirectLoadSliceGroup::init(const int64_t task_cnt)
{
  int ret = OB_SUCCESS;
  const int64_t memory_limit = 1024LL * 1024LL * 1024LL * 10LL; // 10GB
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(lbt()));
  } else if (OB_UNLIKELY(task_cnt < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid task cnt", K(ret), K(task_cnt));
  } else {
    ObMemAttr attr("batch_slice_map");
    if (OB_FAIL(allocator_.init(OB_MALLOC_MIDDLE_BLOCK_SIZE, "SLICE_GRP", memory_limit))) {
      LOG_WARN("init io allocator failed", K(ret));
    } else if (OB_FAIL(batch_slice_map_.create(task_cnt, attr, attr))) {
      LOG_WARN("fail to create map", K(ret), K(task_cnt));
    } else if (OB_FAIL(bucket_lock_.init(task_cnt))) {
      LOG_WARN("failed to init bucket lock", K(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

int ObVectorIndexSliceStore::init(
    ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
    const ObString vec_idx_param,
    const int64_t vec_dim,
    const ObIArray<ObColumnSchemaItem> &col_array,
    const int64_t context_id)
{
  int ret = OB_SUCCESS;
  UNUSED(context_id);
  vector_key_col_idx_ = -1;
  vector_vid_col_idx_ = -1;
  vector_col_idx_ = -1;
  vector_data_col_idx_ = -1;
  int64_t pk_increment_col_idx = -1;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(nullptr == tablet_direct_load_mgr || vec_idx_param.empty() || 0 >= vec_dim || col_array.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(tablet_direct_load_mgr));
  } else {
    const ObIArray<share::schema::ObColDesc> &col_desc_array = static_cast<ObTabletDirectLoadMgr *>(tablet_direct_load_mgr)->get_sqc_build_ctx().data_block_desc_.get_desc().get_col_desc_array();
    is_inited_ = true;
    tablet_id_ = tablet_direct_load_mgr->get_tablet_id();
    vec_idx_param_ = vec_idx_param;
    vec_dim_ = vec_dim;
    // get data tablet id and lob tablet id
    ObLS *ls = nullptr;
    ObTabletHandle five_tablet_handle;
    ObTabletHandle data_tablet_handle;
    ObTabletBindingMdsUserData ddl_data;
    if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
      LOG_WARN("failed to get log stream", K(ret));
    } else if (OB_FAIL(ls->get_tablet(tablet_id_, five_tablet_handle))) {
      LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id_));
    } else if (FALSE_IT(ctx_.data_tablet_id_ = five_tablet_handle.get_obj()->get_data_tablet_id())) {
    } else if (OB_FAIL(ls->get_tablet(ctx_.data_tablet_id_, data_tablet_handle))) {
      LOG_WARN("fail to get tablet handle", K(ret), K(ctx_.data_tablet_id_));
    } else if (OB_FAIL(data_tablet_handle.get_obj()->get_ddl_data(ddl_data))) {
      LOG_WARN("failed to get ddl data from tablet", K(ret), K(data_tablet_handle));
    } else {
      ctx_.lob_meta_tablet_id_ = ddl_data.lob_meta_tablet_id_;
      ctx_.lob_piece_tablet_id_ = ddl_data.lob_piece_tablet_id_;
    }
    // get vid col and vector col
    for (int64_t i = 0; OB_SUCC(ret) && i < col_array.count(); i++) {
      // version control col is not valid
      if (!col_array.at(i).is_valid_) {
      } else if (ObSchemaUtils::is_vec_hnsw_vid_column(col_array.at(i).column_flags_)) {
        vector_vid_col_idx_ = i;
      } else if (col_desc_array.at(i).col_id_ == OB_HIDDEN_PK_INCREMENT_COLUMN_ID) {
        pk_increment_col_idx = i;
      } else if (ObSchemaUtils::is_vec_hnsw_vector_column(col_array.at(i).column_flags_)) {
        vector_col_idx_ = i;
      } else if (ObSchemaUtils::is_vec_hnsw_key_column(col_array.at(i).column_flags_)) {
        vector_key_col_idx_ = i;
      } else if (ObSchemaUtils::is_vec_hnsw_data_column(col_array.at(i).column_flags_)) {
        vector_data_col_idx_ = i;
      } else {
        if (OB_FAIL(extra_column_idx_types_.push_back(ObExtraInfoIdxType(i, col_array.at(i).col_type_)))) {
          LOG_WARN("failed to push back extra info col idx", K(ret), K(i));
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (vector_vid_col_idx_ == -1 && pk_increment_col_idx == -1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get valid vector index col idx", K(ret), K(vector_vid_col_idx_), K(pk_increment_col_idx), K(col_array));
    } else if (vector_vid_col_idx_ == -1 && pk_increment_col_idx != -1) {
      vector_vid_col_idx_ = pk_increment_col_idx;
    } else if (vector_vid_col_idx_ != -1 && pk_increment_col_idx != -1) {
      if (OB_FAIL(extra_column_idx_types_.push_back(ObExtraInfoIdxType(pk_increment_col_idx, col_array.at(pk_increment_col_idx).col_type_)))) {
        LOG_WARN("failed to push back extra info col idx", K(ret), K(pk_increment_col_idx));
      }
    }

    if (OB_SUCC(ret)) {
      if (vector_vid_col_idx_ == -1 || vector_col_idx_ == -1 || vector_key_col_idx_ == -1 || vector_data_col_idx_ == -1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get valid vector index col idx", K(ret), K(vector_col_idx_), K(vector_vid_col_idx_),
                 K(vector_key_col_idx_), K(vector_data_col_idx_), K(col_array));
      }
    }
  }
  return ret;
}

int ObTabletDirectLoadSliceGroup::record_slice_id(const ObTabletDirectLoadBatchSliceKey &key, const int64_t slice_id)
{
  int ret = OB_SUCCESS;
  ObArray<int64_t> *slice_array = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(lbt()));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
    if (OB_FAIL(batch_slice_map_.get_refactored(key, slice_array))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("fail to set key into map", K(ret), K(key), KP(slice_array));
      } else {
        ObArray<int64_t> *new_array = nullptr;
        void *buf = nullptr;
        if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObArray<int64_t>)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("allocate memory failed", K(ret), K(sizeof(ObArray<int64_t>)));
        } else {
          new_array = new (buf) ObArray<int64_t>;
          new_array->set_attr(ObMemAttr("slice_array"));
          if (OB_FAIL(batch_slice_map_.set_refactored(key, new_array))) {
            LOG_WARN("fail to set key into map", K(ret), K(key), KP(new_array));
          } else if (OB_FAIL(new_array->push_back(slice_id))) {
            LOG_WARN("fail to push slice_writer", K(ret), K(key), K(slice_id));
          }
        }
        if (OB_FAIL(ret)) {
          if (OB_NOT_NULL(new_array)) {
            new_array->~ObArray<int64_t>();
            allocator_.free(new_array);
            new_array = nullptr;
          }
        }
      }
    } else if (OB_ISNULL(slice_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null slice", K(ret), K(key), K(slice_id));
    } else if (OB_FAIL(slice_array->push_back(slice_id))) {
      LOG_WARN("fail to push slice_writer", K(ret), K(key), K(slice_id));
    }
  }
  return ret;
}

int ObTabletDirectLoadSliceGroup::get_slice_array(const ObTabletDirectLoadBatchSliceKey &key, ObArray<int64_t> &slice_array)
{
  int ret = OB_SUCCESS;
  slice_array.reset();
  ObArray<int64_t> *cur_slice_array = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(lbt()));
  } else {
    ObBucketHashRLockGuard lock_guard(bucket_lock_, key.hash());
    if (OB_FAIL(batch_slice_map_.get_refactored(key, cur_slice_array))) {
      LOG_WARN("fail to get slice array", K(ret), K(key), KP(cur_slice_array));
    } else if (OB_ISNULL(cur_slice_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid slice array", K(ret), KP(cur_slice_array));
    } else if (OB_FAIL(slice_array.assign(*cur_slice_array))) {
      LOG_WARN("fail to copy array", K(ret));
    }
  }
  return ret;
}

int ObTabletDirectLoadSliceGroup::remove_slice_array(const ObTabletDirectLoadBatchSliceKey &key)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(lbt()));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
    ObArray<int64_t> *slice_array = nullptr;
    if (OB_FAIL(batch_slice_map_.erase_refactored(key, &slice_array))) {
      LOG_WARN("erase failed", K(ret), K(key));
    } else {
      slice_array->~ObArray<int64_t>();
      allocator_.free(slice_array);
      slice_array = nullptr;
    }
  }
  return ret;
}

int ObVectorIndexBaseSliceStore::close()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // do nothing
  }
  return ret;
}

void ObVectorIndexBaseSliceStore::reset()
{
  is_inited_ = false;
  row_cnt_ = 0;
  tablet_id_.reset();
  vec_idx_param_.reset();
  vec_dim_ = 0;
  cur_row_pos_ = 0;
  current_row_.reset();
}

int ObVectorIndexSliceStore::append_row(const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // append to vector inedx adaptor
    ObPluginVectorIndexService *vec_index_service = share::g_mp->plugin_vector_index_service();
    ObPluginVectorIndexAdapterGuard adaptor_guard;
    if (OB_ISNULL(vec_index_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get null ObPluginVectorIndexService ptr", K(ret));
    } else if (OB_FAIL(vec_index_service->acquire_adapter_guard(tablet_id_,
                                                                ObIndexType::INDEX_TYPE_VEC_INDEX_SNAPSHOT_DATA_LOCAL,
                                                                adaptor_guard,
                                                                &vec_idx_param_,
                                                                vec_dim_))) {
      LOG_WARN("fail to get ObMockPluginVectorIndexAdapter", K(ret), K(tablet_id_));
    } else {
      // get vid and vector
      ObString vec_str;
      int64_t vec_vid;
      ObVecExtraInfoObj *extra_obj = nullptr;
      int64_t extra_column_count = extra_column_idx_types_.count();
      int64_t extra_info_actual_size = 0;
      if (datum_row.get_column_count() <= vector_vid_col_idx_ || datum_row.get_column_count() <= vector_col_idx_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get valid vector index col idx", K(ret), K(vector_col_idx_), K(vector_vid_col_idx_), K(datum_row));
      } else if (datum_row.storage_datums_[vector_col_idx_].is_null() || datum_row.storage_datums_[vector_col_idx_].is_nop()) {
        // do nothing
      } else if (FALSE_IT(vec_vid = datum_row.storage_datums_[vector_vid_col_idx_].get_int())) {
      } else if (FALSE_IT(vec_str = datum_row.storage_datums_[vector_col_idx_].get_string())) {
      } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(&tmp_allocator_,
                                                                    ObLongTextType,
                                                                    CS_TYPE_BINARY,
                                                                    true,
                                                                    vec_str))) {
        LOG_WARN("fail to get real data.", K(ret), K(vec_str));
      } else if (vec_str.length() == 0) {
        // do nothing
      } else if (OB_NOT_NULL(adaptor_guard.get_adatper()) &&
                 OB_FAIL(adaptor_guard.get_adatper()->get_extra_info_actual_size(extra_info_actual_size))) {
        LOG_WARN("failed to get extra info actual size.", K(ret));
      } else {
        if (extra_column_count > 0 && extra_info_actual_size > 0) {
          char *buf = nullptr;
          if (OB_ISNULL(buf = static_cast<char *>(tmp_allocator_.alloc(sizeof(ObVecExtraInfoObj) * extra_column_count)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("allocate memory failed", K(ret), K(extra_column_count));
          } else if (OB_FALSE_IT(extra_obj = new (buf) ObVecExtraInfoObj[extra_column_count])) {
          }
          int64_t datum_row_count = datum_row.get_column_count();
          for (int64_t i = 0; OB_SUCC(ret) && i < extra_column_count; ++i) {
            if (datum_row_count <= extra_column_idx_types_.at(i).idx_) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("failed to get valid extra_info idx", K(ret), K(extra_column_idx_types_.at(i).idx_), K(datum_row));
            } else {
              const ObDatum &extra_datum = datum_row.storage_datums_[extra_column_idx_types_.at(i).idx_];
              if (OB_FAIL(extra_obj[i].from_datum(extra_datum, extra_column_idx_types_.at(i).type_))) {
                LOG_WARN("failed to from obj.", K(ret), K(extra_datum), K(extra_column_idx_types_), K(i));
              }
            }
          }
        }
        uint32_t vec_length = vec_str.length();
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(adaptor_guard.get_adatper()->add_snap_index(reinterpret_cast<float *>(vec_str.ptr()),
                                                                       &vec_vid, extra_obj, extra_column_count, 1, &vec_length))) {
          LOG_WARN("fail to build index to adaptor", K(ret), KPC(this));
        } else {
          LOG_DEBUG("[vec index debug] add into snap index success", K(tablet_id_), K(vec_vid), K(vec_str));
        }
      }
    }
  }
  tmp_allocator_.reuse();
  return ret;
}

void ObVectorIndexSliceStore::reset()
{
  ObVectorIndexBaseSliceStore::reset();
  ctx_.reset();
  vector_vid_col_idx_ = -1;
  vector_col_idx_ = -1;
  vector_key_col_idx_ = -1;
  vector_data_col_idx_ = -1;
  vec_allocator_.reset();
  tmp_allocator_.reset();
}

int ObVectorIndexSliceStore::serialize_vector_index(
    ObIAllocator *allocator,
    ObTxDesc *tx_desc,
    int64_t lob_inrow_threshold,
    ObVectorIndexAlgorithmType &type,
    const int64_t snapshot_version)
{
  int ret = OB_SUCCESS;
  tmp_allocator_.reuse();
  // first we do vsag serialize
  ObPluginVectorIndexService *vec_index_service = share::g_mp->plugin_vector_index_service();
  ObPluginVectorIndexAdapterGuard adaptor_guard;
  if (OB_ISNULL(vec_index_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null ObPluginVectorIndexService ptr", K(ret));
  } else if (OB_FAIL(vec_index_service->acquire_adapter_guard(tablet_id_,
                                                              ObIndexType::INDEX_TYPE_VEC_INDEX_SNAPSHOT_DATA_LOCAL,
                                                              adaptor_guard,
                                                              &vec_idx_param_,
                                                              vec_dim_))) {
    LOG_WARN("fail to get ObMockPluginVectorIndexAdapter", K(ret), K(tablet_id_));
  } else {
    ObHNSWSerializeCallback callback;
    ObOStreamBuf::Callback cb = callback;

    ObHNSWSerializeCallback::CbParam param;
    param.vctx_ = &ctx_;
    param.allocator_ = allocator;
    param.tmp_allocator_ = &tmp_allocator_;
    param.lob_inrow_threshold_ = lob_inrow_threshold;
    // build tx
    oceanbase::transaction::ObTransService *txs = share::g_mp->trans_service();
    oceanbase::transaction::ObTxReadSnapshot snapshot;
    int64_t timeout = ObTimeUtility::fast_current_time() + ObInsertLobColumnHelper::LOB_TX_TIMEOUT;
    if (OB_ISNULL(tx_desc)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get tx desc, get nullptr", K(ret));
    } else if (OB_FAIL(txs->get_read_snapshot(*tx_desc, transaction::ObTxIsolationLevel::RC, timeout, snapshot))) {
      LOG_WARN("fail to get snapshot", K(ret));
    } else {
      param.timeout_ = timeout;
      param.snapshot_ = &snapshot;
      param.tx_desc_ = tx_desc;
      ObPluginVectorIndexAdaptor *adp = adaptor_guard.get_adatper();
      if (OB_FAIL(adp->check_snap_hnswsq_index())) {
        LOG_WARN("failed to check snap hnswsq index", K(ret));
      } else if (OB_FAIL(adp->set_snapshot_key_prefix(tablet_id_.id(), snapshot_version, ObVectorIndexSliceStore::OB_VEC_IDX_SNAPSHOT_KEY_LENGTH))) {
        LOG_WARN("failed to set snapshot key prefix", K(ret), K(tablet_id_.id()), K(snapshot_version));
      } else if (OB_FAIL(adp->serialize(allocator, param, cb))) {
        if (OB_NOT_INIT == ret) {
          // ignore // no data in slice store
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("fail to do vsag serialize", K(ret));
        }
      } else {
        type = adp->get_snap_index_type();
        LOG_INFO("HgraphIndex finish vsag serialize for tablet", K(tablet_id_), K(ctx_.get_vals().count()), K(type));
      }
      if (OB_SUCC(ret)) {
        if (!true) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail get tenant_config", KR(ret));
        } else if (OB_FAIL(adp->renew_single_snap_index(type == VIAT_HNSW_BQ 
            || (GCONF.vector_index_memory_saving_mode && (type == VIAT_HNSW || type == VIAT_HNSW_SQ || type == VIAT_HGRAPH))))) {
          LOG_WARN("fail to renew single snap index", K(ret));
        }
      }
    }
  }
  tmp_allocator_.reuse();
  return ret;
}

bool ObVectorIndexSliceStore::is_vec_idx_col_invalid(const int64_t column_cnt) const
{
  return vector_key_col_idx_ < 0 || vector_key_col_idx_ >= column_cnt ||
         vector_data_col_idx_ < 0 || vector_data_col_idx_ >= column_cnt ||
         vector_vid_col_idx_ < 0 || vector_vid_col_idx_ >= column_cnt ||
         vector_col_idx_ < 0 || vector_col_idx_ >= column_cnt;
}

int ObVectorIndexSliceStore::get_next_vector_data_row(
    const int64_t rowkey_cnt,
    const int64_t column_cnt,
    const int64_t snapshot_version,
    ObVectorIndexAlgorithmType index_type,
    blocksstable::ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  const int64_t request_cnt = column_cnt + extra_rowkey_cnt;
  if (current_row_.get_column_count() <= 0
    && OB_FAIL(current_row_.init(vec_allocator_, request_cnt))) {
    LOG_WARN("init datum row failed", K(ret), K(request_cnt));
  } else if (OB_UNLIKELY(current_row_.get_column_count() != request_cnt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(request_cnt), "datum_row_cnt", current_row_.get_column_count());
  } else if (cur_row_pos_ >= ctx_.vals_.count()) {
    ret = OB_ITER_END;
  } else if (index_type >= VIAT_MAX) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get index type invalid.", K(ret), K(index_type));
  } else if (is_vec_idx_col_invalid(current_row_.get_column_count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, vec col idx error", K(ret), K(vector_key_col_idx_), K(vector_data_col_idx_),
             K(vector_vid_col_idx_), K(vector_col_idx_));
  } else {
    // set vec key
    int64_t key_pos = 0;
    char *key_str = static_cast<char*>(vec_allocator_.alloc(OB_VEC_IDX_SNAPSHOT_KEY_LENGTH));
    if (OB_ISNULL(key_str)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc vec key", K(ret));
    } else if (index_type == VIAT_HNSW && OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_hnsw_data_part%05ld", tablet_id_.id(), snapshot_version, cur_row_pos_))) {
      LOG_WARN("fail to build vec snapshot key str", K(ret), K(index_type));
    } else if (index_type == VIAT_HGRAPH &&
      OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_hgraph_data_part%05ld", tablet_id_.id(), snapshot_version, cur_row_pos_))) {
      LOG_WARN("fail to build vec hgraph snapshot key str", K(ret), K(index_type));
    } else if (index_type == VIAT_HNSW_SQ && OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_hnsw_sq_data_part%05ld", tablet_id_.id(), snapshot_version, cur_row_pos_))) {
      LOG_WARN("fail to build sq vec snapshot key str", K(ret), K(index_type));
    } else if (index_type == VIAT_HNSW_BQ && OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_hnsw_bq_data_part%05ld", tablet_id_.id(), snapshot_version, cur_row_pos_))) {
      LOG_WARN("fail to build bq vec snapshot key str", K(ret), K(index_type));
    } else if (index_type == VIAT_IPIVF && OB_FAIL(databuff_printf(key_str, OB_VEC_IDX_SNAPSHOT_KEY_LENGTH, key_pos, "%lu_%ld_ipivf_data_part%05ld", tablet_id_.id(), snapshot_version, cur_row_pos_))) {
      LOG_WARN("fail to build ipivf vec snapshot key str", K(ret), K(index_type));
    } else {
      current_row_.storage_datums_[vector_key_col_idx_].set_string(key_str, key_pos);
    }
    // set vec data
    if (OB_FAIL(ret)) {
    } else {
      // TODO @lhd maybe we should do deep copy
      current_row_.storage_datums_[vector_data_col_idx_].set_string(ctx_.vals_.at(cur_row_pos_));
    }
    // set vid and vec to null
    if (OB_SUCC(ret)) {
      current_row_.storage_datums_[vector_vid_col_idx_].set_null();
      current_row_.storage_datums_[vector_col_idx_].set_null();
      // set extra_info to null
      if (extra_column_idx_types_.count() > 0) {
        for (int64_t i = 0; OB_SUCC(ret) && i < extra_column_idx_types_.count(); i++) {
          current_row_.storage_datums_[extra_column_idx_types_[i].idx_].set_null();
        }
      }
    }
    if (OB_SUCC(ret)) {
      // add extra rowkey
      // TODO how to get snapshot
      current_row_.storage_datums_[rowkey_cnt].set_int(-snapshot_version);
      current_row_.storage_datums_[rowkey_cnt + 1].set_int(0);
      current_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
      datum_row = &current_row_;
      cur_row_pos_++;
    }
  }
  return ret;
}

/////////////////////
// ObIvfSliceStore //
/////////////////////

void ObIvfSliceStore::reset()
{
  ObVectorIndexBaseSliceStore::reset();
  vec_allocator_.reset();
  tmp_allocator_.reset();
}

int ObIvfSliceStore::init(ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
                          const ObString vec_idx_param,
                          const int64_t vec_dim,
                          const ObIArray<ObColumnSchemaItem> &col_array,
                          const int64_t context_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_ISNULL(tablet_direct_load_mgr)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("invalid null tablet_direct_load_mgr", K(ret));
  } else if (OB_UNLIKELY(vec_idx_param.empty() || 0 >= vec_dim || col_array.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(tablet_direct_load_mgr));
  }
  return ret;
}

///////////////////////////
// ObIvfCenterSliceStore //
///////////////////////////

void ObIvfCenterSliceStore::reset()
{
  ObIvfSliceStore::reset();
  center_id_col_idx_ = -1;
  center_vector_col_idx_ = -1;
}

int ObIvfCenterSliceStore::init(
    ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
    const ObString vec_idx_param,
    const int64_t vec_dim,
    const ObIArray<ObColumnSchemaItem> &col_array,
    const int64_t context_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(nullptr == tablet_direct_load_mgr || vec_idx_param.empty() || 0 >= vec_dim || col_array.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(tablet_direct_load_mgr));
  } else {
    tablet_id_ = tablet_direct_load_mgr->get_tablet_id();
    vec_idx_param_ = vec_idx_param;
    vec_dim_ = vec_dim;
    for (int64_t i = 0; OB_SUCC(ret) && i < col_array.count(); i++) {
      if (ObSchemaUtils::is_vec_ivf_center_id_column(col_array.at(i).column_flags_)) {
        center_id_col_idx_ = i;
      } else if (ObSchemaUtils::is_vec_ivf_center_vector_column(col_array.at(i).column_flags_)) {
        center_vector_col_idx_ = i;
      }
    }
    if (OB_SUCC(ret)) {
      ObIvfFlatBuildHelper *helper = nullptr;
      if (center_id_col_idx_ == -1 || center_vector_col_idx_ == -1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get valid vector index col idx", K(ret), K(center_id_col_idx_), K(center_vector_col_idx_), K(col_array));
      } else {
        ObPluginVectorIndexService *vec_index_service = share::g_mp->plugin_vector_index_service();
        ObIvfHelperKey key(tablet_id_, context_id);
        context_id_ = context_id;
        if (OB_ISNULL(vec_index_service)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get null ObPluginVectorIndexService ptr", K(ret));
        } else if (OB_FAIL(vec_index_service->acquire_ivf_build_helper_guard(key,
                                                                             ObIndexType::INDEX_TYPE_VEC_IVFFLAT_CENTROID_LOCAL,
                                                                             helper_guard_,
                                                                             vec_idx_param_))) {
          LOG_WARN("failed to acquire ivf build helper guard", K(ret), K(tablet_id_));
        } else if (OB_FAIL(get_spec_ivf_helper(helper))) {
          LOG_WARN("fail to get ivf flat helper", K(ret));
        } else if (OB_FAIL(helper->init_ctx(vec_dim_))) {
          LOG_WARN("failed ot init kmeans ctx", K(ret), K_(vec_dim));
        } else {
          is_inited_ = true;
        }
      }
    }
  }
  return ret;
}

int ObIvfCenterSliceStore::append_row(const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // get vid and vector
    ObString vec_str;
    ObSingleKmeansExecutor *executor = nullptr;
    ObIvfFlatBuildHelper *helper = nullptr;
    if (datum_row.get_column_count() <= center_vector_col_idx_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get valid vector index col idx", K(ret), K(center_vector_col_idx_), K(datum_row));
    } else if (datum_row.storage_datums_[center_vector_col_idx_].is_null()) {
      // do nothing // ignore
    } else if (FALSE_IT(vec_str = datum_row.storage_datums_[center_vector_col_idx_].get_string())) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(&tmp_allocator_,
                                                                  ObLongTextType,
                                                                  CS_TYPE_BINARY,
                                                                  true,
                                                                  vec_str))) {
      LOG_WARN("fail to get real data.", K(ret), K(vec_str));
    } else if (OB_FAIL(get_spec_ivf_helper(helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else if (OB_FAIL(executor->append_sample_vector(reinterpret_cast<float*>(vec_str.ptr())))) {
      LOG_WARN("failed to append sample vector", K(ret));
    } else {
      LOG_DEBUG("[vec index debug] append sample vector", K(tablet_id_), K(vec_str));
    }
  }
  tmp_allocator_.reuse();
  return ret;
}

int ObIvfCenterSliceStore::build_clusters(ObInsertMonitor* insert_monitor)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSingleKmeansExecutor *executor = nullptr;
    ObIvfFlatBuildHelper *helper = nullptr;
    if (OB_FAIL(get_spec_ivf_helper(helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else if (OB_FAIL(executor->build())) {
      LOG_WARN("failed to build clusters", K(ret));
    }
  }
  return ret;
}

int ObIvfCenterSliceStore::is_empty(bool &empty)
{
  int ret = OB_SUCCESS;
  empty = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObKmeansExecutor *executor = nullptr;
    ObIvfFlatBuildHelper *helper = nullptr;
    if (OB_FAIL(get_spec_ivf_helper(helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else {
      empty = executor->is_empty();
    }
  }
  return ret;
}

int ObIvfCenterSliceStore::get_next_vector_data_row(
    const int64_t rowkey_cnt,
    const int64_t column_cnt,
    const int64_t snapshot_version,
    ObVectorIndexAlgorithmType index_type,
    blocksstable::ObDatumRow *&datum_row)
{
  UNUSED(index_type);
  int ret = OB_SUCCESS;
  tmp_allocator_.reuse();
  ObSingleKmeansExecutor *executor = nullptr;
  const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  const int64_t request_cnt = column_cnt + extra_rowkey_cnt;
  ObIvfFlatBuildHelper *helper = nullptr;
  if (OB_FAIL(get_spec_ivf_helper(helper))) {
    LOG_WARN("fail to get ivf flat helper", K(ret));
  } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr ctx", K(ret));
  } else if (current_row_.get_column_count() <= 0
    && OB_FAIL(current_row_.init(tmp_allocator_, request_cnt))) {
    LOG_WARN("init datum row failed", K(ret), K(request_cnt));
  } else if (OB_UNLIKELY(current_row_.get_column_count() != request_cnt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(request_cnt), "datum_row_cnt", current_row_.get_column_count());
  } else if (cur_row_pos_ >= executor->get_centers_count()) {
    ret = OB_ITER_END;
  } else if (center_id_col_idx_ < 0 || center_id_col_idx_ >= current_row_.get_column_count() ||
             center_vector_col_idx_ < 0 || center_vector_col_idx_ >= current_row_.get_column_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, center col idx error", K(ret), K(center_id_col_idx_), K(center_vector_col_idx_));
  } else {
    ObString data_str;
    ObString vec_res;
    float *center_vector = nullptr;
    int64_t dim = executor->get_centers_dim();
    int64_t buf_len = OB_DOC_ID_COLUMN_BYTE_LENGTH;
    char *buf = nullptr;
    if (OB_FAIL(executor->get_center(cur_row_pos_, center_vector))) {
      LOG_WARN("fail to get center", K(ret), K(cur_row_pos_));
    } else {
      data_str.assign(reinterpret_cast<char *>(center_vector), static_cast<int64_t>(sizeof(float) * dim));
      if (OB_FAIL(ObArrayExprUtils::set_array_res(nullptr, data_str.length(), tmp_allocator_, vec_res, data_str.ptr()))) {
        LOG_WARN("failed to set array res", K(ret));
      } else if (OB_ISNULL(buf = static_cast<char*>(tmp_allocator_.alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc cid", K(ret));
      } else {
        ObString cid_str(buf_len, 0, buf);
        ObCenterId center_id(tablet_id_.id(), cur_row_pos_ + 1);
        if (OB_FAIL(ObVectorKmeansClusterHelper::set_center_id_to_string(center_id, cid_str))) {
          LOG_WARN("failed to set center_id to string", K(ret), K(center_id), K(cid_str));
        } else if (vec_res.length() > lob_inrow_threshold_ || cid_str.length() > lob_inrow_threshold_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected outrow datum in ivf vector index", 
                    K(ret), K(vec_res.length()), K(cid_str.length()), K(lob_inrow_threshold_));
        } else {
          for (int64_t idx = rowkey_cnt + extra_rowkey_cnt; idx < request_cnt; ++idx) {
            if (idx != center_id_col_idx_ && idx != center_vector_col_idx_) {
              current_row_.storage_datums_[idx].set_null(); // set null part key
            }
          }
          current_row_.storage_datums_[center_vector_col_idx_].set_string(vec_res);
          current_row_.storage_datums_[center_id_col_idx_].set_string(cid_str);
          current_row_.storage_datums_[rowkey_cnt].set_int(-snapshot_version);
          current_row_.storage_datums_[rowkey_cnt + 1].set_int(0);
          current_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
          datum_row = &current_row_;
          cur_row_pos_++;
        }
      }
    }
  }
  return ret;
}

////////////////////////////
// ObIvfSq8MetaSliceStore //
////////////////////////////

int ObIvfSq8MetaSliceStore::is_empty(bool &empty)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // NOTE(liyao): empty = false if is_inited_
    empty = false;
  }
  return ret;
}

void ObIvfSq8MetaSliceStore::reset()
{
  ObIvfSliceStore::reset();
  meta_id_col_idx_ = -1;
  meta_vector_col_idx_ = -1;
}

int ObIvfSq8MetaSliceStore::init(
    ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
    const ObString vec_idx_param,
    const int64_t vec_dim,
    const ObIArray<ObColumnSchemaItem> &col_array,
    const int64_t context_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(nullptr == tablet_direct_load_mgr)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("invalid null tablet_direct_load_mgr", K(ret));
  } else {
    tablet_id_ = tablet_direct_load_mgr->get_tablet_id();
    vec_idx_param_ = vec_idx_param;
    vec_dim_ = vec_dim;
    for (int64_t i = 0; OB_SUCC(ret) && i < col_array.count(); i++) {
      if (ObSchemaUtils::is_vec_ivf_meta_id_column(col_array.at(i).column_flags_)) {
        meta_id_col_idx_ = i;
      } else if (ObSchemaUtils::is_vec_ivf_meta_vector_column(col_array.at(i).column_flags_)) {
        meta_vector_col_idx_ = i;
      }
    }
    if (OB_SUCC(ret)) {
      ObIvfSq8BuildHelper *helper = nullptr;
      if (meta_id_col_idx_ == -1 || meta_vector_col_idx_ == -1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get valid vector index col idx", K(ret), K(meta_id_col_idx_), K(meta_vector_col_idx_), K(col_array));
      } else {
        ObPluginVectorIndexService *vec_index_service = share::g_mp->plugin_vector_index_service();
        ObIvfHelperKey key(tablet_id_, context_id);
        context_id_ = context_id;
        if (OB_ISNULL(vec_index_service)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get null ObPluginVectorIndexService ptr", K(ret));
        } else if (OB_FAIL(vec_index_service->acquire_ivf_build_helper_guard(key,
                                                                             ObIndexType::INDEX_TYPE_VEC_IVFSQ8_META_LOCAL,
                                                                             helper_guard_,
                                                                             vec_idx_param_))) {
          LOG_WARN("failed to acquire ivf build helper guard", K(ret), K(tablet_id_));
        } else if (OB_FAIL(get_spec_ivf_helper(helper))) {
          LOG_WARN("fail to get ivf flat helper", K(ret));
        } else if (OB_FAIL(helper->init_ctx(vec_dim_))) {
          LOG_WARN("failed ot init kmeans ctx", K(ret), K(vec_dim_));
        } else {
          is_inited_ = true;
        }
      }
    }
  }
  return ret;
}

int ObIvfSq8MetaSliceStore::append_row(const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // get vid and vector
    ObString vec_str;
    ObSingleKmeansExecutor *ctx = nullptr;
    ObIvfSq8BuildHelper *helper = nullptr;
    int64_t vec_dim = 0;
    if (datum_row.get_column_count() <= meta_vector_col_idx_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get valid vector index col idx", K(ret), K(meta_vector_col_idx_), K(datum_row));
    } else if (datum_row.storage_datums_[meta_vector_col_idx_].is_null()) {
      // do nothing // ignore
    } else if (FALSE_IT(vec_str = datum_row.storage_datums_[meta_vector_col_idx_].get_string())) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(&tmp_allocator_,
                                                                  ObLongTextType,
                                                                  CS_TYPE_BINARY,
                                                                  true,
                                                                  vec_str))) {
      LOG_WARN("fail to get real data.", K(ret), K(vec_str));
    } else if (OB_FAIL(get_spec_ivf_helper(helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (FALSE_IT(vec_dim = vec_str.length() / sizeof(float))) {
    } else if (OB_FAIL(helper->update(reinterpret_cast<float*>(vec_str.ptr()), vec_dim))) {
      LOG_WARN("failed to update helper", K(ret));
    } else {
      LOG_DEBUG("[vec index debug] append sample vector", K(tablet_id_), K(vec_str));
    }
  }
  tmp_allocator_.reuse();
  return ret;
}

int ObIvfSq8MetaSliceStore::build_clusters(ObInsertMonitor* insert_monitor)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObIvfSq8BuildHelper *helper = nullptr;
    if (OB_FAIL(get_spec_ivf_helper(helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_FAIL(helper->build())) {
      LOG_WARN("fail to do helper build", K(ret), KPC(helper));
    }
  }
  return ret;
}

int ObIvfSq8MetaSliceStore::get_next_vector_data_row(
    const int64_t rowkey_cnt,
    const int64_t column_cnt,
    const int64_t snapshot_version,
    ObVectorIndexAlgorithmType index_type,
    blocksstable::ObDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  UNUSED(index_type);
  tmp_allocator_.reuse();
  const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  const int64_t request_cnt = column_cnt + extra_rowkey_cnt;
  ObIvfSq8BuildHelper *helper = nullptr;
  if (current_row_.get_column_count() <= 0
    && OB_FAIL(current_row_.init(vec_allocator_, request_cnt))) {
    LOG_WARN("init datum row failed", K(ret), K(request_cnt));
  } else if (OB_UNLIKELY(current_row_.get_column_count() != request_cnt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(request_cnt), "datum_row_cnt", current_row_.get_column_count());
  } else if (cur_row_pos_ >= ObIvfConstant::SQ8_META_ROW_COUNT) {
    ret = OB_ITER_END;
  } else if (meta_id_col_idx_ < 0 || meta_id_col_idx_ >= current_row_.get_column_count() ||
             meta_vector_col_idx_ < 0 || meta_vector_col_idx_ >= current_row_.get_column_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, center col idx error", K(ret), K(meta_id_col_idx_), K(meta_vector_col_idx_));
  } else if (OB_FAIL(get_spec_ivf_helper(helper))) {
    LOG_WARN("fail to get ivf flat helper", K(ret));
  } else {
    ObString data_str;
    ObString vec_res;
    float *cur_vector = nullptr;
    int64_t buf_len = OB_DOC_ID_COLUMN_BYTE_LENGTH;
    char *buf = nullptr;
    if (OB_FAIL(helper->get_result(cur_row_pos_, cur_vector))) {
      LOG_WARN("fail to get result", K(ret));
    } else {
      data_str.assign(reinterpret_cast<char *>(cur_vector), static_cast<int64_t>(sizeof(float) * vec_dim_));
      if (OB_FAIL(ObArrayExprUtils::set_array_res(nullptr, data_str.length(), vec_allocator_, vec_res, data_str.ptr()))) {
        LOG_WARN("failed to set array res", K(ret));
      } else if (OB_ISNULL(buf = static_cast<char*>(vec_allocator_.alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc cid", K(ret));
      } else {
        ObString cid_str(buf_len, 0, buf);
        // reuse center_id encode, min: 1, max: 2, step: 3
        ObCenterId center_id(tablet_id_.id(), cur_row_pos_ + 1);
        if (OB_FAIL(ObVectorKmeansClusterHelper::set_center_id_to_string(center_id, cid_str))) {
          LOG_WARN("failed to set center_id to string", K(ret), K(center_id), K(cid_str));
        } else if (vec_res.length() > lob_inrow_threshold_ || cid_str.length() > lob_inrow_threshold_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected outrow datum in ivf vector index", 
                    K(ret), K(vec_res.length()), K(cid_str.length()), K(lob_inrow_threshold_));
        } else {
          for (int64_t i = 0; i < current_row_.get_column_count(); ++i) {
            if (meta_vector_col_idx_ == i) {
              current_row_.storage_datums_[meta_vector_col_idx_].set_string(vec_res);
            } else if (meta_id_col_idx_ == i) {
              current_row_.storage_datums_[meta_id_col_idx_].set_string(cid_str);
            } else if (rowkey_cnt == i) {
              current_row_.storage_datums_[i].set_int(-snapshot_version);
            } else if (rowkey_cnt + 1 == i) {
              current_row_.storage_datums_[i].set_int(0);
            } else {
              current_row_.storage_datums_[i].set_null(); // set part key null
            }
          }
          current_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
          datum_row = &current_row_;
          cur_row_pos_++;
        }
      }
    }
  }
  return ret;
}

///////////////////////////
// ObIvfPqSliceStore //
///////////////////////////

void ObIvfPqSliceStore::reset()
{
  ObIvfSliceStore::reset();
  pq_center_id_col_idx_ = -1;
  pq_center_vector_col_idx_ = -1;
}

int ObIvfPqSliceStore::init(
    ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
    const ObString vec_idx_param,
    const int64_t vec_dim,
    const ObIArray<ObColumnSchemaItem> &col_array,
    const int64_t context_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(nullptr == tablet_direct_load_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(tablet_direct_load_mgr));
  } else {
    tablet_id_ = tablet_direct_load_mgr->get_tablet_id();
    table_id_ = static_cast<ObTabletDirectLoadMgr *>(tablet_direct_load_mgr)->get_sqc_build_ctx().build_param_.runtime_only_param_.table_id_;
    vec_idx_param_ = vec_idx_param;
    vec_dim_ = vec_dim;
    // prepare in prepare_schema_item_on_demand -> prepare_schema_item_for_vec_idx_data
    for (int64_t i = 0; OB_SUCC(ret) && i < col_array.count(); i++) {
      if (ObSchemaUtils::is_vec_ivf_pq_center_id_column(col_array.at(i).column_flags_)) {
        pq_center_id_col_idx_ = i;
      } else if (ObSchemaUtils::is_vec_ivf_center_vector_column(col_array.at(i).column_flags_)) {
        pq_center_vector_col_idx_ = i;
      }
    }
    if (OB_SUCC(ret)) {
      ObIvfPqBuildHelper *helper = nullptr;
      context_id_ = context_id;
      if (pq_center_id_col_idx_ == -1 || pq_center_vector_col_idx_ == -1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to get valid vector index col idx", K(ret), K(pq_center_id_col_idx_), K(pq_center_vector_col_idx_), K(col_array));
      } else {
        ObPluginVectorIndexService *vec_index_service = share::g_mp->plugin_vector_index_service();
        ObIvfHelperKey key(tablet_id_, context_id);
        if (OB_ISNULL(vec_index_service) || OB_ISNULL(GCTX.ddl_sql_proxy_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get null ObPluginVectorIndexService or GCTX.ddl_sql_proxy_ ptr", K(ret), KP(vec_index_service));
        } else if (OB_FAIL(vec_index_service->acquire_ivf_build_helper_guard(key,
                                                                             ObIndexType::INDEX_TYPE_VEC_IVFPQ_PQ_CENTROID_LOCAL,
                                                                             helper_guard_,
                                                                             vec_idx_param_))) {
          LOG_WARN("failed to acquire ivf build helper guard", K(ret), K(tablet_id_));
        } else if (OB_FAIL(get_spec_ivf_helper(helper))) {
          LOG_WARN("fail to get ivf flat helper", K(ret));
        } else if (OB_FAIL(helper->init_ctx(vec_dim_))) {
          LOG_WARN("failed ot init kmeans ctx", K(ret), K_(vec_dim));
        } else {
          is_inited_ = true;
        }
      }
    }
  }
  return ret;
}

int ObIvfPqSliceStore::append_row(const blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObString residual_str;
    ObMultiKmeansExecutor *executor = nullptr;
    ObIvfPqBuildHelper *helper = nullptr;
    if (datum_row.get_column_count() <= pq_center_vector_col_idx_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get valid vector index col idx", K(ret), K(pq_center_vector_col_idx_), K(datum_row));
    } else if (datum_row.storage_datums_[pq_center_vector_col_idx_].is_null()) {
      // do nothing // ignore
    } else if (FALSE_IT(residual_str = datum_row.storage_datums_[pq_center_vector_col_idx_].get_string())) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(&tmp_allocator_,
                                                                  ObLongTextType,
                                                                  CS_TYPE_BINARY,
                                                                  true,
                                                                  residual_str))) {
      LOG_WARN("fail to get real data.", K(ret), K(residual_str));
    } else if (OB_FAIL(get_spec_ivf_helper(helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else if (OB_FAIL(executor->append_sample_vector(reinterpret_cast<float*>(residual_str.ptr())))) {
      LOG_WARN("failed to append sample vector", K(ret));
    } else {
      LOG_DEBUG("[vec index debug] append sample vector", K(tablet_id_), K(residual_str));
    }
  }
  tmp_allocator_.reuse();
  return ret;
}

int ObIvfPqSliceStore::build_clusters(ObInsertMonitor* insert_monitor)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObIvfPqBuildHelper *helper = nullptr;
    if (OB_FAIL(get_spec_ivf_helper(helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_FAIL(helper->build(table_id_, tablet_id_, insert_monitor))) {
      LOG_WARN("failed to build clusters", K(ret));
    }
  }
  return ret;
}

int ObIvfPqSliceStore::get_next_vector_data_row(
    const int64_t rowkey_cnt,
    const int64_t column_cnt,
    const int64_t snapshot_version,
    ObVectorIndexAlgorithmType index_type,
    blocksstable::ObDatumRow *&datum_row)
{
  UNUSED(index_type);
  int ret = OB_SUCCESS;
  tmp_allocator_.reuse();
  ObMultiKmeansExecutor *executor = nullptr;
  const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  const int64_t request_cnt = column_cnt + extra_rowkey_cnt;
  ObIvfPqBuildHelper *helper = nullptr;
  if (OB_FAIL(get_spec_ivf_helper(helper))) {
    LOG_WARN("fail to get ivf flat helper", K(ret));
  } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr ctx", K(ret));
  } else if (current_row_.get_column_count() <= 0
    && OB_FAIL(current_row_.init(vec_allocator_, request_cnt))) {
    LOG_WARN("init datum row failed", K(ret), K(request_cnt));
  } else if (OB_UNLIKELY(current_row_.get_column_count() != request_cnt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(request_cnt), "datum_row_cnt", current_row_.get_column_count());
  } else if (cur_row_pos_ >= executor->get_total_centers_count()) {
    ret = OB_ITER_END;
  } else if (pq_center_id_col_idx_ < 0 || pq_center_id_col_idx_ >= current_row_.get_column_count() ||
             pq_center_vector_col_idx_ < 0 || pq_center_vector_col_idx_ >= current_row_.get_column_count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error, center col idx error", K(ret), K(pq_center_id_col_idx_), K(pq_center_vector_col_idx_));
  } else {
    ObString data_str;
    ObString vec_res;
    float *center_vector = nullptr;
    int64_t dim = executor->get_centers_dim();
    int64_t buf_len = OB_DOC_ID_COLUMN_BYTE_LENGTH;
    char *buf = nullptr;
    int64_t center_count_per_kmeans = executor->get_centers_count_per_kmeans();
    if (center_count_per_kmeans == 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("upexpected zero center count", K(ret), K(center_count_per_kmeans));
    } else if (OB_FAIL(executor->get_center(cur_row_pos_, center_vector))) {
      LOG_WARN("fail to get center", K(ret), K(cur_row_pos_), K(center_count_per_kmeans));
    } else {
      data_str.assign(reinterpret_cast<char *>(center_vector), static_cast<int64_t>(sizeof(float) * dim));
      if (OB_FAIL(ObArrayExprUtils::set_array_res(nullptr, data_str.length(), vec_allocator_, vec_res, data_str.ptr()))) {
        LOG_WARN("failed to set array res", K(ret));
      } else if (OB_ISNULL(buf = static_cast<char*>(vec_allocator_.alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to alloc cid", K(ret));
      } else {
        ObString pq_cid_str(buf_len, 0, buf);
        // row_i = pq_centers[m_id - 1][center_id - 1] since m_id and center_id start from 1
        ObPqCenterId pq_center_id(tablet_id_.id(), cur_row_pos_ / center_count_per_kmeans + 1, cur_row_pos_ % center_count_per_kmeans + 1);
        if (OB_FAIL(ObVectorKmeansClusterHelper::set_pq_center_id_to_string(pq_center_id, pq_cid_str))) {
          LOG_WARN("failed to set center_id to string", K(ret), K(pq_center_id), K(pq_cid_str));
        } else if (vec_res.length() > lob_inrow_threshold_ || pq_cid_str.length() > lob_inrow_threshold_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected outrow datum in ivf vector index", 
                    K(ret), K(vec_res.length()), K(pq_cid_str.length()), K(lob_inrow_threshold_));
        } else {
          for (int64_t i = 0; i < current_row_.get_column_count(); ++i) {
            if (pq_center_vector_col_idx_ == i) {
              current_row_.storage_datums_[i].set_string(vec_res);
            } else if (pq_center_id_col_idx_ == i) {
              current_row_.storage_datums_[i].set_string(pq_cid_str);
            } else if (rowkey_cnt == i) {
              current_row_.storage_datums_[i].set_int(-snapshot_version);
            } else if (rowkey_cnt + 1 == i) {
              current_row_.storage_datums_[i].set_int(0);
            } else {
              current_row_.storage_datums_[i].set_null(); // set part key null
            }
          }
          current_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
          datum_row = &current_row_;
          cur_row_pos_++;
        }
      }
    }
  }
  return ret;
}

int ObIvfPqSliceStore::is_empty(bool &empty)
{
  int ret = OB_SUCCESS;
  empty = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObKmeansExecutor *executor = nullptr;
    ObIvfFlatBuildHelper *helper = nullptr;
    if (OB_FAIL(get_spec_ivf_helper(helper))) {
      LOG_WARN("fail to get ivf flat helper", K(ret));
    } else if (OB_ISNULL(executor = helper->get_kmeans_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected nullptr ctx", K(ret));
    } else {
      empty = executor->is_empty();
    }
  }
  return ret;
}

int ObDDLTableMergeDagParam::assign(const ObDDLTableMergeDagParam &merge_param)
{
  int ret = OB_SUCCESS;
  if (!merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_param));
  } else {
    direct_load_type_ = merge_param.direct_load_type_;
    tablet_id_        = merge_param.tablet_id_;
    rec_scn_          = merge_param.rec_scn_;
    is_commit_        = merge_param.is_commit_;
    start_scn_        = merge_param.start_scn_;
    data_format_version_ = merge_param.data_format_version_;
    snapshot_version_    = merge_param.snapshot_version_;
    table_key_           = merge_param.table_key_;
    if (is_commit_ && is_idem_type(direct_load_type_) &&
        OB_FAIL(user_data_.assign(arena_, merge_param.user_data_))) {
      LOG_WARN("failed to assign user data", K(ret));
    }
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::init(const bool for_major,
                                     const bool for_lob,
                                     const bool for_replay,
                                     const share::SCN start_scn,
                                     const ObDirectLoadType &direct_load_type,
                                     const ObDDLTaskParam &task_param,
                                     ObDDLTabletContext *tablet_ctx)
{
  int ret = OB_SUCCESS;
  ObWriteTabletParam              *tablet_param = nullptr;
  ObDDLTabletContext::MergeCtx    *merge_ctx    = nullptr;
  
  if ((is_full_direct_load(direct_load_type) && !for_replay
                                             && (0 == task_param.ddl_task_id_ || 0 == task_param.execution_id_))
      || (nullptr == tablet_ctx)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ddl task id and execution id must be valid", K(ret), K(direct_load_type), K(task_param), KPC(tablet_ctx));
  } else if (FALSE_IT(tablet_param  = for_lob ? &tablet_ctx->lob_meta_tablet_param_ :
                                                 &tablet_ctx->tablet_param_)) {
  } else if (OB_ISNULL(tablet_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet param should not be null", K(ret), K(for_lob), KPC(tablet_ctx));
  } else if (OB_ISNULL(merge_ctx  = for_lob ? &tablet_ctx->lob_merge_ctx_ :
                                              &tablet_ctx->merge_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge ctx should not be bull", K(ret));
  } else if (OB_ISNULL(tablet_param->storage_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet param should not be null", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(merge_ctx->slice_sstables_.create(DDL_SLICE_BUCKET_NUM, ObMemAttr("Ddl_Mrg_Task")))) {
    LOG_WARN("failed to create macro block checksum map", K(ret));
  } else {
    if (for_major) {
      table_key_.table_type_ = ObITable::TableType::MAJOR_SSTABLE;
      table_key_.version_range_.snapshot_version_ = task_param.snapshot_version_;
    } else {
      table_key_.table_type_ = ObITable::TableType::DDL_DUMP_SSTABLE;
      table_key_.scn_range_.start_scn_ = SCN::scn_dec(start_scn);
      table_key_.scn_range_.end_scn_ = start_scn;
    }

    if (OB_FAIL(ret)) {
    } else {
      table_key_.tablet_id_   =  for_lob ? tablet_ctx->lob_meta_tablet_id_ : tablet_ctx->tablet_id_;

      for_major_  = for_major;
      for_lob_    = for_lob;
      for_replay_ = for_replay;

      direct_load_type_ = direct_load_type;
      ddl_task_param_ = task_param;
      start_scn_ = start_scn;
      tablet_ctx_ = tablet_ctx;
      is_inited_ = true;
    }
  }
  return ret;
}

bool ObDDLTabletMergeDagParamV2::is_valid() const
{
  return is_inited_;
}

int ObDDLTabletMergeDagParamV2::get_merge_helper(ObIDDLMergeHelper *&merge_helper)
{
  int ret = OB_SUCCESS;
  merge_helper = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("merge dag param not inited", K(ret), KPC(this));
  } else if (nullptr == tablet_ctx_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ctx should not be null", K(ret), KPC(this));
  } else if (for_lob_) {
    merge_helper = tablet_ctx_->lob_merge_ctx_.merge_helper_;
  } else {
    merge_helper = tablet_ctx_->merge_ctx_.merge_helper_;
  } 
  
  if (OB_FAIL(ret)) {
  } else if (nullptr == merge_helper) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge helper should not be null", K(ret), KPC(this));
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::set_slice_sstable(const int64_t slice_idx, const ObTableHandleV2 &sstable_handle)
{
  int ret =  OB_SUCCESS;
  ObArray<ObTableHandleV2> *table_array = nullptr;
  ObDDLTabletContext::MergeCtx    *merge_ctx = nullptr;
  const int64_t row_store_sstable_slot_idx = 0;
  if (!is_inited_) {
    ret= OB_NOT_INIT;
    LOG_WARN("merge param has not been inited", K(ret), KPC(this), K(lbt()));
  } else if (OB_FAIL(get_merge_ctx(merge_ctx))) {
    LOG_WARN("failed to get merge ctx", K(ret), KPC(this));
  } else if (OB_ISNULL(merge_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge ctx should not be null", K(ret), KPC(this));
  } else if (OB_FAIL(merge_ctx->slice_sstables_.get_refactored(slice_idx, table_array))) {
    LOG_WARN("failed to get refactored", K(ret), K(slice_idx));
  }

  if (OB_FAIL(ret)) {
  } else if (row_store_sstable_slot_idx >= table_array->size()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected slice sstable array size", K(ret), K(table_array->size()));
  } else {
    table_array->at(row_store_sstable_slot_idx) = sstable_handle;
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::assign(const ObDDLTabletMergeDagParamV2 &merge_dag_param)
{
  int ret = OB_SUCCESS;
  if (!merge_dag_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_dag_param));
  } else {
    for_major_        = merge_dag_param.for_major_;
    for_replay_       = merge_dag_param.for_replay_;
    for_lob_          = merge_dag_param.for_lob_;
    merge_all_slice_  = merge_dag_param.merge_all_slice_;
    direct_load_type_ = merge_dag_param.direct_load_type_;
    start_scn_        = merge_dag_param.start_scn_;
    rec_scn_          = merge_dag_param.rec_scn_;
    table_key_        = merge_dag_param.table_key_;
    tablet_ctx_       = merge_dag_param.tablet_ctx_;
    is_inited_        = true;
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::get_tablet_param(ObTabletID &tablet_id,
                                                 ObWriteTabletParam *&tablet_param) const 
{
  int ret = OB_SUCCESS;
  tablet_param =  nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("merge dag param don't init yet", K(ret), KPC(this));
  } else if (OB_ISNULL(tablet_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ctx should not be null", K(ret), KPC(this));
  } else if (for_lob_) {
    if (!tablet_ctx_->lob_meta_tablet_id_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("lob tablet id not exist", K(ret), KPC(this));
    } else {
      tablet_id = tablet_ctx_->lob_meta_tablet_id_;
      tablet_param = &tablet_ctx_->lob_meta_tablet_param_;
    }
  } else {
    tablet_id = tablet_ctx_->tablet_id_;
    tablet_param = &tablet_ctx_->tablet_param_;
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::get_merge_ctx(ObDDLTabletContext::MergeCtx *&merge_ctx)
{
  int ret = OB_SUCCESS;
  merge_ctx = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("merge dag param don't init", K(ret), KPC(this));
  } else if (OB_ISNULL(tablet_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ctx should not be null", K(ret));
  } else if (for_lob_) {
    if (!tablet_ctx_->lob_meta_tablet_id_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("lob tablet id not exist", K(ret));
    } else {
      merge_ctx = &tablet_ctx_->lob_merge_ctx_;
    }
  } else {
    merge_ctx = &tablet_ctx_->merge_ctx_;
  }
  return ret;
}
int ObDDLTabletMergeDagParamV2::init_slice_sstable_array(hash::ObHashSet<int64_t> &slice_idxes)
{
  int ret = OB_SUCCESS;
  const int64_t row_store_sstable_slot_count = 1;
  ObDDLTabletContext::MergeCtx *merge_ctx = nullptr;

  if (OB_FAIL(get_merge_ctx(merge_ctx))) {
    LOG_WARN("failed to get merge ctx", K(ret), KPC(this));
  } else if (OB_ISNULL(merge_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge ctx should not be null", K(ret), KPC(this));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(merge_ctx->fifo_.init(ObMallocAllocator::get_instance(), OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                           ObMemAttr("ddl_tblt_prm")))) {
    LOG_WARN("failed to init fifo allocator", K(ret));
  }
  
  for (hash::ObHashSet<int64_t>::const_iterator iter = slice_idxes.begin(); OB_SUCC(ret) && iter != slice_idxes.end(); iter++) {
    char* buf = nullptr;
    ObArray<ObTableHandleV2> *table_array = nullptr;
    if (OB_ISNULL(buf = static_cast<char*>(merge_ctx->arena_.alloc(sizeof(ObArray<ObTableHandleV2>))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret));
    } else if (FALSE_IT(table_array = new (buf) ObArray<ObTableHandleV2>())) {
    } else if (OB_FAIL(merge_ctx->slice_sstables_.set_refactored(iter->first, table_array))) {
      LOG_WARN("failed to set refactorted", K(ret));
      /* destroy struct when set refactor failed */
      table_array->~ObArray<ObTableHandleV2>();
    } else if (OB_FAIL(table_array->prepare_allocate(row_store_sstable_slot_count))) {
      /* table array should have at least one size*/
      LOG_WARN("failed to prepare array size", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
    /* release mem when failed */
    for (hash::ObHashMap<int64_t, ObArray<ObTableHandleV2>*>::const_iterator iter = merge_ctx->slice_sstables_.begin();
         iter != merge_ctx->slice_sstables_.end();
         iter++) {
      if (nullptr != iter->second) {
        iter->second->~ObArray<ObTableHandleV2>();
      }
    }
    merge_ctx->slice_sstables_.destroy();
    merge_ctx->arena_.reset();
  }
  return ret;
}

int ObDDLMergeBucketLock::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("lock has been inited", K(ret));
  } else if (OB_FAIL(hash_set_.create(DDL_TABLET_BUCKET_NUM, ObMemAttr("DdlMrgBck")))) {
    LOG_WARN("failed to create hash set", K(ret));
  } else  {
    is_inited_ = true;
  }
  return ret;
}

int ObDDLMergeBucketLock::mtl_init(ObDDLMergeBucketLock *&ddl_merge_bucket_lock) 
{
  int ret = OB_SUCCESS;
  
  
  if (OB_ISNULL(ddl_merge_bucket_lock)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invlaid argument, ddl merge bucket lock should not be null", K(ret));
  } else if (OB_FAIL(ddl_merge_bucket_lock->init())) {
    LOG_WARN("failed to init bucket lock", K(ret));
  }
  return ret;
}

int ObDDLMergeBucketLock::lock(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet_id", K(ret), K(tablet_id));
  } else {
    ObMutexGuard guard(mutex_);
    if (OB_FAIL(hash_set_.set_refactored(tablet_id.id(), 0 /* not allow over write */))) {
      if (OB_HASH_EXIST == ret) {
        LOG_WARN("hash already exist", K(ret), K(tablet_id));
        ret = OB_EAGAIN;
      } else {
        LOG_WARN("failed to set refactored", K(ret), K(tablet_id));
      }
    }
  }
  return ret;
}

int ObDDLMergeBucketLock::unlock(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet id", K(ret), K(tablet_id));
  } else {
    ObMutexGuard guard(mutex_);
    if (OB_FAIL(hash_set_.erase_refactored(tablet_id.id()))) {
      if (OB_HASH_NOT_EXIST == ret) {
        LOG_WARN("lock not exist, set ret code as success", K(ret), K(tablet_id));
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to erase refacotred", K(ret));
      }
    }
  }
  return ret;
}
